/* ACVP proxy protocol handler for submitting test responses
 *
 * Copyright (C) 2018, Stephan Mueller <smueller@chronox.de>
 *
 * License: see LICENSE file in root directory
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, ALL OF
 * WHICH ARE HEREBY DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF NOT ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include "logger.h"
#include "acvpproxy.h"
#include "json_wrapper.h"
#include "internal.h"
#include "request_helper.h"
#include "threading_support.h"

static int acvp_vsid_verdict_url(const struct acvp_vsid_ctx *vsid_ctx,
				 char *url, uint32_t urllen)
{
	int ret;

	CKINT(acvp_vsid_url(vsid_ctx, url, urllen));
	CKINT(acvp_extend_string(url, urllen, "/%s", NIST_VAL_OP_RESULTS));

out:
	return ret;
}

static int acvp_testid_verdict_url(const struct acvp_testid_ctx *testid_ctx,
				   char *url, uint32_t urllen)
{
	int ret;

	CKINT(acvp_testid_url(testid_ctx, url, urllen));
	CKINT(acvp_extend_string(url, urllen, "/%s", NIST_VAL_OP_RESULTS));

out:
	return ret;
}

/*****************************************************************************
 * Remember the test verdicts of the vsIDs
 *****************************************************************************/
#define ACVP_VERDICT_MAX	512
static uint32_t acvp_verdict[2][ACVP_VERDICT_MAX];
static atomic_t acvp_verdict_ptr[2] = { ATOMIC_INIT(-1), ATOMIC_INIT(-1) };

static void acvp_record_verdict_vsid(uint32_t vsid, bool passed)
{
	int idx = atomic_inc(&acvp_verdict_ptr[passed]);

	if (idx < 0 || idx >= ACVP_VERDICT_MAX) {
		logger(LOGGER_WARN, LOGGER_C_ANY,
		       "Cannot track verdict vsID %u for %s verdicts\n", vsid,
			passed ? "passed": "failed");
		return;
	}

	acvp_verdict[passed][idx] = vsid;
}

DSO_PUBLIC
int acvp_list_verdict_vsid(int *idx_ptr, uint32_t *vsid, bool passed)
{
	int idx = atomic_read(&acvp_verdict_ptr[passed]);

	if (*idx_ptr > idx)
		return -ENOENT;

	idx = *idx_ptr;

	if (idx < 0 || idx >= ACVP_VERDICT_MAX)
		return -ENOENT;

	*vsid = acvp_verdict[passed][idx];
	*idx_ptr = *idx_ptr + 1;

	return 0;
}

/*****************************************************************************
 * Code for submitting test results and fetching the verdicts
 *****************************************************************************/
static int acvp_check_verdict(const struct acvp_vsid_ctx *vsid_ctx,
			      const struct acvp_buf *verdict_buf)
{
	struct json_object *verdict_full = NULL, *verdict;
	int ret;
	const char *result;

	CKINT_LOG(acvp_req_strip_version(verdict_buf->buf, &verdict_full,
					 &verdict),
		  "JSON parser cannot parse verdict data\n");

	CKINT_LOG(json_get_string(verdict, "disposition", &result),
		  "JSON parser cannot find verdict data\n");

	if (strncmp(result, "passed", 6)) {
		acvp_record_verdict_vsid(vsid_ctx->vsid, false);
	} else {
		acvp_record_verdict_vsid(vsid_ctx->vsid, true);
	}

out:
	ACVP_JSON_PUT_NULL(verdict_full);
	return ret;
}

