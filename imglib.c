#include "imglib.h"

image
alloc_img (unsigned int width, unsigned int height)
{
  image img;
  img = malloc (sizeof (image_t));
  img->buf = (pixel_t*)memalign (128, width * height * sizeof (pixel_t));
  img->width = width;
  img->height = height;
  return img;
}

void
free_img (image img)
{
  free (img->buf);
  free (img);
}

void
fill_img (image img, pixel_t pixel)
{
  unsigned int i, n;
  n = img->width * img->height;
  for (i = 0; i < n; ++i)
    {
      img->buf[i] = pixel;
    }
}

void
fill_img_incr (image img)
{
  unsigned int i, n;
  n = img->width * img->height;
  for (i = 0; i < n; ++i)
    {
      img->buf[i] = i % 255;
    }
}

image
read_ppm (FILE * pf)
{
  char buf[PPMREADBUFLEN], *t;
  image img;
  unsigned int w, h, d;
  int r;

  if (pf == NULL)
    return NULL;
  t = fgets (buf, PPMREADBUFLEN, pf);
  if ((t == NULL) || (strncmp (buf, "P5\n", 3) != 0))
    return NULL;
  do
    {				/* Px formats can have # comments after first line */
      t = fgets (buf, PPMREADBUFLEN, pf);
      if (t == NULL)
	return NULL;
    }
  while (strncmp (buf, "#", 1) == 0);
  r = sscanf (buf, "%u %u", &w, &h);
  if (r < 2)
    return NULL;
  // The program fails if the first byte of the image is equal to 32. because
  // the fscanf eats the space and the image is read with some bit less
  r = fscanf (pf, "%u\n", &d);
  if ((r < 1) || (d != 255))
    return NULL;
  img = alloc_img (w, h);
  if (img != NULL)
    {
      size_t rd = fread (img->buf, sizeof (pixel_t), w * h, pf);
      if (rd < w * h)
      {
    	  free_img (img);
    	  return NULL;
      }
      return img;
    }
  return NULL;
}

void
write_ppm (FILE * fd, image img)
{
  unsigned int n;
  (void) fprintf (fd, "P5\n%d %d\n255\n", img->width, img->height);
  n = img->width * img->height;
  (void) fwrite (img->buf, sizeof (pixel_t), n, fd);
  (void) fflush (fd);
}
