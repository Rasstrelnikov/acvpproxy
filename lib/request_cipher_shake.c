/* JSON request generator for SHAKE
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

int acvp_req_set_algo_shake(const struct def_algo_shake *shake,
			    struct json_object *entry)
{
	int ret;

	CKINT(acvp_req_cipher_to_string(entry, shake->algorithm,
				        ACVP_CIPHERTYPE_HASH, "algorithm"));
	CKINT(json_object_object_add(entry, "inBit",
				     json_object_new_boolean(shake->inbit)));
	CKINT(json_object_object_add(entry, "inEmpty",
				     json_object_new_boolean(shake->inempty));
	CKINT(acvp_req_algo_int_array(entry, shake->outlength,
				      "outLength")));
	CKINT(json_object_object_add(entry, "outBit",
				     json_object_new_boolean(shake->outbit)));

out:
	return ret;
}