/* GET /testSessions/<testSessionId>/vectorSets/<vectorSetId>/results */
static int acvp_get_vsid_verdict(const struct acvp_vsid_ctx *vsid_ctx)
{
	const struct acvp_testid_ctx *testid_ctx = vsid_ctx->testid_ctx;
	const struct acvp_ctx *ctx = testid_ctx->ctx;
	const struct acvp_datastore_ctx *datastore = &ctx->datastore;
	ACVP_BUFFER_INIT(result);
	char url[ACVP_NET_URL_MAXLEN];
	int ret;

	/*
	 * Construct the URL to get the server's response (i.e. final verdict)
	 * for the given results.
	 */
	CKINT(acvp_vsid_verdict_url(vsid_ctx, url, sizeof(url)));
	logger(LOGGER_DEBUG, LOGGER_C_ANY,
	       "Retrieve test results from URL %s\n", url);

	/* Submit request and prepare for a retry reply. */
	CKINT(_acvp_process_retry(vsid_ctx, &result, url,
				  acvp_store_verdict_debug));

	/* Store the entire received response. */
	if (result.buf && result.len)
		CKINT(ds->acvp_datastore_write_vsid(vsid_ctx,
						    datastore->verdictfile,
						    false, &result));

	/* Unconstify allowed as we operate on an atomic primitive. */
	atomic_inc((atomic_t *)&testid_ctx->vsids_processed);
	atomic_inc(&glob_vsids_processed);

	logger_status(LOGGER_C_ANY,
		      "Verdict obtained for testID %u / vsID %u (completed vsIDs %u / %u)\n",
		      testid_ctx->testid, vsid_ctx->vsid,
		      atomic_read(&glob_vsids_processed),
		      atomic_read(&glob_vsids_to_process));

	/*
	 * Get the global verdict for the vsID to allow it to be listed
	 * to the user.
	 */
	ret = acvp_check_verdict(vsid_ctx, &result);
	if (ret) {
		logger(LOGGER_WARN, LOGGER_C_ANY,
		       "Verdict verification failed for vsID %u\n",
		       vsid_ctx->vsid);
		/*
		 * We only log the error, but do not cause the operation to
		 * fail, since this is a convenience function.
		 */
		ret = 0;
	}

out:
	acvp_free_buf(&result);
	return ret;
}

/* POST, PUT /testSessions/<testSessionId>/vectorSets/<vectorSetId>/results */
static int acvp_response_upload(const struct acvp_vsid_ctx *vsid_ctx,
				const struct acvp_buf *buf)
{
	const struct acvp_testid_ctx *testid_ctx = vsid_ctx->testid_ctx;
	const struct acvp_ctx *ctx;
	const struct acvp_net_ctx *net;
	const struct acvp_datastore_ctx *datastore;
	struct acvp_auth_ctx *auth;
	struct acvp_na_ex netinfo;
	ACVP_BUFFER_INIT(tmp);
	ACVP_BUFFER_INIT(result);
	char url[ACVP_NET_URL_MAXLEN];
	int ret, ret2;

	ctx = testid_ctx->ctx;
	datastore = &ctx->datastore;
	auth = testid_ctx->server_auth;

	/* Refresh the ACVP JWT token by re-logging in. */
	CKINT(acvp_login(testid_ctx));

	/* Construct the URL to submit the results for the given vsID to. */
	CKINT(acvp_vsid_verdict_url(vsid_ctx, url, sizeof(url)));

	/* Send the data */
	CKINT(acvp_get_net(&net));
	netinfo.net = net;
	netinfo.url = url;
	netinfo.server_auth = auth;
	mutex_reader_lock(&auth->mutex);
	if (vsid_ctx->resubmit_result)
		ret2 = na->acvp_http_put(&netinfo, buf, &result);
	else
		ret2 = na->acvp_http_post(&netinfo, buf, &result);
	mutex_reader_unlock(&auth->mutex);

	logger(ret2 ? LOGGER_ERR : LOGGER_DEBUG, LOGGER_C_ANY,
	       "Process following server response: %s\n", result.buf);

	/* Store the debug information */
	if (result.buf && result.len)
		CKINT(acvp_store_submit_debug(vsid_ctx, &result, ret2));

	if (ret2) {
		ret = ret2;
		goto out;
	}

	/*
	 * Store status in verdict file to indicate that test responses
	 * were uploaded to ACVP server.
	 */
	tmp.buf = (uint8_t *)"{ \"status\" : \"Test responses uploaded, verdict download pending\" }\n";
	tmp.len = strlen((char *)tmp.buf);
	CKINT(ds->acvp_datastore_write_vsid(vsid_ctx, datastore->verdictfile,
					    false, &tmp));

out:
	if (ret)
		logger(LOGGER_ERR, LOGGER_C_ANY,
		       "Failure to submit testID %u with vsID %u\n",
		       testid_ctx->testid, vsid_ctx->vsid);
	acvp_free_buf(&result);
	return ret;
}

