/*
  Filtered Image Resampling
  Original file by Dale Schumacher (found in Graphics Gems, III).
  Additional changes by Ray Gardener, Daylon Graphics Ltd.
  December 4, 1999
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <math.h>

#ifndef EXIT_SUCCESS
#define	EXIT_SUCCESS	(0)
#define	EXIT_FAILURE	(1)
#endif

/* M_PI was not in gems header ? ? */
#ifndef M_PI
#define M_PI	 3.14159265359
#endif
#ifndef TRUE
#define TRUE	1
#define FALSE	0
#endif
typedef unsigned char Pixel;


#define	WHITE_PIXEL	(255)
#define	BLACK_PIXEL	(0)
#define MaxRGB		WHITE_PIXEL
#define MinRGB		BLACK_PIXEL

#define CLAMP(val,a,b)	(((val)<(a))?(a):(((val)>(b))?(b):(val)))

/*
 *	filter function definitions
 */

#define	filter_support		(1.0)

static double
filter (double t)
{
  /* f(t) = 2|t|^3 - 3|t|^2 + 1, -1 <= t <= 1 */
  if (t < 0.0)
    t = -t;
  if (t < 1.0)
    return ((2.0 * t - 3.0) * t * t + 1.0);
  return (0.0);
}

#define	box_support		(0.5)

static double
box_filter (double t)
{
  if ((t > -0.5) && (t <= 0.5))
    return (1.0);
  return (0.0);
}

#define	triangle_support	(1.0)

static double
triangle_filter (double t)
{
  if (t < 0.0)
    t = -t;
  if (t < 1.0)
    return (1.0 - t);
  return (0.0);
}

#define	bell_support		(1.5)

static double
bell_filter (double t)
  /* box (*) box (*) box */
{
  if (t < 0)
    t = -t;
  if (t < .5)
    return (.75 - (t * t));
  if (t < 1.5)
    {
      t = (t - 1.5);
      return (.5 * (t * t));
    }
  return (0.0);
}

#define	B_spline_support	(2.0)

static double
B_spline_filter (double t)	/* box (*) box (*) box (*) box */
{
  double tt;

  if (t < 0)
    t = -t;
  if (t < 1)
    {
      tt = t * t;
      return ((.5 * tt * t) - tt + (2.0 / 3.0));
    }
  else if (t < 2)
    {
      t = 2 - t;
      return ((1.0 / 6.0) * (t * t * t));
    }
  return (0.0);
}



#define	Mitchell_support	(2.0)

static double
Mitchell_filter (double t)
{
  double tt;
  double B = (1.0 / 3.0);
  double C = (1.0 / 3.0);

  tt = t * t;
  if (t < 0)
    t = -t;
  if (t < 1.0)
    {
      t = (((12.0 - 9.0 * B - 6.0 * C) * (t * tt))
	   + ((-18.0 + 12.0 * B + 6.0 * C) * tt) + (6.0 - 2 * B));
      return (t / 6.0);
    }
  else if (t < 2.0)
    {
      t = (((-1.0 * B - 6.0 * C) * (t * tt))
	   + ((6.0 * B + 30.0 * C) * tt)
	   + ((-12.0 * B - 48.0 * C) * t) + (8.0 * B + 24 * C));
      return (t / 6.0);
    }
  return (0.0);
}


static double
sinc (const double x)
{
  if (x != 0)
    return (sin (x * M_PI) / (x * M_PI));
  return (1.0);
}

#define	Lanczos3_support	(3.0)

static double
Lanczos3_filter (const double arg)
{
  double t = arg;
  if (t < 0)
    t = -t;
  if (t < 3.0)
    return (sinc (t) * sinc (t / 3.0));
  return (0.0);
}


/*
 *	image rescaling routine
 */

typedef struct
{
  int pixel;
  double weight;
}
CONTRIB;

typedef struct
{
  int n;			/* number of contributors */
  CONTRIB *p;			/* pointer to list of contributions */
}
CLIST;

CLIST *contrib;			/* array of contribution lists */


