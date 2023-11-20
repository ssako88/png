/**
 * @file png.c
 * @author ssako88 (https://github.com/ssako88)
 * @brief PNG 入出力
 * @version 0.1
 * @date 2023-11-20
 *
 * @copyright Copyright (c) 2023 ssako88
 *
 */
#include "png.h"
#include "deflate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <zlib.h>

static unsigned int pngcrc(const char *, size_t);
static unsigned int pngcrc2(unsigned int, const char *, size_t);
static void init_table(unsigned int *);
static void write_pngheader(FILE *);
static int read_pngheader(FILE *f);
static void write_pngsection(FILE *o, const char *name, const char *data, size_t length);
static int read_pngsection(FILE *f, const char *name, char **data, size_t *length);

static uint8_t filterAverage(uint8_t left, uint8_t up);
static uint8_t paethPredictor(uint8_t left, uint8_t up, uint8_t upLeft);
static int pngTrueColorAlpha(char *imgbuf, char *buf, int w, int h);

static unsigned int pngcrc(const char *data, size_t length) {
	unsigned int c = 0xffffffff, table[256];
	size_t i;
	unsigned char *d;

	init_table(table);
	d = (unsigned char *)data;
	for (i = 0; i < length; i++)
		c = table[(c ^ d[i]) & 0xff] ^ (c >> 8);
	return c ^ 0xffffffff;
}

static unsigned int pngcrc2(unsigned int c, const char *data, size_t length){
	unsigned int table[256];
	size_t i;
	unsigned char *d;

	c ^= 0xffffffff;
	init_table(table);
	d = (unsigned char *)data;
	for (i = 0; i < length; i++)
		c = table[(c ^ d[i]) & 0xff] ^ (c >> 8);
	return c ^ 0xffffffff;
}

static void init_table(unsigned int *table){
	int i, j;
	unsigned int c;

	for (i = 0; i < 256; i++) {
		c = i;
		for (j = 0; j < 8; j++)
			c = (c & 1 ? 0xedb88320 : 0) ^ (c >> 1);
		table[i] = c;
	}
}