static int acvp_response_submit_one(const struct acvp_vsid_ctx *vsid_ctx,
				    const struct acvp_buf *buf)
{
	const struct acvp_testid_ctx *testid_ctx = vsid_ctx->testid_ctx;
	const struct acvp_ctx *ctx;
	const struct acvp_net_ctx *net;
	const struct acvp_datastore_ctx *datastore;
	ACVP_BUFFER_INIT(tmp);
	int ret;

	ctx = testid_ctx->ctx;
	datastore = &ctx->datastore;
	CKINT(acvp_get_net(&net));

	tmp.buf = (uint8_t *)net->server_name;
	tmp.len = strlen((char *)tmp.buf);
	ret = ds->acvp_datastore_compare(vsid_ctx, datastore->srcserver, true,
					 &tmp);
	if (ret < 0) {
		logger(LOGGER_WARN, LOGGER_C_ANY,
		       "Could not match the upload server for vsID %u with the download server\n",
		       vsid_ctx->vsid);
		goto out;
	}

	/*
	 * The server which we used for downloading the vector is not the
	 * same as for uploading.
	 *
	 * This check implicitly enforces that when test vectors are downloaded
	 * from the demo/debug ACVP server, the vectors cannot be uploaded to
	 * the production ACVP server and vice versa.
	 */
	if (!ret) {
		logger(LOGGER_WARN, LOGGER_C_ANY,
		       "vsID %u was downloaded from a different server than it shall be uploaded to (%s)\n",
		       vsid_ctx->vsid, net->server_name);
		ret = -EOPNOTSUPP;
		goto out;
	}

	/*
	 * Only upload test results if not already done so or explicitly
	 * requested by user.
	 */
	if (!vsid_ctx->verdict_file_present || vsid_ctx->resubmit_result) {
		CKINT(acvp_response_upload(vsid_ctx, buf));
	}

	CKINT(acvp_get_vsid_verdict(vsid_ctx));

out:
	return ret;
}

static int acvp_process_one_vsid(const struct acvp_vsid_ctx *vsid_ctx,
				 const struct acvp_buf *buf)
{
	const struct acvp_testid_ctx *testid_ctx;
	const struct acvp_ctx *ctx;
	const struct acvp_req_ctx *req;
	int ret;

	CKNULL_LOG(vsid_ctx, -EINVAL, "ACVP vsID request context missing\n");
	testid_ctx = vsid_ctx->testid_ctx;

	CKNULL_LOG(testid_ctx, -EINVAL,
		   "ACVP testID request context missing\n");
	ctx = testid_ctx->ctx;
	req = &ctx->req_details;

	if (!buf) {
		/*
		 * We are requested to download pending vsIDs.
		 */
		if (req->download_pending_vsid &&
		    !vsid_ctx->vector_file_present) {
			/*
			 * Unconstify allowed as we operate on an atomic
			 * primitive.
			 */
			atomic_inc((atomic_t *)&testid_ctx->vsids_to_process);
			atomic_inc(&glob_vsids_to_process);

			ret = acvp_get_testvectors(vsid_ctx);

			/* Store the time the download took */
			acvp_record_vsid_duration(vsid_ctx,
						  ACVP_DS_DOWNLOADDURATION);

			/*
			 * Indicate to the caller that the connection was
			 * restarted. Use positive integer as this is no error.
			 */
			if (!ret)
				ret = EINTR;

			return ret;
		} else {
			CKINT(acvp_get_testvectors_expected(vsid_ctx));

			/*
			 * If no buffer was provided, we do not process
			 * anything. Yet we do not return an error, because
			 * the data store backend may find other responses to
			 * submit.
			 */
			return EINTR;
		}
	}
	/*
	 * If we are requested to only download pending vsIDs, do not submit
	 * anything, but possibly we have to download expected results.
	 */
	if (req->download_pending_vsid)
		return acvp_get_testvectors_expected(vsid_ctx);

	/* Unconstify allowed as we operate on an atomic primitive. */
	atomic_inc((atomic_t *)&testid_ctx->vsids_to_process);
	atomic_inc(&glob_vsids_to_process);

	ret = acvp_response_submit_one(vsid_ctx, buf);

	/* Store the time the upload took */
	acvp_record_vsid_duration(vsid_ctx, ACVP_DS_UPLOADDURATION);

out:
	return ret;
}

