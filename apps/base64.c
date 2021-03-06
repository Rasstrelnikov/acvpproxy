/* Base64 encoder and decoder
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
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

#include "base64.h"
#include "../lib/constructor.h"

static const char encoding_table[] = {
	'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
	'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
	'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
	'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
	'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
	'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
	'w', 'x', 'y', 'z', '0', '1', '2', '3',
	'4', '5', '6', '7', '8', '9', '+', '/'};

/* Filename and URL safe */
static const char encoding_table_safe[] = {
	'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
	'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
	'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
	'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
	'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
	'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
	'w', 'x', 'y', 'z', '0', '1', '2', '3',
	'4', '5', '6', '7', '8', '9', '-', '_'};

static char decoding_table[256];
static char decoding_table_safe[256];

static int __base64_encode(const uint8_t *idata, uint32_t ilen,
			   char **odata, uint32_t *olen, const char table[])
{
	uint32_t elen, i, j;
	unsigned int mod_table[] = {0, 2, 1};
	char *encoded;

	if (ilen > (UINT_MAX / 2))
		return -EINVAL;

	if (!ilen)
		return 0;

	elen = 4 * ((ilen + 2) / 3);
	encoded = malloc(elen);
	if (!encoded)
		return -ENOMEM;

	for (i = 0, j = 0; i < ilen; ) {
		uint32_t octet_a = i < ilen ? idata[i++] : 0;
		uint32_t octet_b = i < ilen ? idata[i++] : 0;
		uint32_t octet_c = i < ilen ? idata[i++] : 0;
		uint32_t triple = (octet_a << 0x10) +
				  (octet_b << 0x08) +
				  octet_c;

		encoded[j++] = table[(triple >> 3 * 6) & 0x3F];
		encoded[j++] = table[(triple >> 2 * 6) & 0x3F];
		encoded[j++] = table[(triple >> 1 * 6) & 0x3F];
		encoded[j++] = table[(triple >> 0 * 6) & 0x3F];
	}

	for (i = 0; i < mod_table[ilen % 3]; i++)
		encoded[elen - 1 - i] = '=';

	*odata = encoded;
	*olen = elen;

	return 0;
}

int base64_encode(const uint8_t *idata, uint32_t ilen,
		  char **odata, uint32_t *olen)
{
	return __base64_encode(idata, ilen, odata, olen, encoding_table);
}

int base64_encode_safe(const uint8_t *idata, uint32_t ilen,
		       char **odata, uint32_t *olen)
{
	return __base64_encode(idata, ilen, odata, olen, encoding_table_safe);
}

int __base64_decode(const char *idata, uint32_t ilen,
		    uint8_t **odata, uint32_t *olen, const char table[])
{
	uint32_t dlen, i, j;
	uint8_t *decoded;

	if (ilen % 4 != 0)
		return -EINVAL;

	if (!ilen)
		return 0;

	dlen = ilen / 4 * 3;

	if (idata[ilen - 1] == '=')
		dlen--;
	if (idata[ilen - 2] == '=')
		dlen--;

	decoded = malloc(dlen);
	if (!decoded)
		return -ENOMEM;

	for (i = 0, j = 0; i < ilen;) {
		uint32_t sextet_a = idata[i] == '=' ?
			0 & i++ :
			(uint32_t)table[(unsigned char)idata[i++]];
		uint32_t sextet_b = idata[i] == '=' ?
			0 & i++ :
			(uint32_t)table[(unsigned char)idata[i++]];
		uint32_t sextet_c = idata[i] == '=' ?
			0 & i++ :
			(uint32_t)table[(unsigned char)idata[i++]];
		uint32_t sextet_d = idata[i] == '=' ?
			0 & i++ :
			(uint32_t)table[(unsigned char)idata[i++]];
		uint32_t triple = (sextet_a << 3 * 6)
				  + (sextet_b << 2 * 6)
				  + (sextet_c << 1 * 6)
				  + (sextet_d << 0 * 6);

		if (j < dlen)
			decoded[j++] = (triple >> 2 * 8) & 0xFF;
		if (j < dlen)
			decoded[j++] = (triple >> 1 * 8) & 0xFF;
		if (j < dlen)
			decoded[j++] = (triple >> 0 * 8) & 0xFF;
	}

	*odata = decoded;
	*olen = dlen;

	return 0;
}

