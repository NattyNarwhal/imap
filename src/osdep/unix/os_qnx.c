/* ========================================================================
 * Copyright 1988-2006 University of Washington
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * 
 * ========================================================================
 */

/*
 * Program:	Operating-system dependent routines -- QNX version
 *
 * Author:	Mark Crispin
 *		Networks and Distributed Computing
 *		Computing & Communications
 *		University of Washington
 *		Administration Building, AG-44
 *		Seattle, WA  98195
 *		Internet: MRC@CAC.Washington.EDU
 *
 * Date:	1 August 1993
 * Last Edited:	20 December 2006
 */

#include "tcp_unix.h"		/* must be before osdep includes tcp.h */
#include "mail.h"
#include "osdep.h"
#include <stdio.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ctype.h>
#include <errno.h>
extern int errno;		/* just in case */
#include <pwd.h>
#include <shadow.h>
#include <sys/select.h>
#include "misc.h"

#define DIR_SIZE(d) d->d_reclen

extern char *crypt (const char *pw, const char *salt);

#include "fs_unix.c"
#include "ftl_unix.c"
#include "nl_unix.c"
#include "env_unix.c"
#include "tcp_unix.c"
#include "gr_waitp.c"
#include "tz_sv4.c"
#include "gethstid.c"
#include "scandir.c"

/* QNX local readdir()
 * Accepts: directory structure
 * Returns: direct struct or NIL if failed
 */

#undef readdir

struct direct *Readdir (DIR *dirp)
{
  static struct direct dc;
  struct dirent *de = readdir (dirp);
  if (!de) return NIL;		/* end of data */
  dc.d_fileno = 0;		/* could get from de->stat.st_ino */
  dc.d_namlen = strlen (strcpy (dc.d_name,de->d_name));
  dc.d_reclen = sizeof (dc);
  return &dc;
}