static int acvp_respond_testid(struct acvp_testid_ctx *testid_ctx)
{
	int ret;

	CKNULL_LOG(testid_ctx, -EINVAL,
		   "ACVP volatile request context missing\n");

	CKINT(acvp_init_auth(testid_ctx));

	/* Get auth token for test session */
	CKINT(ds->acvp_datastore_read_authtoken(testid_ctx));

	sig_enqueue_ctx(testid_ctx);

	CKINT(ds->acvp_datastore_find_responses(testid_ctx,
						acvp_process_one_vsid));

out:
	sig_dequeue_ctx(testid_ctx);
	acvp_release_auth(testid_ctx);

	return ret;
}

/*
 * Obtain the final verdict for the test session if all test results
 * were downloaded.
 */
/* GET /testSessions/<testSessionId>/results */
static int acvp_get_testid_verdict(struct acvp_testid_ctx *testid_ctx)
{
	const struct acvp_ctx *ctx = testid_ctx->ctx;
	const struct acvp_datastore_ctx *datastore = &ctx->datastore;
	ACVP_BUFFER_INIT(result);
	int ret;
	char url[ACVP_NET_URL_MAXLEN];

	if (!testid_ctx->testid ||
	    (atomic_read(&testid_ctx->vsids_processed) !=
	     atomic_read(&testid_ctx->vsids_to_process))) {
		logger(LOGGER_VERBOSE, LOGGER_C_ANY,
		       "Not all test verdicts downloaded, skipping retrival of test session verdict\n");
		return 0;
	}

	CKINT(acvp_init_auth(testid_ctx));

	/* Get auth token for test session */
	CKINT(ds->acvp_datastore_read_authtoken(testid_ctx));

	/*
	 * Construct the URL to get the server's response
	 * (i.e. final verdict) for the test session.
	 */
	CKINT(acvp_testid_verdict_url(testid_ctx, url, sizeof(url)));
	logger(LOGGER_DEBUG, LOGGER_C_ANY,
	       "Retrieve test session results from URL %s\n", url);

	/* Submit request and prepare for a retry reply. */
	CKINT(_acvp_process_retry_testid(testid_ctx, &result, url));

	/* Store the entire received response. */
	if (result.buf && result.len) {
		ret = ds->acvp_datastore_write_testid(testid_ctx,
						      datastore->verdictfile,
						      false, &result);
	}

	logger(LOGGER_VERBOSE, LOGGER_C_ANY,
	       "All test verdicts successfully obtained for testID %u\n",
	       testid_ctx->testid);

out:
	acvp_free_buf(&result);
	acvp_release_auth(testid_ctx);
	return ret;
}

static int _acvp_respond(const struct acvp_ctx *ctx,
			 const struct definition *def, uint32_t testid)
{

	struct acvp_testid_ctx *testid_ctx = NULL;
	int ret;

	testid_ctx = calloc(1, sizeof(*testid_ctx));
	CKNULL(testid_ctx, -ENOMEM);

	testid_ctx->def = def;
	testid_ctx->ctx = ctx;
	testid_ctx->testid = testid;
	atomic_set(0, &testid_ctx->vsids_to_process);
	atomic_set(0, &testid_ctx->vsids_processed);

	if (clock_gettime(CLOCK_REALTIME, &testid_ctx->start)) {
		ret = -errno;
		goto out;
	}

	CKINT(acvp_respond_testid(testid_ctx));

	/*
	 * Skip re-downloading test session verdict if we have already obtained
	 * it or when we restarted the test vector download.
	 */
	if (ret != EEXIST && ret != EINTR) {
		CKINT(acvp_get_testid_verdict(testid_ctx));
	}

out:
	if (atomic_read(&testid_ctx->vsids_processed) <
	    atomic_read(&testid_ctx->vsids_to_process)) {
		logger(LOGGER_WARN, LOGGER_C_ANY,
		       "Not all test verdicts obtained for testID %u (%u missing) - use options --testid %u to retry fetching them\n",
		       testid_ctx->testid,
		       atomic_read(&testid_ctx->vsids_to_process) -
		       atomic_read(&testid_ctx->vsids_processed),
		       testid_ctx->testid);
	}

	/* Store the time the upload took */
	if (!ret || atomic_read(&testid_ctx->vsids_to_process))
		acvp_record_testid_duration(testid_ctx, ACVP_DS_UPLOADDURATION);

	acvp_release_testid(testid_ctx);

	return ret;
}