/*
	roundcloser()

	Round an FP value to its closest int representation.
	General routine; ideally belongs in general math lib file.
*/
static int
roundcloser (double d)
{
  /* Untested potential one-liner, but smacks of call overhead */
  /* return fabs(ceil(d)-d) <= 0.5 ? ceil(d) : floor(d); */

  /* Untested potential optimized ceil() usage */
  /*    double cd = ceil(d);
     int ncd = (int)cd;
     if(fabs(cd - d) > 0.5)
     ncd--;
     return ncd;
   */

  /* Version that uses no function calls at all. */
  int n = (int) d;
  double diff = d - (double) n;
  if (diff < 0)
    diff = -diff;
  if (diff >= 0.5)
    {
      if (d < 0)
	n--;
      else
	n++;
    }
  return n;
}				/* roundcloser */


/* 
	calc_x_contrib()
	
	Calculates the filter weights for a single target column.
	contribX->p must be freed afterwards.

	Returns -1 if error, 0 otherwise.
*/
static int
calc_x_contrib (contribX, xscale, fwidth, dstwidth, srcwidth, filterf, i)
     CLIST *contribX;		/* Receiver of contrib info */
     double xscale;		/* Horizontal zooming scale */
     double fwidth;		/* Filter sampling width */
     int dstwidth;		/* Target bitmap width */
     int srcwidth;		/* Source bitmap width */
     double (*filterf) (double);	/* Filter proc */
     int i;			/* Pixel column in source bitmap being processed */
{
  double width;
  double fscale;
  double center, left, right;
  double weight;
  int j, k, n;

  if (xscale < 1.0)
    {
      /* Shrinking image */
      width = fwidth / xscale;
      fscale = 1.0 / xscale;

      contribX->n = 0;
      contribX->p = (CONTRIB *) calloc ((int) (width * 2 + 1),
					sizeof (CONTRIB));
      if (contribX->p == NULL)
	return -1;

      center = (double) i / xscale;
      left = ceil (center - width);
      right = floor (center + width);
      for (j = (int) left; j <= right; ++j)
	{
	  weight = center - (double) j;
	  weight = (*filterf) (weight / fscale) / fscale;
	  if (j < 0)
	    n = -j;
	  else if (j >= srcwidth)
	    n = (srcwidth - j) + srcwidth - 1;
	  else
	    n = j;

	  k = contribX->n++;
	  contribX->p[k].pixel = n;
	  contribX->p[k].weight = weight;
	}

    }
  else
    {
      /* Expanding image */
      contribX->n = 0;
      contribX->p = (CONTRIB *) calloc ((int) (fwidth * 2 + 1),
					sizeof (CONTRIB));
      if (contribX->p == NULL)
	return -1;
      center = (double) i / xscale;
      left = ceil (center - fwidth);
      right = floor (center + fwidth);

      for (j = (int) left; j <= right; ++j)
	{
	  weight = center - (double) j;
	  weight = (*filterf) (weight);
	  if (j < 0)
	    {
	      n = -j;
	    }
	  else if (j >= srcwidth)
	    {
	      n = (srcwidth - j) + srcwidth - 1;
	    }
	  else
	    {
	      n = j;
	    }
	  k = contribX->n++;
	  contribX->p[k].pixel = n;
	  contribX->p[k].weight = weight;
	}
    }
  return 0;
}				/* calc_x_contrib */


