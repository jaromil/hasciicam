/*  HasciiCam 1.0
 *  (c) 2000-2003 Denis Rojo aka jaromil
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
 * CONTRIBUTIONS :
 * Diego Torres aka rapid <rapid@ivworlds.org> 
 *  uid/gid handling and some bugfixes
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
#include <sys/stat.h>
#include <pwd.h>
#include <signal.h>

#include <linux/types.h>
#include <linux/videodev.h>

#include <config.h>

#include <aalib.h>
#include <ftplib.h>

/* hasciicam modes */
#define LIVE 0
#define HTML 1
#define TEXT 2

/* commandline stuff */

char *version =
  "%s %s - (h)ascii 4 the masses! - http://ascii.dyne.org\n"
  "(c)2000-2003 Denis Rojo < jaromil @ dyne.org >\n"
  "watch out for the (h)ASCII ROOTS http://hascii.org\n\n";

char *help =
/* "\x1B" "c" <--- SCREEN CLEANING ESCAPE CODE
   why here? just a reminder for a shamanic secret told by bernie@codewiz.org */
"Usage: hasciicam [options] [rendering options] [aalib options]\n"
"options:\n"
"-h --help         this help\n"
"-H --aahelp       aalib complete help\n"
"-v --version      version information\n"
"-q --quiet        be quiet\n"
"-m --mode         mode: live|html|text      - default live\n"
"-d --device       video grabbing device     - default /dev/video\n"
"-i --input        input channel number      - default 1\n"
"-n --norm         norm: pal|ntsc|secam|auto - default auto\n"
"-s --size         ascii image size WxH      - default 96x72\n"
"-o --aafile       dumped file               - default hasciicam.[txt|html]\n"
"-f --ftp          ie: :user%pass@host:dir   - default none\n"
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
"-F --foreground   foreground color (hex)    - default 00FF00\n";

const struct option long_options[] = {
  {"help", no_argument, NULL, 'h'},
  {"aahelp", no_argument, NULL, 'H'},
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
  {"uid", required_argument, NULL, 'U'},
  {"gid", required_argument, NULL, 'G'},
  //  { "bttvbright", required_argument, NULL, 'B' }, 
  //  { "bttvcontrast", required_argument, NULL, 'C' },
  //  { "bttvgamma", required_argument, NULL, 'G' },
  {0, 0, 0, 0}
};

char *short_options = "hHvqm:d:i:n:s:f:DS:a:r:o:b:c:g:IB:F:O:Q:U:G:";

/* default configuration */
int quiet = 0;
int mode = 0;
int useftp = 0;
int input = 1;
int daemon_mode = 0;
int invert = 0;
int norm = VIDEO_MODE_AUTO;

struct geometry {
  int w, h, size;
  int bright, contrast, gamma; };
struct geometry aa_geo;
struct geometry vid_geo;
/* if width&height have been manually changed */
int whchanged = 0;

char device[256];
int have_tuner = 0;

int refresh = 2;
int fontsize = 1;
int linespace = 5;
char background[64];
char foreground[64];
char fontface[256];

/* ftp stuff */
char ftp[512];
char ftp_user[256];
char ftp_host[256];
char ftp_dir[256];
char ftp_pass[256];
int ftp_passive;
netbuf *ftpconn = NULL;

int uid = -1;
int gid = -1;

/* buffers */
unsigned char *image = NULL; /* mmapped */
char aafile[256];


/* declare the sighandler */
void quitproc (int Sig);
volatile sig_atomic_t userbreak;

/* ascii context & html formatting stuff*/
aa_context *ascii_context;
struct aa_renderparams *ascii_rndparms;
struct aa_hardware_params ascii_hwparms;
struct aa_savedata ascii_save;

char html_header[512];

char *html_escapes[] =
  { "<", "&lt;", ">", "&gt;", "&", "&amp;", NULL };

