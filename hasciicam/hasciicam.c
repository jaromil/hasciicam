/*  HasciiCam 0.9
 *  (c) 2000-2001 Denis Roio aka jaromil
 *
 * This source code is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Public License as published 
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * This source code is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * Please refer to the GNU Public License for more details.
 *
 * You should have received a copy of the GNU Public License along with
 * this source code; if not, write to:
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * CONTRIBUTORS :
 * Diego Torres aka rapid <rapid@ivworlds.org> 
 *  jpgdump, security issues, 24rgb->greyscale conversion, bugfixes
 * Alessandro Preite Martinez <vsg@arcetri.astro.it>
 *  SGI IRIX support, image resampling routines
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <pwd.h>
#include <signal.h>
#include "config.h"

#ifdef HAVE_LINUX
#include <linux/types.h>
#include <linux/videodev.h>
#endif

#ifdef HAVE_SGI
#include <dmedia/vl.h>
#endif

#include <jpeglib.h>
#include <aalib.h>

#include "ftp.h"
#include "yuv2rgb.h"

#define TEMPFILE "/tmp/hasciitmp.jpg"

/* mmapped buffer */
unsigned char *image = NULL;

/* declare the sighandler */
void quitproc (int Sig);

/* line command stuff */

char *version =
  "%s %s - (h)ascii 4 the masses!\n"
  "(c)2000-2001 denis roio aka jaromil <jaromil@dyne.org>\n"
  "[ http://ascii.dyne.org ]\n\n";

char *help =
/* "\x1B" "c" <--- SCREEN CLEANING ESCAPE CODE
   why here? just a reminder */
"Usage: hasciicam [options]\n"
"options:\n"
"-h --help         this help\n"
"-v --version      version information\n"
"-q --quiet        be quiet\n"
"-m --mode         mode: live|html|text      - default live\n"
"-d --device       video grabbing device     - default /dev/video\n"
"-i --input        input channel number      - default 1\n"
"-n --norm         norm: pal|ntsc|secam|auto - default auto\n"
"-s --size         ascii image size WxH      - default 96x72\n"
"-o --aafile       dumped file               - default hasciicam.[txt|html]\n"
"-f --ftp          ie: user@ftp.host.it:/dir - default none\n"
"-D --daemon       run in background         - default foregrond\n"
"-U --uid          setuid (int)              - default current\n"
"-G --gid          setgid (int)              - default current\n"
"rendering options:\n"
"-S --font-size    html font size (1-4)      - default 1\n"
"-a --font-face    html font to use          - default courier\n"
"-r --refresh      refresh delay             - default 2\n"
"-b --aabright     ascii brightness          - default 50\n"
"-c --aacontrast   ascii contrast            - default 10\n"
"-g --aagamma      ascii gamma               - default 10\n"
"-I --invert       invert colors             - default off\n"
"-B --background   background color (hex)    - default 000000\n"
"-F --foreground   foreground color (hex)    - default 00FF00\n"
"-O --jpgfile      jpeg file                 - default none\n"
"-Q --jpgqual      jpeg rendering quality    - default 65\n"
"\n";

#ifndef sgi
static const struct option long_options[] = {
  {"help", no_argument, NULL, 'h'},
  {"version", no_argument, NULL, 'v'},
  {"quiet", no_argument, NULL, 'q'},
  {"mode", required_argument, NULL, 'm'},
  {"device", required_argument, NULL, 'd'},
  {"input", required_argument, NULL, 'i'},
  {"norm", required_argument, NULL, 'n'},
  {"size", required_argument, NULL, 's'},
  {"aafile", required_argument, NULL, 'o'},
  {"ftp", required_argument, NULL, 'f'},
  {"daemon", no_argument, NULL, 'D'},
  {"font-size", required_argument, NULL, 'S'},
  {"font-face", required_argument, NULL, 'a'},
  {"refresh", required_argument, NULL, 'r'},
  {"aabright", required_argument, NULL, 'b'},
  {"aacontrast", required_argument, NULL, 'c'},
  {"aagamma", required_argument, NULL, 'g'},
  {"invert", no_argument, NULL, 'I'},
  {"background", required_argument, NULL, 'B'},
  {"foreground", required_argument, NULL, 'F'},
  {"jpgfile", required_argument, NULL, 'O'},
  {"jpgqual", required_argument, NULL, 'Q'},
  {"uid", required_argument, NULL, 'U'},
  {"gid", required_argument, NULL, 'G'},
  //  { "bttvbright", required_argument, NULL, 'B' },
  //  { "bttvcontrast", required_argument, NULL, 'C' },
  //  { "bttvgamma", required_argument, NULL, 'G' },
  {0, 0, 0, 0}
};
#endif

