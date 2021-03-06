/* JSON generator for symmetric ciphers
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

#include "definition.h"
#include "logger.h"
#include "acvpproxy.h"
#include "internal.h"
#include "request_helper.h"

/*
 * Generate algorithm entry for symmetric ciphers
 */
int acvp_req_set_algo_sym(const struct def_algo_sym *sym,
			  struct json_object *entry)
{
	struct json_object *tmp_array = NULL, *tmp = NULL;
	int ret = -EINVAL;

	CKINT(acvp_req_cipher_to_string(entry, sym->algorithm,
				        ACVP_CIPHERTYPE_AES |
				        ACVP_CIPHERTYPE_TDES |
				        ACVP_CIPHERTYPE_AEAD,
					"algorithm"));

	CKINT(acvp_req_gen_prereq(sym->prereqvals, sym->prereqvals_num,
				  entry));

	tmp_array = json_object_new_array();
	CKNULL(tmp_array, -ENOMEM);
	if (sym->direction & DEF_ALG_SYM_DIRECTION_ENCRYPTION)
		CKINT(json_object_array_add(tmp_array,
					    json_object_new_string("encrypt")));
	if (sym->direction & DEF_ALG_SYM_DIRECTION_DECRYPTION)
		CKINT(json_object_array_add(tmp_array,
					    json_object_new_string("decrypt")));
	CKINT(json_object_object_add(entry, "direction", tmp_array));
	tmp_array = NULL;

	CKINT(acvp_req_sym_keylen(entry, sym->keylen));

	CKINT(acvp_req_algo_int_array_always(entry, sym->ptlen, "payloadLen"));

	CKINT(acvp_req_algo_int_array(entry, sym->ivlen, "ivLen"));

	if (acvp_match_cipher(sym->algorithm, ACVP_GCM) && !sym->ivgen) {
		logger(LOGGER_ERR, LOGGER_C_ANY,
		       "GCM mode definition: ivgenmode setting missing\n");
		ret = -EINVAL;
		goto out;
	}
	switch (sym->ivgen) {
	case DEF_ALG_SYM_IVGEN_UNDEF:
		/* Do nothing */
		break;
	case DEF_ALG_SYM_IVGEN_INTERNAL:
		CKINT(json_object_object_add(entry, "ivGen",
					json_object_new_string("internal")));
		break;
	case DEF_ALG_SYM_IVGEN_EXTERNAL:
		CKINT(json_object_object_add(entry, "ivGen",
					json_object_new_string("external")));
		break;
	default:
		logger(LOGGER_WARN, LOGGER_C_ANY,
		       "Unknown IV generator definition\n");
		ret = -EINVAL;
		goto out;
		break;
	}

	if (acvp_match_cipher(sym->algorithm, ACVP_GCM) && !sym->ivgenmode) {
		logger(LOGGER_ERR, LOGGER_C_ANY,
		       "GCM mode definition: ivgenmode setting missing\n");
		ret = -EINVAL;
		goto out;
	}
	switch (sym->ivgenmode) {
	case DEF_ALG_SYM_IVGENMODE_UNDEF:
		/* Do nothing */
		break;
	case DEF_ALG_SYM_IVGENMODE_821:
		CKINT(json_object_object_add(entry, "ivGenMode",
					     json_object_new_string("8.2.1")));
		break;
	case DEF_ALG_SYM_IVGENMODE_822:
		CKINT(json_object_object_add(entry, "ivGenMode",
					     json_object_new_string("8.2.2")));
		break;
	default:
		logger(LOGGER_WARN, LOGGER_C_ANY,
		       "Unknown IV generator mode definition\n");
		ret = -EINVAL;
		goto out;
		break;
	}