/*
	zoom()

	Resizes a one-component bitmap while resampling it.
	Returns -1 if error, 0 if success.
*/
void
zoom (unsigned char *src, int src_width, int src_height,
      unsigned char *dst, int dst_width, int dst_height)
{
  Pixel *tmp;
  double xscale, yscale;	/* zoom scale factors */
  int xx;
  int i, j, k;			/* loop variables */
  int n;			/* pixel number */
  double center, left, right;	/* filter calculation variables */
  double width, fscale, weight;	/* filter calculation variables */
  Pixel pel, pel2;
  int bPelDelta;
  CLIST *contribY;		/* array of contribution lists */
  CLIST contribX;

  double (*filterf) (double) = bell_filter;
  double fwidth = bell_support;

  /* create intermediate column to hold horizontal dst column zoom */
  tmp = (Pixel *) malloc (src_height * sizeof (Pixel));
  if (tmp == NULL)
    return;

  xscale = (double) dst_width / (double) src_width;

  /* Build y weights */
  /* pre-calculate filter contributions for a column */
  contribY = (CLIST *) calloc (dst_height, sizeof (CLIST));
  if (contribY == NULL)
    {
      free (tmp);
      return;
    }

  yscale = (double) dst_height / (double) src_height;

  if (yscale < 1.0)
    {
      width = fwidth / yscale;
      fscale = 1.0 / yscale;
      for (i = 0; i < dst_height; ++i)
	{
	  contribY[i].n = 0;
	  contribY[i].p = (CONTRIB *) calloc ((int) (width * 2 + 1),
					      sizeof (CONTRIB));
	  if (contribY[i].p == NULL)
	    {
	      free (tmp);
	      free (contribY);
	      return;
	    }
	  center = (double) i / yscale;
	  left = ceil (center - width);
	  right = floor (center + width);
	  for (j = (int) left; j <= right; ++j)
	    {
	      weight = center - (double) j;
	      weight = (*filterf) (weight / fscale) / fscale;
	      if (j < 0)
		{
		  n = -j;
		}
	      else if (j >= src_height)
		{
		  n = (src_height - j) + src_height - 1;
		}
	      else
		{
		  n = j;
		}
	      k = contribY[i].n++;
	      contribY[i].p[k].pixel = n;
	      contribY[i].p[k].weight = weight;
	    }
	}
    }
  else
    {
      for (i = 0; i < dst_height; ++i)
	{
	  contribY[i].n = 0;
	  contribY[i].p = (CONTRIB *) calloc ((int) (fwidth * 2 + 1),
					      sizeof (CONTRIB));
	  if (contribY[i].p == NULL)
	    {
	      free (tmp);
	      free (contribY);
	      return;
	    }
	  center = (double) i / yscale;
	  left = ceil (center - fwidth);
	  right = floor (center + fwidth);
	  for (j = (int) left; j <= right; ++j)
	    {
	      weight = center - (double) j;
	      weight = (*filterf) (weight);
	      if (j < 0)
		{
		  n = -j;
		}
	      else if (j >= src_height)
		{
		  n = (src_height - j) + src_height - 1;
		}
	      else
		{
		  n = j;
		}
	      k = contribY[i].n++;
	      contribY[i].p[k].pixel = n;
	      contribY[i].p[k].weight = weight;
	    }
	}
    }

  for (xx = 0; xx < dst_width; xx++)
    {
      if (0 != calc_x_contrib (&contribX, xscale, fwidth,
			       dst_width, src_width, filterf, xx))
	{
	  goto __zoom_cleanup;
	}
      /* Apply horz filter to make dst column in tmp. */
      for (k = 0; k < src_height; ++k)
	{
	  weight = 0.0;
	  bPelDelta = FALSE;
	  /*pel = get_pixel(src, contribX.p[0].pixel, k); */
	  pel = ((Pixel *) src)[(contribX.p[0].pixel + k * src_width)];
	  for (j = 0; j < contribX.n; ++j)
	    {
	      /* pel2 = get_pixel(src, contribX.p[j].pixel, k); */
	      pel2 = ((Pixel *) src)[(contribX.p[j].pixel + k * src_width)];
	      if (pel2 != pel)
		bPelDelta = TRUE;
	      weight += pel2 * contribX.p[j].weight;
	    }
	  weight = bPelDelta ? roundcloser (weight) : pel;

	  tmp[k] = (Pixel) CLAMP (weight, BLACK_PIXEL, WHITE_PIXEL);
	}			/* next row in temp column */

      free (contribX.p);

      /* The temp column has been built. Now stretch it 
         vertically into dst column. */
      for (i = 0; i < dst_height; ++i)
	{
	  weight = 0.0;
	  bPelDelta = FALSE;
	  pel = tmp[contribY[i].p[0].pixel];

	  for (j = 0; j < contribY[i].n; ++j)
	    {
	      pel2 = tmp[contribY[i].p[j].pixel];
	      if (pel2 != pel)
		bPelDelta = TRUE;
	      weight += pel2 * contribY[i].p[j].weight;
	    }
	  weight = bPelDelta ? roundcloser (weight) : pel;
	  ((Pixel *) dst)[(xx + i * dst_width)] =
	    (Pixel) CLAMP (weight, BLACK_PIXEL, WHITE_PIXEL);
	}			/* next dst row */
    }				/* next dst column */

__zoom_cleanup:
  free (tmp);

  /* free the memory allocated for vertical filter weights */
  for (i = 0; i < dst_height; ++i)
    free (contribY[i].p);
  free (contribY);

}				/* zoom */