static void write_pngheader(FILE *o){
	const char dat[8] = {(char)0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
	fwrite(dat, 1, 8, o);
}

static int read_pngheader(FILE *f){
	uint8_t dat[8];
	if (fread(dat, 1, 8, f) < 8)
		return 0;
	return
		dat[0] == 0x89 &&
		dat[1] == 'P' &&
		dat[2] == 'N' &&
		dat[3] == 'G' &&
		dat[4] == '\r' &&
		dat[5] == '\n' &&
		dat[6] == 0x1A &&
		dat[7] == '\n';
}

static void write_pngsection(FILE *o, const char *name, const char *data, size_t length){
	unsigned int n;

	n = (unsigned int)htonl(length);
	fwrite(&n, sizeof(int), 1, o);
	fwrite(name, sizeof(char), strlen(name), o);
	if (length)
		fwrite(data, sizeof(char), length, o);
	n = pngcrc(name, strlen(name));
	n = pngcrc2(n, data, length);
	n = htonl(n);
	fwrite(&n, sizeof(int), 1, o);
}

static int read_pngsection(FILE *f, const char *name, char **data, size_t *length){
	uint32_t l, n;
	char s[8], *buf;

	*data = NULL;
	*length = 0;
	while (1){
		if (fread(&n, sizeof(uint32_t), 1, f) < 1)
			return 0;
		l = (uint32_t)ntohl(n);
		if (fread(s, sizeof(char), 4, f) < 4)
			return 0;
		s[4] = 0;
		if (strcmp(s, name) == 0)
			break;
		fseek(f, l+4, SEEK_CUR);
	}
	buf = (char *)malloc(l);
	if (!buf){
		printf("failed to allocate memory");
		return 0;
	}
	fread(buf, sizeof(char), l, f);
	fread(&n, sizeof(int), 1, f);
	*data = buf;
	*length = l;
	return 1;
}

static uint8_t filterAverage(uint8_t left, uint8_t up){
	uint16_t a = left;
	uint16_t b = up;
	return (uint8_t)((a + b) / 2);
}

// see https://www.w3.org/TR/2003/REC-PNG-20031110/#9Filter-type-4-Paeth
static uint8_t paethPredictor(uint8_t left, uint8_t up, uint8_t upLeft){
	uint16_t a = left;
	uint16_t b = up;
	uint16_t c = upLeft;
	int16_t p = a + b - c;
	int16_t pa = p < a ? a - p : p - a;
	int16_t pb = p < b ? b - p : p - b;
	int16_t pc = p < c ? c - p : p - c;
	if (pa <= pb && pa <= pc)
		return left;
	if (pb <= pc)
		return up;
	return upLeft;
}

int saveAsPNG(const char *fname, pngimage img){
	char info[16], *buf, *p;
	size_t l;
	unsigned int n;
	int j, w, h;
	FILE *f;

	f = fopen(fname, "wb");
	if (f == NULL){
		printf("failed to open file (%s): %s\n", fname, strerror(errno));
		return 1;
	}

	// header
	write_pngheader(f);

	// IHDR
	memset(info, 0, 16);
	w = img.w;
	h = img.h;
	n = htonl(w);
	memcpy(info, &n, 4);
	n = htonl(h);
	memcpy(info + 4, &n, 4);
	info[8] = 8; // 8bit color component
	info[9] = 6; // True-color + alpha (RGBA)
	write_pngsection(f, "IHDR", info, 13);

	// IDAT
	// Filter type is 0 (none) for all rows
	buf = (char *)malloc((w * 4 + 1) * h);
	memset(buf, 0, (w * 4 + 1) * h);
	for (j = 0; j < h; j++)
		memcpy(buf + (h-1-j) * (w * 4 + 1) + 1, (char *)img.data + j * w * 4, w * 4);
	zl_deflate(buf, (w*4+1)*h, &p, &l);
	free(buf);
	write_pngsection(f, "IDAT", p, l);
	free(p);

	// IEND
	write_pngsection(f, "IEND", NULL, 0);
	fclose(f);
	return 0;
}

static int pngTrueColorAlpha(char *imgbuf, char *buf, int w, int h){
	int i, j;
	char c;
	for (j = 0; j < h; j++){
		switch (buf[j * (w * 4 + 1)]) {
			case 0: // filter: none
				memcpy(imgbuf + (h-1-j) * w * 4, buf + j * (w * 4 + 1) + 1, w * 4);
				break;
			case 1: // filter: sub
				imgbuf[(h-1-j) * w * 4] = buf[j * (w * 4 + 1) + 1];
				imgbuf[(h-1-j) * w * 4 + 1] = buf[j * (w * 4 + 1) + 2];
				imgbuf[(h-1-j) * w * 4 + 2] = buf[j * (w * 4 + 1) + 3];
				imgbuf[(h-1-j) * w * 4 + 3] = buf[j * (w * 4 + 1) + 4];
				for (i = 1; i < w; i++){
					imgbuf[(h-1-j)*w*4+i*4+0] = buf[j * (w*4+1) + 1 + i*4] += buf[j * (w*4+1) - 3 + i*4];
					imgbuf[(h-1-j)*w*4+i*4+1] = buf[j * (w*4+1) + 2 + i*4] += buf[j * (w*4+1) - 2 + i*4];
					imgbuf[(h-1-j)*w*4+i*4+2] = buf[j * (w*4+1) + 3 + i*4] += buf[j * (w*4+1) - 1 + i*4];
					imgbuf[(h-1-j)*w*4+i*4+3] = buf[j * (w*4+1) + 4 + i*4] += buf[j * (w*4+1) - 0 + i*4];
				}
				break;
			case 2: // filter: up
				for (i = 0; i < w; i++){
					imgbuf[(h-1-j)*w*4+i*4+0] = buf[j * (w*4+1) + 1 + i*4] += buf[(j-1) * (w*4+1) + i*4 + 1];
					imgbuf[(h-1-j)*w*4+i*4+1] = buf[j * (w*4+1) + 2 + i*4] += buf[(j-1) * (w*4+1) + i*4 + 2];
					imgbuf[(h-1-j)*w*4+i*4+2] = buf[j * (w*4+1) + 3 + i*4] += buf[(j-1) * (w*4+1) + i*4 + 3];
					imgbuf[(h-1-j)*w*4+i*4+3] = buf[j * (w*4+1) + 4 + i*4] += buf[(j-1) * (w*4+1) + i*4 + 4];
				}
				break;
			case 3: // filter: average
				c = filterAverage(0, buf[(j-1) * (w*4+1) + 1]);
				imgbuf[(h-1-j)*w*4+0] = buf[j * (w*4+1) + 1] += c;
				c = filterAverage(0, buf[(j-1) * (w*4+1) + 2]);
				imgbuf[(h-1-j)*w*4+1] = buf[j * (w*4+1) + 2] += c;
				c = filterAverage(0, buf[(j-1) * (w*4+1) + 3]);
				imgbuf[(h-1-j)*w*4+2] = buf[j * (w*4+1) + 3] += c;
				c = filterAverage(0, buf[(j-1) * (w*4+1) + 4]);
				imgbuf[(h-1-j)*w*4+3] = buf[j * (w*4+1) + 4] += c;
				for (i = 1; i < w; i++){
					c = filterAverage(buf[(j-0) * (w*4+1) + (i-1)*4 + 1],
									  buf[(j-1) * (w*4+1) + (i-0)*4 + 1]);
					imgbuf[(h-1-j)*w*4+i*4+0] = buf[j * (w*4+1) + 1 + i*4] += c;
					c = filterAverage(buf[(j-0) * (w*4+1) + (i-1)*4 + 2],
									  buf[(j-1) * (w*4+1) + (i-0)*4 + 2]);
					imgbuf[(h-1-j)*w*4+i*4+1] = buf[j * (w*4+1) + 2 + i*4] += c;
					c = filterAverage(buf[(j-0) * (w*4+1) + (i-1)*4 + 3],
									  buf[(j-1) * (w*4+1) + (i-0)*4 + 3]);
					imgbuf[(h-1-j)*w*4+i*4+2] = buf[j * (w*4+1) + 3 + i*4] += c;
					c = filterAverage(buf[(j-0) * (w*4+1) + (i-1)*4 + 4],
									  buf[(j-1) * (w*4+1) + (i-0)*4 + 4]);
					imgbuf[(h-1-j)*w*4+i*4+3] = buf[j * (w*4+1) + 4 + i*4] += c;
				}
				break;
			case 4: // filter: paeth
				c = paethPredictor(0, buf[(j-1) * (w*4+1) + 1], 0);
				imgbuf[(h-1-j)*w*4+0] = buf[j * (w*4+1) + 1] += c;
				c = paethPredictor(0, buf[(j-1) * (w*4+1) + 2], 0);
				imgbuf[(h-1-j)*w*4+1] = buf[j * (w*4+1) + 2] += c;
				c = paethPredictor(0, buf[(j-1) * (w*4+1) + 3], 0);
				imgbuf[(h-1-j)*w*4+2] = buf[j * (w*4+1) + 3] += c;
				c = paethPredictor(0, buf[(j-1) * (w*4+1) + 4], 0);
				imgbuf[(h-1-j)*w*4+3] = buf[j * (w*4+1) + 4] += c;
				for (i = 1; i < w; i++){
					c = paethPredictor(buf[(j-0) * (w*4+1) + (i-1)*4 + 1],
									   buf[(j-1) * (w*4+1) + (i-0)*4 + 1],
									   buf[(j-1) * (w*4+1) + (i-1)*4 + 1]);
					imgbuf[(h-1-j)*w*4+i*4+0] = buf[j * (w*4+1) + 1 + i*4] += c;
					c = paethPredictor(buf[(j-0) * (w*4+1) + (i-1)*4 + 2],
									   buf[(j-1) * (w*4+1) + (i-0)*4 + 2],
									   buf[(j-1) * (w*4+1) + (i-1)*4 + 2]);
					imgbuf[(h-1-j)*w*4+i*4+1] = buf[j * (w*4+1) + 2 + i*4] += c;
					c = paethPredictor(buf[(j-0) * (w*4+1) + (i-1)*4 + 3],
									   buf[(j-1) * (w*4+1) + (i-0)*4 + 3],
									   buf[(j-1) * (w*4+1) + (i-1)*4 + 3]);
					imgbuf[(h-1-j)*w*4+i*4+2] = buf[j * (w*4+1) + 3 + i*4] += c;
					c = paethPredictor(buf[(j-0) * (w*4+1) + (i-1)*4 + 4],
									   buf[(j-1) * (w*4+1) + (i-0)*4 + 4],
									   buf[(j-1) * (w*4+1) + (i-1)*4 + 4]);
					imgbuf[(h-1-j)*w*4+i*4+3] = buf[j * (w*4+1) + 4 + i*4] += c;
				}
				break;
		}
	}
	return 0;
}

static int pngTrueColor(char *imgbuf, char *buf, int w, int h){
	int i, j;
	char c;
	for (j = 0; j < h; j++){
		switch (buf[j * (w * 3 + 1)]) {
			case 0: // filter: none
				for (i = 0; i < w; i++){
					imgbuf[(h-1-j)*w*4+i*4+0] = buf[j * (w*3+1) + 1 + i*3];
					imgbuf[(h-1-j)*w*4+i*4+1] = buf[j * (w*3+1) + 2 + i*3];
					imgbuf[(h-1-j)*w*4+i*4+2] = buf[j * (w*3+1) + 3 + i*3];
					imgbuf[(h-1-j)*w*4+i*4+3] = 0xff;
				}
				break;
			case 1: // filter: sub
				imgbuf[(h-1-j) * w * 4] = buf[j * (w * 3 + 1) + 1];
				imgbuf[(h-1-j) * w * 4 + 1] = buf[j * (w * 3 + 1) + 2];
				imgbuf[(h-1-j) * w * 4 + 2] = buf[j * (w * 3 + 1) + 3];
				imgbuf[(h-1-j) * w * 4 + 3] = 0xff;
				for (i = 1; i < w; i++){
					imgbuf[(h-1-j)*w*4+i*4+0] = buf[j * (w*3+1) + 1 + i*3] += buf[j * (w*3+1) + (i-1)*3 + 1];
					imgbuf[(h-1-j)*w*4+i*4+1] = buf[j * (w*3+1) + 2 + i*3] += buf[j * (w*3+1) + (i-1)*3 + 2];
					imgbuf[(h-1-j)*w*4+i*4+2] = buf[j * (w*3+1) + 3 + i*3] += buf[j * (w*3+1) + (i-1)*3 + 3];
					imgbuf[(h-1-j)*w*4+i*4+3] = 0xff;
				}
				break;
			case 2: // filter: up
				for (i = 0; i < w; i++){
					imgbuf[(h-1-j)*w*4+i*4+0] = buf[j * (w*3+1) + 1 + i*3] += buf[(j-1) * (w*3+1) + i*3 + 1];
					imgbuf[(h-1-j)*w*4+i*4+1] = buf[j * (w*3+1) + 2 + i*3] += buf[(j-1) * (w*3+1) + i*3 + 2];
					imgbuf[(h-1-j)*w*4+i*4+2] = buf[j * (w*3+1) + 3 + i*3] += buf[(j-1) * (w*3+1) + i*3 + 3];
					imgbuf[(h-1-j)*w*4+i*4+3] = 0xff;
				}
				break;
			case 3: // filter: average
				c = filterAverage(0, buf[(j-1) * (w*3+1) + 1]);
				imgbuf[(h-1-j)*w*4+0] = buf[j * (w*3+1) + 1] += c;
				c = filterAverage(0, buf[(j-1) * (w*3+1) + 2]);
				imgbuf[(h-1-j)*w*4+1] = buf[j * (w*3+1) + 2] += c;
				c = filterAverage(0, buf[(j-1) * (w*3+1) + 3]);
				imgbuf[(h-1-j)*w*4+2] = buf[j * (w*3+1) + 3] += c;
				imgbuf[(h-1-j)*w*4+3] = 0xff;
				for (i = 1; i < w; i++){
					c = filterAverage(buf[(j-0) * (w*3+1) + (i-1)*3 + 1],
									  buf[(j-1) * (w*3+1) + (i-0)*3 + 1]);
					imgbuf[(h-1-j)*w*4+i*4+0] = buf[j * (w*3+1) + 1 + i*3] += c;
					c = filterAverage(buf[(j-0) * (w*3+1) + (i-1)*3 + 2],
									  buf[(j-1) * (w*3+1) + (i-0)*3 + 2]);
					imgbuf[(h-1-j)*w*4+i*4+1] = buf[j * (w*3+1) + 2 + i*3] += c;
					c = filterAverage(buf[(j-0) * (w*3+1) + (i-1)*3 + 3],
									  buf[(j-1) * (w*3+1) + (i-0)*3 + 3]);
					imgbuf[(h-1-j)*w*4+i*4+2] = buf[j * (w*3+1) + 3 + i*3] += c;
					imgbuf[(h-1-j)*w*4+i*4+3] = 0xff;
				}
				break;
			case 4: // filter: paeth
				c = paethPredictor(0, buf[(j-1) * (w*3+1) + 1], 0);
				imgbuf[(h-1-j)*w*4+0] = buf[j * (w*3+1) + 1] += c;
				c = paethPredictor(0, buf[(j-1) * (w*3+1) + 2], 0);
				imgbuf[(h-1-j)*w*4+1] = buf[j * (w*3+1) + 2] += c;
				c = paethPredictor(0, buf[(j-1) * (w*3+1) + 3], 0);
				imgbuf[(h-1-j)*w*4+2] = buf[j * (w*3+1) + 3] += c;
				imgbuf[(h-1-j)*w*4+3] = 0xff;
				for (i = 1; i < w; i++){
					c = paethPredictor(buf[(j-0) * (w*3+1) + (i-1)*3 + 1],
									   buf[(j-1) * (w*3+1) + (i-0)*3 + 1],
									   buf[(j-1) * (w*3+1) + (i-1)*3 + 1]);
					imgbuf[(h-1-j)*w*4+i*4+0] = buf[j * (w*3+1) + 1 + i*3] += c;
					c = paethPredictor(buf[(j-0) * (w*3+1) + (i-1)*3 + 2],
									   buf[(j-1) * (w*3+1) + (i-0)*3 + 2],
									   buf[(j-1) * (w*3+1) + (i-1)*3 + 2]);
					imgbuf[(h-1-j)*w*4+i*4+1] = buf[j * (w*3+1) + 2 + i*3] += c;
					c = paethPredictor(buf[(j-0) * (w*3+1) + (i-1)*3 + 3],
									   buf[(j-1) * (w*3+1) + (i-0)*3 + 3],
									   buf[(j-1) * (w*3+1) + (i-1)*3 + 3]);
					imgbuf[(h-1-j)*w*4+i*4+2] = buf[j * (w*3+1) + 3 + i*3] += c;
					imgbuf[(h-1-j)*w*4+i*4+3] = 0xff;
				}
				break;
		}
	}
	return 0;
}

pngimage getFromPNG(const char *fname){
	pngimage img = {0, 0, NULL};
	char *info, *buf, *imgbuf, *p;
	z_stream *z;
	size_t l;
	unsigned int n;
	int w, h, bpp;
	FILE *f;
	int phase = 0;

	f = fopen(fname, "rb");
	if (f == NULL){
		printf("failed to open file (%s): %s\n", fname, strerror(errno));
		return img;
	}

	// header
	if (!read_pngheader(f))
		goto FAIL;
	phase = 1;

	// IHDR
	read_pngsection(f, "IHDR", &info, &l);
	if (!info || l < 13)
		goto FAIL;
	phase = 2;
	memcpy(&n, info, 4);
	w = ntohl(n);
	memcpy(&n, info + 4, 4);
	h = ntohl(n);
	if (info[8] != 8) // 8bit color component
		goto FAIL;
	phase = 3;
	switch (info[9]){
		case 6: // True-color + alpha (RGBA)
			bpp = 4;
			break;
		case 2: // True-color (RGB)
			bpp = 3;
			break;
		default:
			goto FAIL;
	}
	phase = 4;
	free(info);

	// IDAT
	buf = malloc((w * bpp + 1) * h);
	if (!buf)
		goto FAIL;
	phase = 5;
	z = zl_initInflate(buf, (w*bpp+1)*h);
	if (!z){
		free(buf);
		goto FAIL;
	}
	phase = 6;
	memset(buf, 0, (w * bpp + 1) * h);
	imgbuf = malloc(w * h * 4);
	if (!imgbuf)
		goto FAIL2;
	phase = 7;
	memset(imgbuf, 0, w * h * 4);
	while (read_pngsection(f, "IDAT", &p, &l)){
		if (!zl_inflatePart(z, p, l)){
			free(imgbuf);
			goto FAIL2;
		}
		free(p);
	}
	phase = 8;
	if (!zl_inflateFinish(z)){
		free(imgbuf);
		goto FAIL2;
	}
	if (bpp == 4)
		pngTrueColorAlpha(imgbuf, buf, w, h);
	else if (bpp == 3)
		pngTrueColor(imgbuf, buf, w, h);
	img.w = w;
	img.h = h;
	img.data = imgbuf;

	goto CLEAN2;

FAIL:
	printf("failed at phase: %d\n", phase);
	goto CLEAN;

FAIL2:
	printf("failed at phase: %d\n", phase);
	goto CLEAN2;

CLEAN2:
	zl_inflateClean(z);
	free(buf);

CLEAN:
	fclose(f);
	return img;
}
