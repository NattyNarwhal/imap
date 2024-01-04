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
 * Program:	UNIX environment routines
 *
 * Author:	Mark Crispin
 *		Networks and Distributed Computing
 *		Computing & Communications
 *		University of Washington
 *		Administration Building, AG-44
 *		Seattle, WA  98195
 *		Internet: MRC@CAC.Washington.EDU
 *
 * Date:	1 August 1988
 * Last Edited:	30 August 2006
 */


typedef struct dotlock_base {
  char lock[MAILTMPLEN];
  int pipei;
  int pipeo;
} DOTLOCK;


/* Bits that can be set in restrictBox */

#define RESTRICTROOT 0x1	/* restricted box doesn't allow root */
#define RESTRICTOTHERUSER 0x2	/* restricted box doesn't allow other user */

/* Subscription definitions for UNIX */

#define SUBSCRIPTIONFILE(t) sprintf (t,"%s/.mailboxlist",myhomedir ())
#define SUBSCRIPTIONTEMP(t) sprintf (t,"%s/.mlbxlsttmp",myhomedir ())


/* dorc() options */

#define SYSCONFIG "/etc/c-client.cf"


/* Special users */

#define ANONYMOUSUSER "nobody"	/* anonymous user */
#define UNLOGGEDUSER "root"	/* unlogged-in user */
#define ADMINGROUP "mailadm"	/* mail administrator group */

/* Function prototypes */

#include "env.h"

void rfc822_fixed_date (char *date);
long env_init (char *user,char *home);
char *myusername_full (unsigned long *flags);
#define MU_LOGGEDIN 0
#define MU_NOTLOGGEDIN 1
#define MU_ANONYMOUS 2
#define myusername() \
  myusername_full (NIL)
char *sysinbox ();
char *mailboxdir (char *dst,char *dir,char *name);
long dotlock_lock (char *file,DOTLOCK *base,int fd);
long dotlock_unlock (DOTLOCK *base);
int lockname (char *lock,char *fname,int op,long *pid);
int lockfd (int fd,char *lock,int op);
int lock_work (char *lock,void *sbuf,int op,long *pid);
long chk_notsymlink (char *name,void *sbuf);
void unlockfd (int fd,char *lock);
long set_mbx_protections (char *mailbox,char *path);
long get_dir_protection (char *mailbox);
MAILSTREAM *user_flags (MAILSTREAM *stream);
char *default_user_flag (unsigned long i);
void dorc (char *file,long flag);
long path_create (MAILSTREAM *stream,char *mailbox);
void grim_pid_reap_status (int pid,int killreq,void *status);
#define grim_pid_reap(pid,killreq) \
  grim_pid_reap_status (pid,killreq,NIL)
long safe_write (int fd,char *buf,long nbytes);
void *arm_signal (int sig,void *action);
struct passwd *checkpw (struct passwd *pw,char *pass,int argc,char *argv[]);
long loginpw (struct passwd *pw,int argc,char *argv[]);
long pw_login (struct passwd *pw,char *auser,char *user,char *home,int argc,
	       char *argv[]);
void *mm_blocknotify (int reason,void *data);