char *short_options = "hvqm:d:i:n:s:f:DS:a:r:o:b:c:g:IB:F:O:Q:U:G:";

/* default configuration */

volatile sig_atomic_t userbreak;

int quiet = 0;
int mode = 0;
int useftp = 0;
int input = 1;
int jpgdump = 0;
int daemon_mode = 0;

#ifndef HAVE_SGI
int norm = VIDEO_MODE_AUTO;
#endif

int width = 96;
int height = 72;

int refresh = 2;
int fontsize = 1;
char *device = "/dev/video";
char *aafile;
char *jpgfile;

char *fontface = "courier";
char *ftp;
char *ftp_user;
char *ftp_host;
char *ftp_dir;
char *ftp_pass;

int jpgqual = 65;
int uid = -1;
int gid = -1;

int aabright = 50;
int aacontrast = 10;
int aagamma = 10;
int invert = 0;
char *background = "000000";
char *foreground = "00FF00";


/* if width&height have been manually changed */
int whchanged = 0;

/* if the device has a tuner */
int have_tuner = 0;

/* ascii context & html formatting stuff*/
aa_context *ascii_img;

char html_header[512];

static char *html_escapes[] =
  { "<", "&lt;", ">", "&gt;", "&", "&amp;", NULL };

struct aa_format aa_html_format = {
  79, 25,
  0, 0,
  0,
  AA_NORMAL_MASK | AA_BOLD_MASK | AA_BOLDFONT_MASK,
  NULL,
  "Pure html",
  ".html",
  html_header,
  "</PRE>\n</FONT>\n</BODY>\n</HTML>\n",
  "\n",
  /*The order is:normal, dim, bold, boldfont, reverse, special */
  {"%s", "%s", "%s", "%s", "%s",},
  {"", "", "<B>", "", "<B>"},
  {"", "", "</B>", "", "</B>"},
  html_escapes
};

/* jpeg */

void
swap_rgb24 (char *mem, int n)
{
  char c;
  char *p = mem;
  int i = n;

  while (--i)
    {
      c = p[0];
      p[0] = p[2];
      p[2] = c;
      p += 3;
    }
}

void
resample_8bit (unsigned char *mem, int n, unsigned char *dest)
{
  int c;
  int cc = 0;
  for (c = 0; c < n; c += 4)
    {
      dest[cc] =
	(int) rint ((mem[c]) * 0.3 + (mem[c + 1]) * 0.59 +
		    (mem[c + 2]) * 0.11);
      cc++;
    }
}

int
write_jpeg (char *filename, char *data, int w, int h)
{
  struct jpeg_compress_struct cinfo;
  struct jpeg_error_mgr jerr;
  FILE *fp;
  char *dest;

  int i;
  unsigned char *line;

  if (NULL == (fp = fopen (TEMPFILE, "w")))
    {
      fprintf (stderr, "can't open %s for writing: %s\n",
	       TEMPFILE, strerror (errno));
      return -1;
    }

  cinfo.err = jpeg_std_error (&jerr);
  jpeg_create_compress (&cinfo);
  jpeg_stdio_dest (&cinfo, fp);
  cinfo.image_width = w;
  cinfo.image_height = h;
  cinfo.input_components = 3;
  cinfo.in_color_space = JCS_RGB;
  jpeg_set_defaults (&cinfo);
  jpeg_set_quality (&cinfo, jpgqual, TRUE);
  jpeg_start_compress (&cinfo, TRUE);

  for (i = 0, line = data; i < h; i++, line += w * 3)
    jpeg_write_scanlines (&cinfo, &line, 1);

  jpeg_finish_compress (&(cinfo));
  jpeg_destroy_compress (&(cinfo));
  fclose (fp);

  // 3 for the "mv " + source file + 1 for " " + dest file + NULL
  dest =
    (char *) calloc (3 + strlen (TEMPFILE) + 1 + strlen (filename) + 1,
		     sizeof (char));
  strcpy (dest, "mv ");
  strcat (dest, TEMPFILE);
  strcat (dest, " ");
  strcat (dest, filename);
  system (dest);
  free (dest);
  return 0;
}

