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
 * Program:	Operating-system dependent routines -- MS-DOS (Novell) version
 *
 * Author:	Mark Crispin
 *		Networks and Distributed Computing
 *		Computing & Communications
 *		University of Washington
 *		Administration Building, AG-44
 *		Seattle, WA  98195
 *		Internet: MRC@CAC.Washington.EDU
 *
 * Date:	11 April 1989
 * Last Edited:	30 August 2006
 */

/* Private function prototypes */

#include "tcp_dos.h"		/* must be before osdep includes tcp.h */
#include "mail.h"
#include "osdep.h"
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <sys\stat.h>
#include <sys\timeb.h>
#include <sys\socket.h>
#include <netinet\in.h>
#include <netdb.h>
#include "misc.h"


#include "fs_dos.c"
#include "ftl_dos.c"
#include "nl_dos.c"
#include "env_dos.c"
#undef write
#define read soread
#define write sowrite
#define close soclose 
#include "tcp_dos.c"


/* Return my local host name
 * Returns: my local host name
 */

char *mylocalhost (void)
{
  if (!myLocalHost) {		/* known yet? */
    char *s,tmp[MAILTMPLEN];
    struct hostent *he;
    struct in_addr in;
				/* could we get local id? */
    if ((in.s_addr = getmyipaddr ()) != -1) {
      if (he = gethostbyaddr ((char *) &in.s_addr,4,PF_INET)) s = he->h_name;
      else sprintf (s = tmp,"[%s]",inet_ntoa (in));
    }
    else s = "random-pc";	/* say what? */
    myLocalHost = cpystr (s);	/* record for subsequent use */
  }
  return myLocalHost;
}


/* Look up host address
 * Accepts: pointer to pointer to host name
 *	    socket address block
 * Returns: non-zero with host address in socket, official host name in host;
 *	    else NIL
 */

long lookuphost (char **host,struct sockaddr_in *sin)
{
  char *s = *host;		/* in case of error */
  sin->sin_addr.s_addr = rhost (host);
  if (sin->sin_addr.s_addr == -1) {
    *host = s;			/* error, restore old host name */
    return NIL;
  }
  *host = cpystr (*host);	/* make permanent copy of name */
  return T;			/* success */
}
