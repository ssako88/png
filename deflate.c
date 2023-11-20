/**
 * @file deflate.c
 * @author ssako88 (https://github.com/ssako88)
 * @brief zlib のラッパー関数
 * @version 0.1
 * @date 2023-11-20
 *
 * @copyright Copyright (c) 2023 ssako88
 *
 */

#include "deflate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUFFER_SIZE 65536

typedef struct {
	char *p;
	size_t l;
}bufstr, *pbufstr;

static void bufstr_write(pbufstr, const char *, size_t);

void zl_deflate(const char *data, size_t length, char **ret, size_t *ret_len){
	z_stream z;
	char *buf;
	bufstr dat = {NULL, 0};
	int result;

	z.zalloc = Z_NULL;
	z.zfree = Z_NULL;
	z.opaque = Z_NULL;
	if (deflateInit(&z, Z_DEFAULT_COMPRESSION) != Z_OK) {
		fprintf(stderr, "deflateInit: %s\n", (const char *)(z.msg));
		return;
	}

	buf = (char *)malloc(BUFFER_SIZE);

	z.next_in = (Bytef *)data;
	z.avail_in = (uInt)length;
	z.next_out = (Bytef *)buf;
	z.avail_out = BUFFER_SIZE;

	do {
		result = deflate(&z, Z_FINISH);
		if (result == Z_STREAM_END) break;
		if (result != Z_OK) {
			printf("ERROR deflate: %s\n", (const char *)(z.msg));
			return;
		}
		if (z.avail_out == 0) {
			bufstr_write(&dat, buf, BUFFER_SIZE);
			z.next_out = (Bytef *)buf;
			z.avail_out = BUFFER_SIZE;
		}
	} while(1);
	bufstr_write(&dat, buf, BUFFER_SIZE - z.avail_out);

	if (deflateEnd(&z) != Z_OK)
		printf("ERROR deflateEnd: %s\n", (const char *)(z.msg));

	free(buf);

	*ret = dat.p;
	*ret_len = dat.l;
}

z_stream *zl_initInflate(char *out, size_t outlen){
	z_stream *z = (z_stream *)malloc(sizeof(z_stream));

	z->zalloc = Z_NULL;
	z->zfree = Z_NULL;
	z->opaque = Z_NULL;
	if (inflateInit(z) != Z_OK) {
		fprintf(stderr, "inflateInit: %s\n", (const char *)(z->msg));
		return NULL;
	}
	z->next_out = (Bytef *)out;
	z->avail_out = (uInt)outlen;
	return z;
}

int zl_inflatePart(z_stream *z, const char *in, size_t inlen){
	int result;

	z->next_in = (Bytef *)in;
	z->avail_in = (uInt)inlen;
	do {
		result = inflate(z, Z_NO_FLUSH);
		if (result == Z_STREAM_END)
			return 1;
		if (result != Z_OK) {
			printf("ERROR inflate: code=%d %s\n", result, (const char *)(z->msg));
			return 0;
		}
		if (z->avail_in == 0)
			return 1;
		if (z->avail_out == 0){
			printf("outbuf exhausted\n");
			return 0;
		}
	} while(1);
}

int zl_inflateFinish(z_stream *z){
	int result;

	do {
		result = inflate(z, Z_FINISH);
		if (result == Z_STREAM_END)
			return 1;
		if (result != Z_OK) {
			printf("ERROR inflate: %s\n", (const char *)(z->msg));
			return 0;
		}
		if (z->avail_out == 0){
			printf("outbuf exhausted\n");
			return 0;
		}
	} while(1);
}

void zl_inflateClean(z_stream *z){
	inflateEnd(z);
	free(z);
}

static void bufstr_write(pbufstr b, const char *data, size_t length){
	char *q;

	if (b->p == NULL) {
		b->p = malloc(length);
		b->l = length;
		memcpy(b->p, data, length);
	} else {
		q = malloc(b->l + length);
		memcpy(q, b->p, b->l);
		free(b->p);
		b->p = q;
		memcpy(q + b->l, data, length);
		b->l = b->l + length;
	}
}