#ifdef HAVE_LINUX
/* v4l */

static struct video_capability grab_cap;
static struct video_mbuf grab_map;
static struct video_mmap grab_buf[2];
static struct video_channel grab_chan;
static struct video_tuner grab_tuner;
static struct video_picture grab_pic;
static int grab_fd = -1;
static int grab_size;
static int cur_frame = 0;
static int ok_frame = 0;
static int palette;
static unsigned char *grab_data;

/* yuv2rgb conversion routine pointer 
   this is returned by yuv2rgb_init */
yuv2rgb_fun *yuv2rgb;
void *rgb_surface;
int u,v; /* uv offset */

void grab_detect(char *devfile) {
  int counter, res;
  char *capabilities[] = {
    "VID_TYPE_CAPTURE          can capture to memory",
    "VID_TYPE_TUNER            has a tuner of some form",
    "VID_TYPE_TELETEXT         has teletext capability",
    "VID_TYPE_OVERLAY          can overlay its image to video",
    "VID_TYPE_CHROMAKEY        overlay is chromakeyed",
    "VID_TYPE_CLIPPING         overlay clipping supported",
    "VID_TYPE_FRAMERAM         overlay overwrites video memory",
    "VID_TYPE_SCALES           supports image scaling",
    "VID_TYPE_MONOCHROME       image capture is grey scale only",
    "VID_TYPE_SUBCAPTURE       capture can be of only part of the image"
  };

  if (-1 == (grab_fd = open(devfile,O_RDWR|O_NONBLOCK))) {
    perror("!! error in opening video capture device: ");
    return;
  } else {
    close(grab_fd);
    grab_fd = open(devfile,O_RDWR);
  }
  
  res = ioctl(grab_fd,VIDIOCGCAP,&grab_cap);
  if(res<0) {
    perror("!! error in VIDIOCGCAP: ");
    return;
  }

  fprintf(stderr,"Device detected is %s\n",devfile);
  fprintf(stderr,"%s\n",grab_cap.name);
  fprintf(stderr,"%u channels detected\n",grab_cap.channels);
  fprintf(stderr,"max size w[%u] h[%u] - min size w[%u] h[%u]\n",grab_cap.maxwidth,grab_cap.maxheight,grab_cap.minwidth,grab_cap.minheight);
  fprintf(stderr,"Video capabilities:\n");
  for (counter=0;counter<11;counter++)
    if (grab_cap.type & (1 << counter)) fprintf(stderr,"%s\n",capabilities[counter]);
  
  if (-1 == ioctl(grab_fd, VIDIOCGPICT, &grab_pic)) {
    perror("!! ioctl VIDIOCGPICT: ");
    exit(1);
  }
  
  if (grab_pic.palette & VIDEO_PALETTE_GREY)
    fprintf(stderr,"VIDEO_PALETTE_GREY        device is able to grab greyscale frames\n");

  
  if (strncmp (grab_cap.name, "OV511", 5) == 0)
    /* the device is a USB camera, chipset OV511, wich has only one input
       channel (afaik, alltough i don't have one) so we force that chan */
    input = 0;
  
  if(grab_cap.type & VID_TYPE_TUNER)
    /* if the device does'nt has any tuner, so we avoid some ioctl
       this should be a fix for many webcams, thanks to Ben Wilson */
    have_tuner = 1;
  
  


  /* set and check the minwidth and minheight */
  if (whchanged == 0) {
    if (((grab_cap.minwidth / 2) > width)
	|| ((grab_cap.minheight / 2) > height)) {
      /* remember width & height are the ASCII size */
      width = grab_cap.minwidth / 2;
      height = grab_cap.minheight / 2;
    }
  }

  if (ioctl (grab_fd, VIDIOCGMBUF, &grab_map) == -1) {
    perror("!! error in ioctl VIDIOCGMBUF: ");
    return;
  }
  /* print memory info */
  fprintf(stderr,"memory map of %i frames: %i bytes\n",grab_map.frames,grab_map.size);
  for(counter=0;counter<grab_map.frames;counter++)
    fprintf(stderr,"Offset of frame %i: %i\n",counter,grab_map.offsets[counter]);

}


