/**
 * @file png.h
 * @author ssako88 (https://github.com/ssako88)
 * @brief PNG 入出力
 * @version 0.1
 * @date 2023-11-20
 *
 * @copyright Copyright (c) 2023 ssako88
 *
 */
#ifndef png_h
#define png_h

typedef struct{
	int w;
	int h;
	void *data;
} pngimage;

int saveAsPNG(const char *fname, pngimage img);
pngimage getFromPNG(const char *fname);

#endif /* png_h */
