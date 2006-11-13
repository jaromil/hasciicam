/*
 * (c) 1998-2000 Gerd Knorr
 *
 *   functions to handle ftp uploads using the ftp utility
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>

#include "config.h"
#include "ftp.h"

/* ---------------------------------------------------------------------- */
/* FTP stuff                                                              */

int ftp_connected;
int ftp_debug;

static int ftp_pty, ftp_pid;
static char tty_name[32];
static int userbreak = 0;

static void ftp_send (int argc, ...);
static int ftp_recv (void);

static int
open_pty ()
{
#ifdef HAVE_GETPT
  int master;
  char *slave;

  if (-1 == (master = getpt ()))
    return -1;
  if (-1 == grantpt (master) ||
      -1 == unlockpt (master) || NULL == (slave = ptsname (master)))
    {
      close (master);
      return -1;
    }
  strcpy (tty_name, slave);
  return master;
#else
  static char pty_name[32];
  static char s1[] = "pqrs";
  static char s2[] = "0123456789abcdef";

  char *p1, *p2;
  int pty;

  for (p1 = s1; *p1; p1++)
    {
      for (p2 = s2; *p2; p2++)
	{
	  sprintf (pty_name, "/dev/pty%c%c", *p1, *p2);
	  sprintf (tty_name, "/dev/tty%c%c", *p1, *p2);
	  if (-1 == access (tty_name, R_OK | W_OK))
	    continue;
	  if (-1 != (pty = open (pty_name, O_RDWR)))
	    return pty;
	}
    }
  return -1;
#endif
}

void
ftp_init (int passive)
{
  static char *noauto[] = { "ftp", "-n", NULL };

  if (-1 == (ftp_pty = open_pty ()))
    {
      fprintf (stderr, "can't grab pty\n");
      exit (1);
    }
  switch (ftp_pid = fork ())
    {
    case -1:
      perror ("fork");
      exit (1);
    case 0:
      /* child */
      close (ftp_pty);
      close (0);
      close (1);
      close (2);
      setsid ();
      open (tty_name, O_RDWR);
      dup (0);
      dup (0);
#ifndef HAVE_SGI
      unsetenv ("LANG");	/* need english messages from ftp */
#endif
      execvp (noauto[0], noauto);
      perror ("execvp");
      exit (1);
    default:
      /* parent */
      break;
    }
  ftp_recv ();

  /* initialisation */
  if (passive)
    {
      ftp_send (1, "pass");
      ftp_recv ();
    }
  return;
}

void
ftp_send (int argc, ...)
{
  va_list ap;
  char line[256], *arg;
  int length, i;

  va_start (ap, argc);
  memset (line, 0, 256);
  length = 0;
  for (i = 0; i < argc; i++)
    {
      if (i)
	line[length++] = ' ';
      arg = va_arg (ap, char *);
      length += strlen (arg);
      strcat (line, arg);
    }
  line[length++] = '\n';
  va_end (ap);

  if (ftp_debug)
    fprintf (stderr, ">> %s", line);
  if (length != write (ftp_pty, line, length))
    {
      fprintf (stderr, "ftp: write error\n");
      exit (1);
    }
}

int
ftp_recv ()
{
  char line[512], *p, *n;
  int length, done, status, ret = 0;
  fd_set set;

  for (done = 0; !done;)
    {
      FD_ZERO (&set);
      FD_SET (ftp_pty, &set);
      select (ftp_pty + 1, &set, NULL, NULL, NULL);

      switch (length = read (ftp_pty, line, 511))
	{
	case -1:
	  perror ("ftp: read error");
	  exit (1);
	case 0:
	  fprintf (stderr, "ftp: EOF\n");
	  exit (1);
	}
      line[length] = 0;

      for (p = line; p && *p; p = n)
	{
	  /* split into lines */
	  if (NULL != (n = strchr (p, '\n'))
	      || NULL != (n = strchr (p, '\r')))
	    *(n++) = 0;
	  else
	    n = NULL;
	  if (ftp_debug)
	    fprintf (stderr, "<< %s\n", p);

	  /* prompt? */
	  if (NULL != strstr (p, "ftp>"))
	    {
	      done = 1;
	    }

	  /* line dropped ? */
	  if (NULL != strstr (p, "closed connection"))
	    {
	      fprintf (stderr, "ftp: lost connection\n");
	      ftp_connected = 0;
	    }
	  if (NULL != strstr (p, "Not connected"))
	    {
	      if (ftp_connected)
		fprintf (stderr, "ftp: lost connection\n");
	      ftp_connected = 0;
	    }

	  /* status? */
	  if (1 == sscanf (p, "%d", &status))
	    {
	      ret = status;
	    }
	}
    }
  return ret;
}

void
ftp_connect (char *host, char *user, char *pass, char *dir)
{
  int delay = 0, status;

  //  for (;;)
  while(userbreak < 1)
    {
      /* Wiederholungsversuche mit wachsendem Intervall, 10 min max. */
      if (delay)
	{
	  fprintf (stderr, "ftp: connect failed, sleeping %d sec\n", delay);
	  sleep (delay);
	  delay *= 2;
	  if (delay > 600)
	    delay = 600;
	}
      else
	{
	  delay = 5;
	}

      /* (re-) connect */
      ftp_send (1, "close");
      ftp_recv ();
      ftp_send (2, "open", host);
      status = ftp_recv ();
      if (230 == status)
	{
	  fprintf (stderr, "ftp: connected to %s, login ok\n", host);
	  ftp_connected = 1;
	  goto login_ok;
	}
      if (220 != status && 530 != status)
	continue;

      fprintf (stderr, "ftp: connected to %s\n", host);
      ftp_connected = 1;

      /* login */
      ftp_send (3, "user", user, pass);
      if (230 != ftp_recv ())
	{
	  if (!ftp_connected)
	    continue;
	  fprintf (stderr, "ftp: login incorrect\n");
	  exit (1);
	}

    login_ok:
      fprintf(stderr, "ftp: login successful\n");
      /* set directory */
      ftp_send (2, "cd", dir);

      if (250 != ftp_recv ())
	{
	  if (!ftp_connected)
	    continue;
	  fprintf(stderr,"ftp: cd %s failed, ignoring directory path\n",dir);
	}
      
      /* initialisation */
      ftp_send (1, "bin");
      ftp_recv ();
      ftp_send (1, "umask 022");
      ftp_recv ();
      
      /* ok */
      break;
    }
}

void
ftp_upload (char *local, char *remote, char *tmp)
{
  ftp_send (3, "put", local, tmp);
  ftp_recv ();
  ftp_send (3, "rename", tmp, remote);
  ftp_recv ();
}

void
ftp_close ()
{
  if(ftp_connected) ftp_send(1,"quit");
  close(ftp_pty);
  userbreak = 1;
}