int base64_decode(const char *idata, uint32_t ilen,
		  uint8_t **odata, uint32_t *olen)
{
	return __base64_decode(idata, ilen, odata, olen, decoding_table);
}

int base64_decode_safe(const char *idata, uint32_t ilen,
		       uint8_t **odata, uint32_t *olen)
{
	return __base64_decode(idata, ilen, odata, olen, decoding_table_safe);
}

ACVP_DEFINE_CONSTRUCTOR(base64_init)
static void base64_init(void)
{
	unsigned int i;

	for (i = 0; i < 64; i++)
		decoding_table[(unsigned char)encoding_table[i]] = i;
	for (i = 0; i < 64; i++)
		decoding_table_safe[(unsigned char)encoding_table_safe[i]] = i;
}

#if 0
#include <stdio.h>
#include <string.h>

struct test_vector {
	uint8_t *bin;
	uint32_t binlen;
	char *encoded;
	uint32_t elen;
};

/* Test vectors from RFC4648 chapter 10 */
struct test_vector vectors[] = {
	{ (uint8_t *)"",	0, "", 0 },
	{ (uint8_t *)"f",	1, "Zg==", 4 },
	{ (uint8_t *)"fo",	2, "Zm8=", 4 },
	{ (uint8_t *)"foo",	3, "Zm9v", 4 },
	{ (uint8_t *)"foob",	4, "Zm9vYg==", 8 },
	{ (uint8_t *)"fooba",	5, "Zm9vYmE=", 8 },
	{ (uint8_t *)"foobar",	6, "Zm9vYmFy", 8 },
};

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

int main(int argc, char *argv[])
{
	uint8_t *bresult;
	char *result;
	uint32_t rlen;
	unsigned int i;

	(void)argc;
	(void)argv;

	for (i = 0; i < ARRAY_SIZE(vectors); i++) {
		int ret = base64_encode(vectors[i].bin, vectors[i].binlen,
					&result, &rlen);

		if (ret)
			return ret;

		if (rlen != vectors[i].elen ||
		    strncmp(result, vectors[i].encoded, vectors[i].elen)) {
			printf("encoding: test %u failed (expected %s, received %s)\n",
			       i, vectors[i].encoded, result);
		}

		if (rlen)
			free(result);

		ret = base64_encode_safe(vectors[i].bin, vectors[i].binlen,
					 &result, &rlen);

		if (ret)
			return ret;

		if (rlen != vectors[i].elen ||
		    strncmp(result, vectors[i].encoded, vectors[i].elen)) {
			printf("encoding: test %u failed (expected %s, received %s)\n",
			       i, vectors[i].encoded, result);
		}

		if (rlen)
			free(result);

		ret = base64_decode(vectors[i].encoded, vectors[i].elen,
				    &bresult, &rlen);
		if (ret)
			return ret;

		if (rlen != vectors[i].binlen ||
		    memcmp(bresult, vectors[i].bin, vectors[i].binlen)) {
			printf("decoding: test %u failed (expected %s, received %s)\n",
			       i, vectors[i].bin, bresult);
		}

		if (rlen)
			free(bresult);

		ret = base64_decode_safe(vectors[i].encoded, vectors[i].elen,
					 &bresult, &rlen);
		if (ret)
			return ret;

		if (rlen != vectors[i].binlen ||
		    memcmp(bresult, vectors[i].bin, vectors[i].binlen)) {
			printf("decoding: test %u failed (expected %s, received %s)\n",
			       i, vectors[i].bin, bresult);
		}

		if (rlen)
			free(bresult);
	}

	return 0;
}
#endif