	switch (sym->saltgen) {
	case DEF_ALG_SYM_SALTGEN_UNDEF:
		/* Do nothing */
		break;
	case DEF_ALG_SYM_SALTGEN_INTERNAL:
		CKINT(json_object_object_add(entry, "saltGen",
					json_object_new_string("internal")));
		break;
	case DEF_ALG_SYM_SALTGEN_EXTERNAL:
		CKINT(json_object_object_add(entry, "saltGen",
					json_object_new_string("external")));
		break;
	default:
		logger(LOGGER_WARN, LOGGER_C_ANY,
		       "Unknown salt generator definition\n");
		ret = -EINVAL;
		goto out;
		break;
	}

	if ((acvp_match_cipher(sym->algorithm, ACVP_GCM) ||
	     acvp_match_cipher(sym->algorithm, ACVP_CCM)) &&
	    !sym->aadlen[0]) {
		logger(LOGGER_ERR, LOGGER_C_ANY,
		       "GCM/CCM mode definition: aadlen setting missing\n");
		ret = -EINVAL;
		goto out;
	}
	CKINT(acvp_req_algo_int_array(entry, sym->aadlen, "aadLen"));

	if ((acvp_match_cipher(sym->algorithm, ACVP_GCM) ||
	     acvp_match_cipher(sym->algorithm, ACVP_CCM)) &&
	    !sym->taglen[0]) {
		logger(LOGGER_ERR, LOGGER_C_ANY,
		       "GCM/CCM mode definition: taglen setting missing\n");
		ret = -EINVAL;
		goto out;
	}
	CKINT(acvp_req_algo_int_array(entry, sym->taglen, "tagLen"));

	if ((acvp_match_cipher(sym->algorithm, ACVP_KW) ||
	     acvp_match_cipher(sym->algorithm, ACVP_KWP) ||
	     acvp_match_cipher(sym->algorithm, ACVP_TDESKW)) &&
	    !sym->kwcipher) {
		logger(LOGGER_ERR, LOGGER_C_ANY,
		       "KW mode definition: kwcipher setting missing %lu\n",
		       sym->algorithm);
		ret = -EINVAL;
		goto out;
	}
	if (sym->kwcipher) {
		tmp_array = json_object_new_array();
		CKNULL(tmp_array, -ENOMEM);
		if (sym->kwcipher & DEF_ALG_SYM_KW_CIPHER)
			CKINT(json_object_array_add(tmp_array,
					json_object_new_string("cipher")));
		if (sym->kwcipher & DEF_ALG_SYM_KW_INVERSE)
			CKINT(json_object_array_add(tmp_array,
					json_object_new_string("inverse")));
		CKINT(json_object_object_add(entry, "kwCipher", tmp_array));
		tmp_array = NULL;
	}

	if (acvp_match_cipher(sym->algorithm, ACVP_XTS) && !sym->tweakformat) {
		logger(LOGGER_ERR, LOGGER_C_ANY,
		       "XTS mode definition: tweakformat setting missing\n");
		ret = -EINVAL;
		goto out;
	}
	if (sym->tweakformat) {
		tmp_array = json_object_new_array();
		CKNULL(tmp_array, -ENOMEM);
		if (sym->tweakformat & DEF_ALG_SYM_XTS_TWEAK_128HEX)
			CKINT(json_object_array_add(tmp_array,
					json_object_new_string("128hex")));
		if (sym->tweakformat & DEF_ALG_SYM_XTS_TWEAK_DUSEQUENCE)
			CKINT(json_object_array_add(tmp_array,
					json_object_new_string("duSequence")));
		CKINT(json_object_object_add(entry, "tweakFormat", tmp_array));
		tmp_array = NULL;
	}

	if (acvp_match_cipher(sym->algorithm, ACVP_XTS) && !sym->tweakmode) {
		logger(LOGGER_ERR, LOGGER_C_ANY,
		       "XTS mode definition: tweakmode setting missing\n");
		ret = -EINVAL;
		goto out;
	}
	if (sym->tweakmode) {
		tmp_array = json_object_new_array();
		CKNULL(tmp_array, -ENOMEM);
		if (sym->tweakformat & DEF_ALG_SYM_XTS_TWEAK_HEX)
			CKINT(json_object_array_add(tmp_array,
					json_object_new_string("hex")));
		if (sym->tweakformat & DEF_ALG_SYM_XTS_TWEAK_NUM)
			CKINT(json_object_array_add(tmp_array,
					json_object_new_string("number")));
		CKINT(json_object_object_add(entry, "tweakMode", tmp_array));
		tmp_array = NULL;
	}