void grab_init() {

  int linespace = 5;
  int i;

  grab_detect (device);  

  if (grab_fd == -1) {
    perror ("Error in opening video capture device");
    exit (1);
  }
  
  if(have_tuner) { /* does this only if the device has a tuner */
    //   _band = 5; /* default band is europe west */
    //   _freq = 0;
    /* resets CHAN */
    if (-1 == ioctl(grab_fd,VIDIOCGCHAN,&grab_chan))
      fprintf(stderr,"!! error in ioctl VIDIOCGCHAN: %s",strerror(errno));

    if (-1 == ioctl(grab_fd,VIDIOCSCHAN,&grab_chan))
      fprintf(stderr,"error in ioctl VIDIOCSCHAN: %s",strerror(errno));
    
    /* get/set TUNER settings */
    if (-1 == ioctl(grab_fd,VIDIOCGTUNER,&grab_tuner))
      fprintf(stderr,"error in ioctl VIDIOCGTUNER: %s",strerror(errno));
  }

  /* set image source and TV norm */
  if (grab_cap.channels >= input)
    grab_chan.channel = input;
  else
    grab_chan.channel = 0;

  palette = VIDEO_PALETTE_YUV422P;
  for(i=0; i<grab_map.frames; i++) {
    grab_buf[i].format = palette; //RGB24;
    grab_buf[i].frame  = i;
    grab_buf[i].height = height*2;
    grab_buf[i].width = width*2;
  }


  grab_size = grab_buf[0].width * grab_buf[0].height * 4;

  /* choose best yuv2rgb routine (detecting cpu)
     supported: C, ASM-MMX, ASM-MMX+SSE */
  yuv2rgb = yuv2rgb_init(32,0x1); /* arg2 is MODE_RGB */
  rgb_surface = malloc(grab_size);
  if(!rgb_surface) {
    perror("can't allocate buffer for YUV conversion: ");
    exit(1);
  }

  u = (grab_buf[0].width*grab_buf[0].height);
  v = u+(u/2);

  grab_data = mmap (0, grab_map.size, PROT_READ | PROT_WRITE, MAP_SHARED, grab_fd, 0);
  if (MAP_FAILED == grab_data) {
    perror ("Cannot allocate video4linux grabber buffer ");
    exit (1);
  }

  /* feed up the mmapped frames */
  cur_frame = ok_frame = 0;  
  for(;cur_frame<grab_map.frames;cur_frame++) {
    if (-1 == ioctl(grab_fd,VIDIOCMCAPTURE,&grab_buf[cur_frame])) {
      fprintf(stderr,"error in ioctl VIDIOCMCAPTURE: %s",strerror(errno));
    }
  }
  cur_frame = 0;


  /* untested with new code restructuration */
  switch (fontsize) {
  case 1:
    linespace = 5;
    break;
  case 2:
    linespace = 10;
    break;
  case 3:
    linespace = 11;
    break;
  case 4:
    linespace = 13;
    break;
  }

  /* init the html header */
  snprintf (&html_header[0], 512,
	    "<HTML>\n <HEAD> <TITLE>wow! (h)ascii 4 the masses!</TITLE>\n<META HTTP-EQUIV=\"refresh\" CONTENT=\"%u\"; url=\"%s\">\n<META HTTP-EQUIV=\"Pragma\" CONTENT=\"no-cache\">\n<STYLE TYPE=\"text/css\">\n<!--\npre {\nletter-spacing: 1px;\nlayer-background-color: Black;\nleft : auto;\nline-height : %upx;\n}\n-->\n</STYLE>\n</HEAD>\n<BODY bgcolor=\"#%s\" text=\"#%s\">\n<FONT SIZE=%u face=\"%s\">\n<PRE>\n",
	    refresh, aafile, linespace, background, foreground, fontsize,
	    fontface);

}

