#ifndef _IMGLIB_0
#define _IMGLIB_0

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <math.h>
#include <malloc.h>
#include <sys/queue.h>
#include "tema4.h"

typedef unsigned char pixel_t;
#define MAX_IMAGE_NAME 100

typedef struct
{
  unsigned int width;
  unsigned int height;
  pixel_t *buf;
  char name[MAX_IMAGE_NAME];
} image_t;

typedef image_t *image;

image alloc_img (unsigned int width, unsigned int height);
void free_img (image);
void fill_img (image img, pixel_t pixel);
void fill_img_incr (image img);
image read_ppm (FILE * pf);
void write_ppm (FILE * fd, image img);

#define GET_PIXEL(IMG, X, Y) (IMG->buf[ ((Y) * IMG->width + (X)) ])
#define PPMREADBUFLEN 256
#endif