struct aa_format hascii_format = {
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

/* v4l */
unsigned char *grab_data;
struct video_capability grab_cap;
struct video_mbuf grab_map;
struct video_mmap grab_buf[32];
struct video_channel grab_chan;
struct video_picture grab_pic;
struct video_tuner grab_tuner;
int minw, minh, maxw, maxh;

int dev = -1;
int cur_frame, ok_frame;
int palette;

/* greyscale image is sampled from Y luminance component */
unsigned char *grey;
int YtoRGB[256];

void YUV422_to_grey(unsigned char *src, unsigned char *dst, int w, int h) {
  int c,cc;
  for (c=0,cc=0;c<vid_geo.size;c++,cc+=2)
    dst[c] = YtoRGB[src[cc]];
}

int vid_detect(char *devfile) {
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

  if (-1 == (dev = open(devfile,O_RDWR|O_NONBLOCK))) {
    perror("!! error in opening video capture device: ");
    return -1;
  } else {
    close(dev);
    dev = open(devfile,O_RDWR);
  }
  
  res = ioctl(dev,VIDIOCGCAP,&grab_cap);
  if(res<0) {
    perror("!! error in VIDIOCGCAP: ");
    return -1;
  }

  fprintf(stderr,"Device detected is %s\n",devfile);
  fprintf(stderr,"%s\n",grab_cap.name);
  fprintf(stderr,"%u channels detected\n",grab_cap.channels);
  fprintf(stderr,"max size w[%u] h[%u] - min size w[%u] h[%u]\n",grab_cap.maxwidth,grab_cap.maxheight,grab_cap.minwidth,grab_cap.minheight);
  fprintf(stderr,"Video capabilities:\n");
  for (counter=0;counter<11;counter++)
    if (grab_cap.type & (1 << counter)) fprintf(stderr,"%s\n",capabilities[counter]);
  
  if (-1 == ioctl(dev, VIDIOCGPICT, &grab_pic)) {
    perror("!! ioctl VIDIOCGPICT: ");
    exit(1);
  }
  
  if (grab_pic.palette & VIDEO_PALETTE_GREY)
    fprintf(stderr,"VIDEO_PALETTE_GREY        device is able to grab greyscale frames\n");

  
  if(grab_cap.type & VID_TYPE_TUNER)
    /* if the device does'nt has any tuner, so we avoid some ioctl
       this should be a fix for many webcams, thanks to Ben Wilson */
    have_tuner = 1;
  

  /* set and check the minwidth and minheight */
  minw = grab_cap.minwidth;
  minh = grab_cap.minheight;
  maxw = grab_cap.maxwidth;
  maxh = grab_cap.maxheight;

  if (ioctl (dev, VIDIOCGMBUF, &grab_map) == -1) {
    perror("!! error in ioctl VIDIOCGMBUF: ");
    return -1;
  }
  /* print memory info */
  fprintf(stderr,"memory map of %i frames: %i bytes\n",grab_map.frames,grab_map.size);
  for(counter=0;counter<grab_map.frames;counter++)
    fprintf(stderr,"Offset of frame %i: %i\n",counter,grab_map.offsets[counter]);
  return dev;
}


int vid_init() {

  int linespace = 5;
  int i;

  /* set image source and TV norm */
  grab_chan.channel = input = (grab_cap.channels>1) ? 1 : 0;

  
  if(have_tuner) { /* does this only if the device has a tuner */
    //   _band = 5; /* default band is europe west */
    //   _freq = 0;
    /* resets CHAN */
    if (-1 == ioctl(dev,VIDIOCGCHAN,&grab_chan))
      fprintf(stderr,"!! error in ioctl VIDIOCGCHAN: %s",strerror(errno));

    if (-1 == ioctl(dev,VIDIOCSCHAN,&grab_chan))
      fprintf(stderr,"error in ioctl VIDIOCSCHAN: %s",strerror(errno));
    
    /* get/set TUNER settings */
    if (-1 == ioctl(dev,VIDIOCGTUNER,&grab_tuner))
      fprintf(stderr,"error in ioctl VIDIOCGTUNER: %s",strerror(errno));
  }

  /* init video size from ascii size
     1 ascii pixel = 4 video pixel
     so video h&w are each double than ascii */
  aa_geo.size = aa_geo.w*aa_geo.h;
  vid_geo.h = aa_geo.h*2;
  vid_geo.w = aa_geo.w*2;
  vid_geo.size = vid_geo.w*vid_geo.h;
  palette = VIDEO_PALETTE_YUV422;

  grey = (unsigned char *) malloc (vid_geo.size);
  for (i=0; i< 256; i++)
    YtoRGB[i] = 1.164*(i-16);

  for(i=0; i<grab_map.frames; i++) {
    grab_buf[i].format = palette; //RGB24;
    grab_buf[i].frame  = i;
    grab_buf[i].height = vid_geo.h;
    grab_buf[i].width = vid_geo.w;
  }
  


  grab_data = mmap (0, grab_map.size, PROT_READ | PROT_WRITE, MAP_SHARED, dev, 0);
  if (MAP_FAILED == grab_data) {
    perror ("Cannot allocate video4linux grabber buffer ");
    exit (1); }

  /* feed up the mmapped frames */
    if (-1 == ioctl(dev,VIDIOCMCAPTURE,&grab_buf[0])) {
      fprintf(stderr,"error in ioctl VIDIOCMCAPTURE: %s",strerror(errno));
    }
  cur_frame = ok_frame = 0;

  /* init the html header */
  snprintf (&html_header[0], 1024,
	    "<HTML>\n <HEAD> <TITLE>wow! (h)ascii 4 the masses!</TITLE>\n"
	    "<META HTTP-EQUIV=\"refresh\" CONTENT=\"%u\"; url=\"%s\">\n"
	    "<META HTTP-EQUIV=\"Pragma\" CONTENT=\"no-cache\">\n"
	    "<STYLE TYPE=\"text/css\">\n"
	    "<!--\npre {\nletter-spacing: 1px;\n"
	    "layer-background-color: Black;\n"
	    "left : auto;\nline-height : %upx;\n}\n-->\n"
	    "</STYLE>\n</HEAD>\n<BODY bgcolor=\"#%s\" text=\"#%s\">\n"
	    "<FONT SIZE=%u face=\"%s\">\n<PRE>\n",
	    refresh, aafile, linespace, background, foreground, fontsize,
	    fontface);
  return dev;
}

unsigned char *grab_one () {
  int c = 0, cc=0;
  /* we use just one frame
     no matters about the capability of the cam
     this makes grabbing much faster on my webcam
     i hope also on yours
     ok_frame = cur_frame;
     cur_frame = (cur_frame>=grab_map.frames) ? 0 : cur_frame+1;
  */

  ok_frame = 0; cur_frame = 0;

  grab_buf[ok_frame].format = palette;
  if (-1 == ioctl(dev,VIDIOCSYNC,&grab_buf[ok_frame])) {
    perror("error in ioctl VIDIOCSYNC: ");
    return NULL;
  }

  grab_buf[cur_frame].format = palette;
  if (-1 == ioctl(dev,VIDIOCMCAPTURE,&grab_buf[cur_frame])) {
    perror("error in ioctl VIDIOCMCAPTURE: ");
    return NULL;
  }

  YUV422_to_grey(&grab_data[grab_map.offsets[ok_frame]],
		 grey,vid_geo.w,vid_geo.h);

  return grey;

}



void
config_init (int argc, char *argv[]) {
  int res;

  /* setup defaults */

  { /* device filename */
    struct stat st;
    if( stat("/dev/video",&st) <0)
      strcpy(device,"/dev/video0");
    else
      strcpy(device,"/dev/video");
  }
  strcpy(background,"000000");
  strcpy(foreground,"00FF00");
  strcpy(fontface,"courier"); /* you'd better choose monospace fonts */
  aa_geo.w = 96;
  aa_geo.h = 72;
  aa_geo.bright = 50;
  aa_geo.contrast = 10;
  aa_geo.gamma = 10;
  
  ftp_passive = 0;

  do {
    res = getopt_long (argc, argv, short_options, long_options, NULL);
    
    switch (res) {
    case 'h':
      fprintf (stderr, "%s", help);
      exit (1);
      break;
    case 'H':
      fprintf (stderr, "%s", help);
      fprintf (stderr, "\naalib options:\n%s",aa_help);
      exit(1);
    case 'v':
      exit (1);
      break;
    case 'q':
      quiet = 1;
      break;
    case 'm':
      if (strcasecmp (optarg, "live") == 0) {
	if (useftp) {
	  fprintf (stderr,"heek, ftp option makes no sense with live mode!\n");
	  useftp = 0;
	}
	mode = LIVE;
      } else if (strcasecmp (optarg, "html") == 0) {
	mode = HTML;
	strcpy(aafile,"hasciicam.html");
      } else if (strcasecmp (optarg, "text") == 0) {
	mode = TEXT;
	strcpy(aafile,"hasciicam.asc");
      } else {
	fprintf (stderr, "!! invalid mode selected, using live\n");
	mode = LIVE;
      }
      break;
    case 'd':
      strncpy(device,optarg,256);
      break;
    case 'i':
      input = atoi (optarg);
      /* 
	 here we assume that capture cards have maximum 3 channels
	 (usually the 4th, when present, is the radio tuner) 
      */
      if (input > 3) {
	fprintf (stderr, "invalid input selected\n");
	exit (1);
      }
      break;
    case 'n':
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
      
    case 's':
      {
	char *t;
	char *tt;
	t = optarg;
	while (isdigit (*t))
	  t++;
	*t = 0;
	aa_geo.w = atoi (optarg);
	tt = ++t;
	while (isdigit (*tt))
	  tt++;
	*tt = 0;
	aa_geo.h = atoi (t);
	whchanged = 1;
      }
      break;
    case 'S':
      fontsize = atoi (optarg);
      switch (fontsize) {
      case 1: linespace = 5; break;
      case 2: linespace = 10; break;
      case 3: linespace = 11; break;
      case 4: linespace = 13; break;
      default: linespace = 15; break;
      }
      break;
    case 'a':
      strncpy(fontface,optarg,256);
      break;
    case 'r':
      refresh = atoi (optarg);
      break;
    case 'o':
      if(mode>0)
	strncpy(aafile,optarg,256);
      break;
    case 'f':
      if (mode>0) {
	strncpy(ftp,optarg,512);
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
      aa_geo.bright = atoi (optarg);
      break;
    case 'c':
      aa_geo.contrast = atoi (optarg);
      break;
    case 'g':
      aa_geo.gamma = atoi (optarg);
      break;
    case 'I':
      invert = 1;
      break;
    case 'B':
      strncpy(background,optarg,64);
      break;
    case 'F':
      strncpy(foreground,optarg,64);
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
  } while (res > 0);

  /* live mode defaults to a different size */
  if(!whchanged && mode==LIVE) {
    aa_geo.w = 80;
    aa_geo.h = 40;
  }

  if (useftp) {
    /* i have to say i'm quite proud of the parsers i write :) */
    char *p, *pp;
    p = pp = ftp;

    /* duepunti at the beginning for passive mode */
    if(*p == ':') { ftp_passive = 1; p++; pp++; }

    /* get the user and check if a password has been specified */
    while (*p != '@') {
      if(*p == '%') { /* pass found, get it */
	*p = '\0'; strncpy(ftp_pass,pp,256);
	pp = p+1;
      }
      if ((p - pp) < 256) p++;
      else {
	fprintf (stderr,"Error: malformed ftp command: %s\n", ftp);
	exit (0); }
    } /* here we have the username */
     *p = '\0'; strncpy(ftp_user,pp,256);
    p++; pp = p;
    
    while (*p != ':') {
      if ((pp - p) < 256) p++;
      else {
	fprintf (stderr,"Error: malformed ftp command: %s\n", ftp);
	exit (0); }
    } /* here the host */
    *p = '\0'; strncpy(ftp_host,pp,256);
    p++; pp = p;

    while (*p != '\0' && *p != '\n') {
      if ((pp - p) < 256) p++;
      else {
	fprintf (stderr,"Error: malformed ftp command: %s\n", ftp);
	exit (0); }
    }
    if((pp-p)==0) strcpy(ftp_dir,".");
    else strncpy(ftp_dir,pp,256);
  }

}

/* here we go (chmicl broz rlz! :)*/

int
main (int argc, char **argv) {
  /* reminder:
     !!! grabbing height & width should be double
     the ascii context width and height !!! */

  /* register signal traps */
  if (signal (SIGINT, quitproc) == SIG_ERR) {
      perror ("Couldn't install SIGINT handler"); exit (1); }

  fprintf (stderr, version, PACKAGE, VERSION);

  /* default values */
  uid = getuid ();
  gid = getgid ();

  /* initialize aalib default params */
  memcpy (&ascii_hwparms, &aa_defparams, sizeof (struct aa_hardware_params));
  ascii_rndparms = aa_getrenderparams();
  //  memcpy (&ascii_rndparms,&aa_defrenderparams,sizeof(struct aa_renderparams));

  /* gathering aalib commandline options */
  aa_parseoptions (&ascii_hwparms, ascii_rndparms, &argc, argv);

  /* and hasciicam options */
  config_init (argc, argv);

  /* detect and init video device */
  if( vid_detect(device) > 0 )
    vid_init();
  else
    exit(-1);

  /* width/height image setup */
  ascii_hwparms.width = aa_geo.w;
  ascii_hwparms.height = aa_geo.h;
  
  setuid (uid);
  setgid (gid);

  fprintf (stderr, " - (h)ascii size is %dx%d\n", aa_geo.w, aa_geo.h);

  switch (mode)
    {
    case LIVE:
      fprintf (stderr, "using LIVE mode\n");
      break;
      
    case HTML:
      ascii_save.name = aafile;
      ascii_save.format = &hascii_format;
      ascii_save.file = NULL;

      fprintf (stderr, "using HTML mode dumping to file %s\n", aafile);
      break;
      
    case TEXT:
      ascii_save.name = aafile;
      ascii_save.format = &aa_text_format;
      ascii_save.file = NULL;

      fprintf (stderr, "using TEXT mode dumping to file %s\n", aafile);

      break;

    default:
      break;
    }
  
  fprintf(stderr,"\n");

  /* aalib init */
  if (mode > 0)
    ascii_context = aa_init (&save_d, &ascii_hwparms, &ascii_save);
  else
    ascii_context = aa_autoinit (&ascii_hwparms);

  if(!ascii_context) {
    fprintf(stderr,"!! cannot initialize aalib\n");
    exit(-1);
  }
    
  while (useftp)
    {
      char temp[160];
      fprintf (stderr, "ftp push on ftp://%s@%s:%s\n", ftp_user, ftp_host, ftp_dir);

      FtpInit();
      if(!FtpConnect(ftp_host,&ftpconn)) {
	fprintf(stderr,"Unable to connect to host %s\n", ftp_host);
	useftp = 0; break;
      }
      if(ftp_passive)
	if(!FtpOptions(FTPLIB_CONNMODE,FTPLIB_PASSIVE,ftpconn)) {
	  fprintf(stderr,"Unable to activate passive mode: %s\n",FtpLastResponse(ftpconn));
	  useftp = 0; break;
	}
      if(!strchr(ftp,'%')) {
	sprintf (temp, "password for %s@%s : ", ftp_user, ftp_host);
	strncpy(ftp_pass, getpass(temp), 256);
      }
      if(!FtpLogin(ftp_user, ftp_pass, ftpconn)) {
	fprintf(stderr,"Login Failure: %s\n",FtpLastResponse(ftpconn));
	useftp = 0; break;
      }
      if(!FtpChdir(ftp_dir,ftpconn)) {
	fprintf(stderr,"Change directory failed: %s\n",FtpLastResponse(ftpconn));
	useftp = 0; break;
      }
      break;
    }


  ascii_rndparms->bright = aa_geo.bright;
  ascii_rndparms->contrast = aa_geo.contrast;
  ascii_rndparms->gamma = aa_geo.gamma;
  // those are left to be setted by aalib options
  //  ascii_rndparms->dither = AA_FLOYD_S;
  //  ascii_rndparms->inversion = invert;
  //  ascii_rndparms->randomval = 0;


  if (daemon_mode)
    daemon (0, 1);

  /* cycle until ctrl-c */
  
  while (userbreak < 1) {
    grab_one ();
    
    memcpy (aa_image (ascii_context), grey, vid_geo.size);
    aa_render (ascii_context, ascii_rndparms, 0, 0, 
	       vid_geo.w,vid_geo.h);
    aa_flush (ascii_context);
    
    if (useftp) {
      //      if (!ftp_connected)
      //	ftp_connect (ftp_host, ftp_user, ftp_pass, ftp_dir);
      /* scolopendro is the tmp file being renamed
	 it is called so for hystorical reasons */
      //      ftp_upload (aafile, aafile, "scolopendro");
      if(!FtpPut(aafile,"scolopendro",FTPLIB_ASCII,ftpconn))
	fprintf(stderr,"Error in ftp put: %s\n",FtpLastResponse(ftpconn));
      if(!FtpRename("scolopendro",aafile,ftpconn))
	fprintf(stderr,"Error in ftp rename %s\n",FtpLastResponse(ftpconn));
    }
    
    if (mode != LIVE) sleep (refresh);
  }

  /* CLEAN EXIT */
  
  if(useftp)
    FtpClose(ftpconn);

  aa_close(ascii_context);
  //  free(ascii_rndparms);

  free (grey);
  
  if (dev > 0) close (dev);

  if (image != NULL)
    munmap (grab_data, grab_map.size);

  fprintf (stderr, "cya!\n");
  exit (0);
}

/* signal handling */
void
quitproc (int Sig)
{

  fprintf (stderr, "interrupt caught, exiting.\n");

  userbreak = 1;

}