#ifdef ACVP_USE_PTHREAD
static int acvp_process_testids_thread(void *arg)
{
	struct acvp_thread_reqresp_ctx *tdata = arg;
	const struct acvp_ctx *ctx = tdata->ctx;
	const struct definition *def = tdata->def;
	uint32_t testid = tdata->testid;
	int (*cb)(const struct acvp_ctx *ctx, const struct definition *def,
		  uint32_t testid) = tdata->cb;

	free(tdata);

	return cb(ctx, def, testid);
}
#endif

int acvp_process_testids(const struct acvp_ctx *ctx,
			 int (*cb)(const struct acvp_ctx *ctx,
				   const struct definition *def,
				   uint32_t testid))
{
	const struct acvp_datastore_ctx *datastore;
	const struct acvp_search_ctx *search;
	struct definition *def;
	uint32_t testids[ACVP_REQ_MAX_FAILED_TESTID];
	int ret;

	CKNULL_LOG(ctx, -EINVAL, "ACVP request context missing\n");

	if (!acvp_library_initialized()) {
		logger(LOGGER_ERR, LOGGER_C_ANY,
		       "ACVP library was not yet initialized\n");
		return -EOPNOTSUPP;
	}

	datastore = &ctx->datastore;
	search = &datastore->search;

	/* Find a module definition */
	def = acvp_find_def(search, NULL);
	if (!def) {
		logger(LOGGER_ERR, LOGGER_C_ANY,
		       "No cipher implementation found for search criteria\n");
		return -EINVAL;
	}

	/*
	 * Use thread group 0 for the register upload of one cipher definition
	 * and thread group 1 for upload of the individual vsIDs.
	 *
	 * We have one thread per test session ID. Each test session ID thread
	 * spawns one thread per vsID for uploading the test responses and
	 * downloading the verdict.
	 *
	 * The threads for the test sessions are spawned all at
	 * the beginning (to the extent possible). Thus, if we have more test
	 * sessions to be processed at the same time as threads, we will
	 * not be able to spawn any thread for uploading a vsID which will
	 * cause a deadlock. Thus, we use different thread groups for
	 * these interdependent threads to prevent that there can be a deadlock.
	 */

	/* Iterate through all modules */
	while (def) {
		unsigned int testid_count = ACVP_REQ_MAX_FAILED_TESTID;
		unsigned int i;

		/* Search for all testids for a given module */
		CKINT(ds->acvp_datastore_find_testsession(def, ctx,
							  testids,
							  &testid_count));

		/* Iterate through all testids */
		for (i = 0; i < testid_count; i++) {

#ifdef ACVP_USE_PTHREAD
			/* Disable threading in DEBUG mode */
			if (logger_get_verbosity(LOGGER_C_ANY) >= LOGGER_DEBUG) {
				logger(LOGGER_DEBUG, LOGGER_C_ANY,
				       "Disable threading support\n");
				CKINT(cb(ctx, def, testids[i]));
			} else {
				struct acvp_thread_reqresp_ctx *tdata;
				int ret_ancestor;

				tdata = calloc(1, sizeof(*tdata));
				CKNULL(tdata, -ENOMEM);
				tdata->ctx = ctx;
				tdata->def = def;
				tdata->testid = testids[i];
				tdata->cb = cb;
				CKINT(thread_start(acvp_process_testids_thread,
						   tdata, 0, &ret_ancestor));
				ret |= ret_ancestor;
			}
#else
			CKINT(cb(ctx, new_def, testids[i]));
#endif
		}

		/* Check if we find another module definition. */
		def = acvp_find_def(search, def);
	}

out:

#ifdef ACVP_USE_PTHREAD
	ret |= thread_wait();
#endif

	return ret;
}

DSO_PUBLIC
int acvp_respond(const struct acvp_ctx *ctx)
{
	return acvp_process_testids(ctx, &_acvp_respond);
}
