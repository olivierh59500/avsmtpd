/*
 *  log.c - logging routines.
 *  Copyright (c) 1998 Alex L. Demidov
 */

/*
 *  $Id: log.c,v 1.5 2003-02-23 07:22:56 alexd Exp $
 *
 *  $Log: log.c,v $
 *  Revision 1.5  2003-02-23 07:22:56  alexd
 *  �� ��������� �� ������ � ����.
 *
 *  Revision 1.4  2003/02/22 18:32:19  alexd
 *  added dmalloc.h
 *  replace fprintf("") with fputs
 *  replace vsyslog with vprintf && syslog
 *
 *  Revision 1.3  2003/02/17 20:42:53  alexd
 *  some more clean up
 *
 *  Revision 1.2  2003/02/17 01:55:37  alexd
 *  some lint cleanup
 *
 *  Revision 1.1.1.1  2003/02/16 16:44:08  alexd
 *  import into cvs
 *
 *  Revision 1.6  2001/03/24 20:24:56  alexd
 *  program name cleanup
 *
 *  Revision 1.5  2001/03/24 17:51:05  alexd
 *  Added RCS strings to .c files
 *
 *  Revision 1.4  2001/03/24 17:48:31  alexd
 *  Changed *program for logging
 *
 *  Revision 1.3  2000/04/23 23:33:39  alexd
 *  try to fix %m in log printing
 *
 *  Revision 1.2  2000/04/23 09:19:12  alexd
 *  version 0.2
 *
 *  Revision 1.1.1.1  1999/03/12 22:41:10  alexd
 *  imported fidod
 *
 *
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#include "config.h"

#include "daemon.h"
#include "log.h"

#ifdef WITH_DMALLOC
#include "dmalloc.h"
#endif

#ifndef lint
static const char *rcsid = "$Id: log.c,v 1.5 2003-02-23 07:22:56 alexd Exp $";
static const char *revision = "$Revision: 1.5 $";
#endif

static int syslog_init = 0;
static char *logfile = NULL;

/* FIXME: prog name */
extern char *program; 

void initlog(const char *a_program, int facility, const char *a_logfile)
{
    program = strdup(a_program);
    if ( logfile != NULL) 
        logfile = strdup(a_logfile);

    openlog(program, LOG_CONS | LOG_PID, facility);
    syslog_init = 1;
}

void shutdownlog()
{
    closelog();

    syslog_init = 0;

    if ( logfile != NULL ) 
        free(logfile);
    free(program);

    logfile = program = NULL;
}

static void vmessage(int loglevel, const char *format, va_list ap)
{

    /* FIXME: try to log messages of any size */
#define BUFSIZE 256

    char buf[BUFSIZE];

    /* FIXME: logging to console if errors and logfile still not initilized */
    if (syslog_init) {
#if 0
	vsyslog(loglevel, format, ap);
#else
	vsnprintf(buf, BUFSIZE, format, ap);
        syslog( loglevel, buf);
#endif
    }

    if (logfile != NULL || !daemon_mode) {
	/* FIXME: %m format */
	char str[BUFSIZE];
	char timestamp[BUFSIZE];

	time_t t = time(NULL);
	struct tm *ptm = localtime(&t);

	int saved_errno = errno;
	char *errstr = strerror(saved_errno);

	char *p;

	vsnprintf(str, BUFSIZE, format, ap);
	strftime(timestamp, BUFSIZE, "%b %d %H:%M:%S", ptm);
	snprintf(buf, BUFSIZE, "%s %s[%d]: %s\n",
		 timestamp, program, (int) getpid(), str);
#if 0

	if ((p = strstr(buf, "%m")) != NULL ) {
	    char newbuf[BUFSIZE];

	    strncpy(newbuf, buf, p - buf);
	    strcpy(newbuf, errstr);
	    strcpy(newbuf, p + 2);

            strcpy ( buf, newbuf );
	}
#endif
        
    }

    if (logfile != NULL) {
	FILE *log;

        if ( geteuid() != 0 ) 
            enter_suid();
	log = fopen(logfile, "a");
        if ( geteuid() == 0 ) 
            leave_suid();

	if (!log) {
	    /* FIXME: don't try write to file */
	    /* error("can't open logfile %s:%m", logfile); */
	}
	else {
	    fputs(buf, log);
	    fclose(log);
	}
    }

    if (!daemon_mode) {
	fprintf(stderr, buf);
    }
}

/* FIXME: maybe cpp define? for those functions */

void debug(const char *format, ...)
{
    va_list ap;

    if (!verbose_mode)
	return;
    va_start(ap, format);
    vmessage(LOG_DEBUG, format, ap);
    va_end(ap);
}
void notice(const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    vmessage(LOG_NOTICE, format, ap);
    va_end(ap);
}

void error(const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    vmessage(LOG_ERR, format, ap);
    va_end(ap);
}

void message(int loglevel, const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    vmessage(loglevel, format, ap);
    va_end(ap);
}

#if 0
void Perror(const char *str)
{
    error("%s:%m", str);
}

#endif