int
grab_one () {

  ok_frame = cur_frame;
  cur_frame = ((cur_frame+1)%grab_map.frames);
  grab_buf[0].format = palette;
  
  (*yuv2rgb)((uint8_t *) rgb_surface,
	     (uint8_t *) &grab_data[grab_map.offsets[ok_frame]],
	     (uint8_t *) &grab_data[grab_map.offsets[ok_frame]+u],
	     (uint8_t *) &grab_data[grab_map.offsets[ok_frame]+v],
	     grab_buf[ok_frame].width, grab_buf[ok_frame].height, grab_buf[ok_frame].width*4,
	     grab_buf[ok_frame].width, grab_buf[ok_frame].width);

  if (-1 == ioctl(grab_fd,VIDIOCSYNC,&grab_buf[cur_frame])) {
    perror("error in ioctl VIDIOCSYNC: ");
    return -1;
  }

  if (-1 == ioctl(grab_fd,VIDIOCMCAPTURE,&grab_buf[cur_frame])) {
    perror("error in ioctl VIDIOCMCAPTURE: ");
    return -1;
  }
  return 1;

}





#endif





#ifdef sgi
/* Use IRIX DMedia layer */

#define VIDEO_PALETTE_RGB24 0

VLServer svr;
VLPath path;
VLNode src, drn;
VLBuffer buffer;
VLControlValue val;
VLInfoPtr info;

struct grab_buf_s
{
  int width, height;
}
grab_buf;

void
strip_alpha (char *ptr, int count)
{
  char *out = ptr;
  while (count--)
    {
      ++ptr;
      *out++ = *ptr++;
      *out++ = *ptr++;
      *out++ = *ptr++;
    }
}

unsigned char *
grab_init ()
{
  int linespace = 5;

  if (!(svr = vlOpenVideo ("")))
    {
      vlPerror ("hasciicam");
      exit (1);
    }

  /* set up a drain node in memory and a source node */
  drn = vlGetNode (svr, VL_DRN, VL_MEM, VL_ANY);
  src = vlGetNode (svr, VL_SRC, VL_VIDEO, VL_ANY);

  /* create a path using the first supporting device */
  path = vlCreatePath (svr, VL_ANY, src, drn);

  /* setup the hardware for the path */
  if ((vlSetupPaths (svr, (VLPathList) & path, 1, VL_SHARE, VL_SHARE)) < 0)
    {
      vlPerror ("vlSetupPaths");
      exit (2);
    }

  /* why even if I specify RGB packing the data gets
     out in RGBA ? */
  val.intVal = VL_PACKING_RGB_8;
  vlSetControl (svr, path, drn, VL_PACKING, &val);

  /* create and register a 1 frame buffer */
  buffer = vlCreateBuffer (svr, path, drn, 1);
  if (buffer == NULL)
    {
      vlPerror ("vlCreateBuffer");
      exit (1);
    }
  vlRegisterBuffer (svr, path, drn, buffer);

  vlGetControl (svr, path, drn, VL_SIZE, &val);
  grab_buf.width = val.xyVal.x;
  grab_buf.height = val.xyVal.y;

  switch (fontsize)
    {
    case 1:
      linespace = 5;
      break;
    case 2:
      linespace = 10;
      break;
    case 3:
      linespace = 11;
      break;
    case 4:
      linespace = 13;
      break;
    }

  /* init the html header */
  snprintf (&html_header[0], 512,
	    "<HTML>\n <HEAD> <TITLE>wow! (h)ascii 4 the masses!</TITLE>\n<META HTTP-EQUIV=\"refresh\" CONTENT=\"%u\"; url=\"%s\">\n<META HTTP-EQUIV=\"Pragma\" CONTENT=\"no-cache\">\n<STYLE TYPE=\"text/css\">\n<!--\npre {\nletter-spacing: 1px;\nlayer-background-color: Black;\nleft : auto;\nline-height : %upx;\n}\n-->\n</STYLE>\n</HEAD>\n<BODY bgcolor=\"#%s\" text=\"#%s\">\n<FONT SIZE=%u face=\"%s\">\n<PRE>\n",
	    refresh, aafile, linespace, background, foreground, fontsize,
	    fontface);

  return malloc (width * height * 4);
}

