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
 * Program:	Dummy routines for WCE
 *
 * Author:	Mark Crispin
 *		Networks and Distributed Computing
 *		Computing & Communications
 *		University of Washington
 *		Administration Building, AG-44
 *		Seattle, WA  98195
 *		Internet: MRC@CAC.Washington.EDU
 *
 * Date:	24 May 1993
 * Last Edited:	30 August 2006
 */


#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <direct.h>
#include "mail.h"
#include "osdep.h"
#include <sys\stat.h>
#include <dos.h>
#include "dummy.h"
#include "misc.h"

/* Function prototypes */

DRIVER *dummy_valid (char *name);
void *dummy_parameters (long function,void *value);
MAILSTREAM *dummy_open (MAILSTREAM *stream);
void dummy_close (MAILSTREAM *stream,long options);
long dummy_ping (MAILSTREAM *stream);
void dummy_check (MAILSTREAM *stream);
long dummy_expunge (MAILSTREAM *stream,char *sequence,long options);
long dummy_copy (MAILSTREAM *stream,char *sequence,char *mailbox,long options);
long dummy_append (MAILSTREAM *stream,char *mailbox,append_t af,void *data);

/* Dummy routines */


/* Driver dispatch used by MAIL */

DRIVER dummydriver = {
  "dummy",			/* driver name */
  DR_LOCAL|DR_MAIL,		/* driver flags */
  (DRIVER *) NIL,		/* next driver */
  dummy_valid,			/* mailbox is valid for us */
  dummy_parameters,		/* manipulate parameters */
  dummy_scan,			/* scan mailboxes */
  dummy_list,			/* list mailboxes */
  dummy_lsub,			/* list subscribed mailboxes */
  NIL,				/* subscribe to mailbox */
  NIL,				/* unsubscribe from mailbox */
  dummy_create,			/* create mailbox */
  dummy_delete,			/* delete mailbox */
  dummy_rename,			/* rename mailbox */
  mail_status_default,		/* status of mailbox */
  dummy_open,			/* open mailbox */
  dummy_close,			/* close mailbox */
  NIL,				/* fetch message "fast" attributes */
  NIL,				/* fetch message flags */
  NIL,				/* fetch overview */
  NIL,				/* fetch message structure */
  NIL,				/* fetch header */
  NIL,				/* fetch text */
  NIL,				/* fetch message data */
  NIL,				/* unique identifier */
  NIL,				/* message number from UID */
  NIL,				/* modify flags */
  NIL,				/* per-message modify flags */
  NIL,				/* search for message based on criteria */
  NIL,				/* sort messages */
  NIL,				/* thread messages */
  dummy_ping,			/* ping mailbox to see if still alive */
  dummy_check,			/* check for new messages */
  dummy_expunge,		/* expunge deleted messages */
  dummy_copy,			/* copy messages to another mailbox */
  dummy_append,			/* append string message to mailbox */
  NIL				/* garbage collect stream */
};


				/* prototype stream */
MAILSTREAM dummyproto = {&dummydriver};

				/* driver parameters */
static char *file_extension = NIL;

/* Dummy validate mailbox
 * Accepts: mailbox name
 * Returns: our driver if name is valid, NIL otherwise
 */

DRIVER *dummy_valid (char *name)
{
  char *s,tmp[MAILTMPLEN];
  struct stat sbuf;
				/* must be valid local mailbox */
  return (name && *name && (*name != '{') &&
	  (s = mailboxfile (tmp,name)) && (!*s || !stat (s,&sbuf))) ?
	    &dummydriver : NIL;
}


/* Dummy manipulate driver parameters
 * Accepts: function code
 *	    function-dependent value
 * Returns: function-dependent return value
 */

void *dummy_parameters (long function,void *value)
{
  return value;
}

/* Dummy scan mailboxes
 * Accepts: mail stream
 *	    reference
 *	    pattern to search
 *	    string to scan
 */

void dummy_scan (MAILSTREAM *stream,char *ref,char *pat,char *contents)
{
				/* return silently */
}

