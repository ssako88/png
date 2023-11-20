/**
 * @file deflate.h
 * @author ssako88 (https://github.com/ssako88)
 * @brief zlib のラッパー関数
 * @version 0.1
 * @date 2023-11-20
 *
 * @copyright Copyright (c) 2023 ssako88
 *
 */
#ifndef deflate_h
#define deflate_h

#include <zlib.h>

void zl_deflate(const char *data, size_t length, char **ret, size_t *ret_len);
z_stream *zl_initInflate(char *out, size_t outlen);
int zl_inflatePart(z_stream *z, const char *in, size_t inlen);
int zl_inflateFinish(z_stream *z);
void zl_inflateClean(z_stream *z);

#endif /* deflate_h */