	CKINT(acvp_req_tdes_keyopt(entry, sym->algorithm));

	if ((acvp_match_cipher(sym->algorithm, ACVP_CTR) ||
	     acvp_match_cipher(sym->algorithm, ACVP_TDESCTR)) &&
	    !sym->ctrsource) {
		logger(LOGGER_ERR, LOGGER_C_ANY,
		       "CTR mode definition: ctrsource setting missing\n");
		ret = -EINVAL;
		goto out;
	}
	switch (sym->ctrsource) {
	case DEF_ALG_SYM_CTR_UNDEF:
		/* Do nothing */
		break;
	case DEF_ALG_SYM_CTR_INTERNAL:
		CKINT(json_object_object_add(entry, "ctrSource",
					json_object_new_string("internal")));
		break;
	case DEF_ALG_SYM_CTR_EXTERNAL:
		CKINT(json_object_object_add(entry, "ctrSource",
					json_object_new_string("external")));
		break;
	default:
		logger(LOGGER_WARN, LOGGER_C_ANY,
		       "Unknown CTR source definition\n");
		ret = -EINVAL;
		goto out;
		break;
	}

	if ((acvp_match_cipher(sym->algorithm, ACVP_CTR) ||
	     acvp_match_cipher(sym->algorithm, ACVP_TDESCTR)) &&
	    !sym->ctroverflow) {
		logger(LOGGER_ERR, LOGGER_C_ANY,
		       "CTR mode definition: ctroverflow setting missing\n");
		ret = -EINVAL;
		goto out;
	}
	switch (sym->ctroverflow) {
	case DEF_ALG_SYM_CTROVERFLOW_UNDEF:
		/* Do nothing */
		break;
	case DEF_ALG_SYM_CTROVERFLOW_HANDLED:
		CKINT(json_object_object_add(entry, "overflowCounter",
					     json_object_new_boolean(1)));
		break;
	case DEF_ALG_SYM_CTR_EXTERNAL:
		CKINT(json_object_object_add(entry, "overflowCounter",
					     json_object_new_boolean(0)));
		break;
	default:
		logger(LOGGER_WARN, LOGGER_C_ANY,
		       "Unknown CTR overflow definition\n");
		ret = -EINVAL;
		goto out;
		break;
	}

	if ((acvp_match_cipher(sym->algorithm, ACVP_CTR) ||
	     acvp_match_cipher(sym->algorithm, ACVP_TDESCTR)) &&
	    !sym->ctrincrement) {
		logger(LOGGER_ERR, LOGGER_C_ANY,
		       "CTR mode definition: ctrincrement setting missing\n");
		ret = -EINVAL;
		goto out;
	}
	switch (sym->ctrincrement) {
	case DEF_ALG_SYM_CTRINCREMENT_UNDEF:
		/* Do nothing */
		break;
	case DEF_ALG_SYM_CTRINCREMENT_INCREMENT:
		CKINT(json_object_object_add(entry, "incrementalCounter",
					     json_object_new_boolean(1)));
		break;
	case DEF_ALG_SYM_CTRINCREMENT_DECREMENT:
		CKINT(json_object_object_add(entry, "incrementalCounter",
					     json_object_new_boolean(0)));
		break;
	default:
		logger(LOGGER_WARN, LOGGER_C_ANY,
		       "Unknown CTR overflow definition\n");
		ret = -EINVAL;
		goto out;
		break;
	}

	return 0;

out:
	if (tmp)
		json_object_put(tmp);
	if (tmp_array)
		json_object_put(tmp_array);
	return ret;
}