/* Dummy list mailboxes
 * Accepts: mail stream
 *	    reference
 *	    pattern to search
 */

void dummy_list (MAILSTREAM *stream,char *ref,char *pat)
{
				/* return silently */
}


/* Dummy list subscribed mailboxes
 * Accepts: mail stream
 *	    pattern to search
 */

void dummy_lsub (MAILSTREAM *stream,char *ref,char *pat)
{
				/* return silently */
}

/* Dummy create mailbox
 * Accepts: mail stream
 *	    mailbox name to create
 * Returns: T on success, NIL on failure
 */

long dummy_create (MAILSTREAM *stream,char *mailbox)
{
  return NIL;			/* always fails */
}


/* Dummy delete mailbox
 * Accepts: mail stream
 *	    mailbox name to delete
 * Returns: T on success, NIL on failure
 */

long dummy_delete (MAILSTREAM *stream,char *mailbox)
{
  return NIL;			/* always fails */
}


/* Mail rename mailbox
 * Accepts: mail stream
 *	    old mailbox name
 *	    new mailbox name
 * Returns: T on success, NIL on failure
 */

long dummy_rename (MAILSTREAM *stream,char *old,char *newname)
{
  return NIL;			/* always fails */
}

/* Dummy open
 * Accepts: stream to open
 * Returns: stream on success, NIL on failure
 */

MAILSTREAM *dummy_open (MAILSTREAM *stream)
{
  char tmp[MAILTMPLEN];
				/* OP_PROTOTYPE call or silence */
  if (!stream || stream->silent) return NIL;
  if (compare_cstring (stream->mailbox,"INBOX")) {
    sprintf (tmp,"Not a mailbox: %s",stream->mailbox);
    mm_log (tmp,ERROR);
    return NIL;			/* always fails */
  }
  if (!stream->silent) {	/* only if silence not requested */
    mail_exists (stream,0);	/* say there are 0 messages */
    mail_recent (stream,0);
    stream->uid_validity = time (0);
  }
  stream->inbox = T;		/* note that it's an INBOX */
  return stream;		/* return success */
}


/* Dummy close
 * Accepts: MAIL stream
 *	    options
 */

void dummy_close (MAILSTREAM *stream,long options)
{
				/* return silently */
}

/* Dummy ping mailbox
 * Accepts: MAIL stream
 * Returns: T if stream alive, else NIL
 * No-op for readonly files, since read/writer can expunge it from under us!
 */

long dummy_ping (MAILSTREAM *stream)
{
  return T;
}


/* Dummy check mailbox
 * Accepts: MAIL stream
 * No-op for readonly files, since read/writer can expunge it from under us!
 */

void dummy_check (MAILSTREAM *stream)
{
  dummy_ping (stream);		/* invoke ping */
}


/* Dummy expunge mailbox
 * Accepts: MAIL stream
 *	    sequence to expunge if non-NIL
 *	    expunge options
 * Returns: T, always
 */

long dummy_expunge (MAILSTREAM *stream,char *sequence,long options)
{
  return LONGT;
}

/* Dummy copy message(s)
 * Accepts: MAIL stream
 *	    sequence
 *	    destination mailbox
 *	    options
 * Returns: T if copy successful, else NIL
 */

long dummy_copy (MAILSTREAM *stream,char *sequence,char *mailbox,long options)
{
  if ((options & CP_UID) ? mail_uid_sequence (stream,sequence) :
      mail_sequence (stream,sequence)) fatal ("Impossible dummy_copy");
  return NIL;
}


/* Dummy append message string
 * Accepts: mail stream
 *	    destination mailbox
 *	    stringstruct of message to append
 * Returns: T on success, NIL on failure
 */

long dummy_append (MAILSTREAM *stream,char *mailbox,append_t af,void *data)
{
  char tmp[MAILTMPLEN];
  sprintf (tmp,"Can't append to %s",mailbox);
  mm_log (tmp,ERROR);		/* pass up error */
  return NIL;			/* always fails */
}