int
grab_one (int width, int height, int pal)
{
  unsigned char *frameData;
  if (vlBeginTransfer (svr, path, 0, NULL))
    {
      vlPerror ("vlBeginTransfer");
      exit (1);
    }
  do
    {
      info = vlGetNextValid (svr, buffer);
    }
  while (!info);
  frameData = vlGetActiveRegion (svr, buffer, info);
  strip_alpha (frameData, grab_buf.width * grab_buf.height);
  resample_8bit (frameData, grab_buf.width * grab_buf.height);
  zoom (frameData, grab_buf.width, grab_buf.height,
	image, width * 2, height * 2);
  vlEndTransfer (svr, path);
  vlPutFree (svr, buffer);
}

void
grab_close (void)
{
  vlDeregisterBuffer (svr, path, drn, buffer);
  vlDestroyBuffer (svr, buffer);
  vlDestroyPath (svr, path);
  vlCloseVideo (svr);
}

#endif

void
config_init (int argc, char *argv[])
{
  int res;

  do
    {
#ifdef HAVE_SGI
      res = getopt (argc, argv, short_options);
#else
      res = getopt_long (argc, argv, short_options, long_options, NULL);
#endif
      switch (res)
	{
	case 'h':
	  fprintf (stderr, "%s", help);
	  exit (0);
	  break;
	case 'v':
	  exit (0);
	  break;
	case 'q':
	  quiet = 1;
	  break;
	case 'm':
	  if (strcasecmp (optarg, "live") == 0)
	    {
	      if (useftp)
		{
		  fprintf (stderr,"heek, ftp option makes no sense with live mode!\n");
		  useftp = 0;
		}
	      mode = 0;
	    }
	  else if (strcasecmp (optarg, "html") == 0)
	    {
	      mode = 1;
	      aafile = strdup ("hasciicam.html");
	    }
	  else if (strcasecmp (optarg, "text") == 0)
	    {
	      mode = 2;
	      aafile = strdup ("hasciicam.asc");
	    }
	  else
	    fprintf (stderr, "!! invalid mode selected, using live\n");
	  break;
	case 'd':
	  device = strdup (optarg);
	  break;
	case 'i':
	  input = atoi (optarg);
	  /* 
	     here we assume that capture cards have maximum 3 channels
	     (usually the 4th, when present, is the radio tuner) 
	   */
	  if (input > 3)
	    {
	      fprintf (stderr, "invalid input selected\n");
	      exit (1);
	    }
	  break;
	case 'n':
#ifdef HAVE_LINUX
	  if (strcmp (optarg, "pal") == 0)
	    norm = VIDEO_MODE_PAL;
	  else if (strcmp (optarg, "ntsc") == 0)
	    norm = VIDEO_MODE_NTSC;
	  else if (strcmp (optarg, "secam") == 0)
	    norm = VIDEO_MODE_SECAM;
	  else if (strcmp (optarg, "auto") == 0)
	    norm = VIDEO_MODE_AUTO;
	  else
	    fprintf (stderr, "!! invalid video norm selected, using auto\n");
	  break;
#endif
	case 's':
	  {
	    char *t;
	    char *tt;
	    t = optarg;
	    while (isdigit (*t))
	      t++;
	    *t = 0;
	    width = atoi (optarg);
	    tt = ++t;
	    while (isdigit (*tt))
	      tt++;
	    *tt = 0;
	    height = atoi (t);
	    whchanged = 1;
	  }
	  break;
	case 'S':
	  fontsize = atoi (optarg);
	  break;
	case 'a':
	  fontface = strdup (optarg);
	  break;
	case 'r':
	  refresh = atoi (optarg);
	  break;
	case 'o':
	  if(mode>0) {
	    free (aafile);
	    aafile = strdup (optarg);
	  }
	  break;
	case 'f':
	  if (mode>0) {
	    ftp = strdup (optarg);
	    useftp = 1;
	  }
	  else
	    fprintf (stderr,
		     "heek, ftp option makes no sense with live mode!\n");
	  break;
	case 'D':
	  daemon_mode = 1;
	  break;
	case 'b':
	  aabright = atoi (optarg);
	  break;
	case 'c':
	  aacontrast = atoi (optarg);
	  break;
	case 'g':
	  aagamma = atoi (optarg);
	  break;
	case 'I':
	  invert = 1;
	  break;
	case 'B':
	  background = strdup (optarg);
	  break;
	case 'F':
	  foreground = strdup (optarg);
	  break;
	case 'O':
	  if(mode>0) {
	    jpgfile = strdup (optarg);
	    jpgdump = 1;
	  } else
	    fprintf (stderr,"heek, jpeg dump becomes dangerous with live mode!\n");
	  break;
	case 'Q':
	  jpgqual = atoi (optarg);
	  break;
	case 'U':
	  uid = atoi (optarg);
	  break;
	case 'G':
	  gid = atoi (optarg);
	  break;
	  /*
	     case 'B':
	     bttvbright = atoi(optarg);
	     break;
	     case 'C':
	     bttvcontrast = atoi(optarg);
	     break;
	     case 'G':
	     bttvgamma = atoi(optarg);
	     break;
	   */
	}
    }
  while (res > 0);

  if ((width == 0) || (height == 0))
    {
      printf ("Error: invalid size %dx%d\n", width, height);
      exit (0);
    }

  if (useftp)
    {
      char *p, *pp;
      p = ftp;

      while (*p != '@')
	{
	  if ((ftp - p) < 32)
	    p++;
	  else
	    {
	      printf ("Error: malformed ftp command: %s\n", ftp);
	      exit (0);
	    }
	}
      *p = '\0';
      ftp_user = strdup(ftp);
      p++;
      pp = p;

      while (*p != ':')
	{
	  if ((pp - p) < 64)
	    p++;
	  else
	    {
	      printf ("Error: malformed ftp command: %s\n", ftp);
	      exit (0);
	    }
	}

      *p = '\0';
      ftp_host = strdup(pp);
      p++;
      pp = p;

      while (*p != '\0')
	{
	  if ((pp - p) < 64)
	    p++;
	  else
	    {
	      printf ("Error: malformed ftp command: %s\n", ftp);
	      exit (0);
	    }
	}
      if((pp-p)==0) ftp_dir = strdup(".");
      else ftp_dir = strdup(pp);
    }
}

/* here we go (chmicl broz rlz! :)*/

int
main (int argc, char **argv)
{

  /* 
     reminder:
     !!! grabbing height & width should be 
     double the ascii context width and height !!! 
   */

  /* aalib vars */
  int awidth, aheight;
  struct aa_renderparams ascii_parms;
  struct aa_hardware_params ascii_surf;
  struct aa_savedata ascii_save;

  /* image downsampling */
  unsigned char *grey;

  /* register signal traps */
  if (signal (SIGINT, quitproc) == SIG_ERR)
    {
      perror ("Couldn't install SIGINT handler");
      exit (1);
    }

  fprintf (stderr, version, PACKAGE, VERSION);

  /* default values */
  uid = getuid ();
  gid = getgid ();


  /* gathering line commands */
  config_init (argc, argv);

  /* initialize aalib default params */
  memcpy (&ascii_surf, &aa_defparams, sizeof (struct aa_hardware_params));

  /* sets the default size on live mode */
  if (whchanged == 0 && mode == 0)
    {
      width = 80;
      height = 40;
    }

  /* bttv grabber init */
  grab_init();

  /* width/height image setup */
  ascii_surf.width = width;
  ascii_surf.height = height;

  fprintf (stderr, "grabbed image is %dx%d",
	   grab_buf[ok_frame].width, grab_buf[ok_frame].height);

  setuid (uid);
  setgid (gid);

  switch (mode)
    {
    case 0:
      fprintf (stderr, " - ascii context is %dx%d\n", width, height);
      fprintf (stderr, "using LIVE mode\n");
      break;
      
    case 1:
      ascii_save.name = aafile;
      ascii_save.format = &aa_html_format;
      ascii_save.file = NULL;

      fprintf (stderr, " - ascii context is %dx%d\n", width, height);
      fprintf (stderr, "using HTML mode dumping to file %s\n", aafile);
      if (useftp)
	fprintf (stderr, " ftp-pushing on %s%s\n", ftp_host, ftp_dir);
      break;
      
    case 2:
      ascii_save.name = aafile;
      ascii_save.format = &aa_text_format;
      ascii_save.file = NULL;

      fprintf (stderr, " - ascii context is %dx%d\n", width, height);
      fprintf (stderr, "using TEXT mode dumping to file %s\n", aafile);
      if (useftp)
	fprintf (stderr, " ftp-pushing on ftp://%s%s\n", ftp_host, ftp_dir);

      break;

    default:
      break;
    }
  
  if(jpgdump)
    fprintf(stderr," dumping jpeg image on %s\n", jpgfile);

  fprintf(stderr,"\n");

  /* aalib init */
  if (mode > 0)
    ascii_img = aa_init (&save_d, &ascii_surf, &ascii_save);
  else
    ascii_img = aa_autoinit (&ascii_surf);

/* ftp init *//* untested with new code changes */
  if (useftp)
    {
      char temp[160];
      ftp_init (0);
      sprintf (temp, "password for %s@%s : ", ftp_user, ftp_host);
      ftp_pass = getpass (temp);
      ftp_connect (ftp_host, ftp_user, ftp_pass, ftp_dir);
    }

  /* setting ascii rendering parameters */
  ascii_parms.bright = aabright;
  ascii_parms.contrast = aacontrast;
  ascii_parms.gamma = aagamma;
  ascii_parms.dither = AA_FLOYD_S;
  ascii_parms.inversion = invert;
  ascii_parms.randomval = 0;

  grey =
    (unsigned char *) calloc (width * height * 4, sizeof (unsigned char));

  if (daemon_mode)
    daemon (0, 1);

  /* cycle until ctrl-c */
  
  while (userbreak < 1) {
    grab_one ();
    
    resample_8bit (rgb_surface, grab_size, grey);
    memcpy (aa_image (ascii_img), grey, width * height * 4);
    aa_render (ascii_img, &ascii_parms, 0, 0, width, height);
    aa_flush (ascii_img);
    if(jpgdump) {
      swap_rgb24 (image, grab_buf[ok_frame].width * grab_buf[ok_frame].height);
      write_jpeg (jpgfile, image, grab_buf[ok_frame].width, grab_buf[ok_frame].height);
    }
    
    if (useftp)
      {
	if (!ftp_connected)
	  ftp_connect (ftp_host, ftp_user, ftp_pass, ftp_dir);
	/* scolopendro is the tmp file being renamed
	   this is here for hystorical reasons
	   feel free to change it - be hacker ;) */
	ftp_upload (aafile, aafile, "scolopendro");
	if(jpgdump) ftp_upload (jpgfile, jpgfile, "scolopendro");
      }
    
    if (mode > 0)
      sleep (refresh);
  }

  if(jpgdump) free (jpgfile);
  if(mode>0) free (aafile);
  free (grey);
#ifdef HAVE_SGI
  grab_close ();
#else
  if (grab_fd > 0)
    close (grab_fd);

  if (image != NULL)
    munmap (grab_data, grab_size);
#endif

  fprintf (stderr, "cya!\n");
  exit (0);
}

/* signal handling */
void
quitproc (int Sig)
{

  fprintf (stderr, "interrupt caught, exiting.\n");
  ftp_close();
  userbreak = 1;

}
