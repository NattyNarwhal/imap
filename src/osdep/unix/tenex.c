/* ========================================================================
 * Copyright 1988-2007 University of Washington
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
 * Program:	Tenex mail routines
 *
 * Author:	Mark Crispin
 *		Networks and Distributed Computing
 *		Computing & Communications
 *		University of Washington
 *		Administration Building, AG-44
 *		Seattle, WA  98195
 *		Internet: MRC@CAC.Washington.EDU
 *
 * Date:	22 May 1990
 * Last Edited:	11 October 2007
 */


/*				FILE TIME SEMANTICS
 *
 * The atime is the last read time of the file.
 * The mtime is the last flags update time of the file.
 * The ctime is the last write time of the file.
 *
 *				TEXT SIZE SEMANTICS
 *
 * Most of the text sizes are in internal (LF-only) form, except for the
 * msg.text size.  Beware.
 */

#include <stdio.h>
#include <ctype.h>
#include <errno.h>
extern int errno;		/* just in case */
#include "mail.h"
#include "osdep.h"
#include <sys/stat.h>
#include "misc.h"
#include "dummy.h"

/* TENEX I/O stream local data */
	
typedef struct tenex_local {
  unsigned int shouldcheck: 1;	/* if ping should do a check instead */
  unsigned int mustcheck: 1;	/* if ping must do a check instead */
  int fd;			/* file descriptor for I/O */
  off_t filesize;		/* file size parsed */
  time_t filetime;		/* last file time */
  time_t lastsnarf;		/* local snarf time */
  unsigned char *buf;		/* temporary buffer */
  unsigned long buflen;		/* current size of temporary buffer */
  unsigned long uid;		/* current text uid */
  SIZEDTEXT text;		/* current text */
} TENEXLOCAL;


/* Convenient access to local data */

#define LOCAL ((TENEXLOCAL *) stream->local)


/* Function prototypes */

DRIVER *tenex_valid (char *name);
int tenex_isvalid (char *name,char *tmp);
void *tenex_parameters (long function,void *value);
void tenex_scan (MAILSTREAM *stream,char *ref,char *pat,char *contents);
void tenex_list (MAILSTREAM *stream,char *ref,char *pat);
void tenex_lsub (MAILSTREAM *stream,char *ref,char *pat);
long tenex_create (MAILSTREAM *stream,char *mailbox);
long tenex_delete (MAILSTREAM *stream,char *mailbox);
long tenex_rename (MAILSTREAM *stream,char *old,char *newname);
long tenex_status (MAILSTREAM *stream,char *mbx,long flags);
MAILSTREAM *tenex_open (MAILSTREAM *stream);
void tenex_close (MAILSTREAM *stream,long options);
void tenex_fast (MAILSTREAM *stream,char *sequence,long flags);
void tenex_flags (MAILSTREAM *stream,char *sequence,long flags);
char *tenex_header (MAILSTREAM *stream,unsigned long msgno,
		    unsigned long *length,long flags);
long tenex_text (MAILSTREAM *stream,unsigned long msgno,STRING *bs,long flags);
void tenex_flag (MAILSTREAM *stream,char *sequence,char *flag,long flags);
void tenex_flagmsg (MAILSTREAM *stream,MESSAGECACHE *elt);
long tenex_ping (MAILSTREAM *stream);
void tenex_check (MAILSTREAM *stream);
void tenex_snarf (MAILSTREAM *stream);
long tenex_expunge (MAILSTREAM *stream,char *sequence,long options);
long tenex_copy (MAILSTREAM *stream,char *sequence,char *mailbox,long options);
long tenex_append (MAILSTREAM *stream,char *mailbox,append_t af,void *data);

unsigned long tenex_size (MAILSTREAM *stream,unsigned long m);
char *tenex_file (char *dst,char *name);
long tenex_parse (MAILSTREAM *stream);
MESSAGECACHE *tenex_elt (MAILSTREAM *stream,unsigned long msgno);
void tenex_read_flags (MAILSTREAM *stream,MESSAGECACHE *elt);
void tenex_update_status (MAILSTREAM *stream,unsigned long msgno,
			  long syncflag);
unsigned long tenex_hdrpos (MAILSTREAM *stream,unsigned long msgno,
			    unsigned long *size);

/* Tenex mail routines */


/* Driver dispatch used by MAIL */

DRIVER tenexdriver = {
  "tenex",			/* driver name */
  DR_LOCAL|DR_MAIL|DR_NOSTICKY|DR_LOCKING,
				/* driver flags */
  (DRIVER *) NIL,		/* next driver */
  tenex_valid,			/* mailbox is valid for us */
  tenex_parameters,		/* manipulate parameters */
  tenex_scan,			/* scan mailboxes */
  tenex_list,			/* list mailboxes */
  tenex_lsub,			/* list subscribed mailboxes */
  NIL,				/* subscribe to mailbox */
  NIL,				/* unsubscribe from mailbox */
  dummy_create,			/* create mailbox */
  tenex_delete,			/* delete mailbox */
  tenex_rename,			/* rename mailbox */
  tenex_status,			/* status of mailbox */
  tenex_open,			/* open mailbox */
  tenex_close,			/* close mailbox */
  tenex_fast,			/* fetch message "fast" attributes */
  tenex_flags,			/* fetch message flags */
  NIL,				/* fetch overview */
  NIL,				/* fetch message envelopes */
  tenex_header,			/* fetch message header */
  tenex_text,			/* fetch message body */
  NIL,				/* fetch partial message text */
  NIL,				/* unique identifier */
  NIL,				/* message number */
  tenex_flag,			/* modify flags */
  tenex_flagmsg,		/* per-message modify flags */
  NIL,				/* search for message based on criteria */
  NIL,				/* sort messages */
  NIL,				/* thread messages */
  tenex_ping,			/* ping mailbox to see if still alive */
  tenex_check,			/* check for new messages */
  tenex_expunge,		/* expunge deleted messages */
  tenex_copy,			/* copy messages to another mailbox */
  tenex_append,			/* append string message to mailbox */
  NIL				/* garbage collect stream */
};

				/* prototype stream */
MAILSTREAM tenexproto = {&tenexdriver};

/* Tenex mail validate mailbox
 * Accepts: mailbox name
 * Returns: our driver if name is valid, NIL otherwise
 */

DRIVER *tenex_valid (char *name)
{
  char tmp[MAILTMPLEN];
  return tenex_isvalid (name,tmp) ? &tenexdriver : NIL;
}


/* Tenex mail test for valid mailbox
 * Accepts: mailbox name
 * Returns: T if valid, NIL otherwise
 */

int tenex_isvalid (char *name,char *tmp)
{
  int fd;
  int ret = NIL;
  char *s,file[MAILTMPLEN];
  struct stat sbuf;
  time_t tp[2];
  errno = EINVAL;		/* assume invalid argument */
				/* if file, get its status */
  if ((s = tenex_file (file,name)) && !stat (s,&sbuf)) {
    if (!sbuf.st_size) {	/* allow empty file if INBOX */
      if ((s = mailboxfile (tmp,name)) && !*s) ret = T;
      else errno = 0;		/* empty file */
    }
    else if ((fd = open (file,O_RDONLY,NIL)) >= 0) {
      memset (tmp,'\0',MAILTMPLEN);
      if ((read (fd,tmp,64) >= 0) && (s = strchr (tmp,'\012')) &&
	  (s[-1] != '\015')) {	/* valid format? */
	*s = '\0';		/* tie off header */
				/* must begin with dd-mmm-yy" */
	ret = (((tmp[2] == '-' && tmp[6] == '-') ||
		(tmp[1] == '-' && tmp[5] == '-')) &&
	       (s = strchr (tmp+18,',')) && strchr (s+2,';')) ? T : NIL;
      }
      else errno = -1;		/* bogus format */
      close (fd);		/* close the file */
				/* \Marked status? */
      if (sbuf.st_ctime > sbuf.st_atime) {
	tp[0] = sbuf.st_atime;	/* preserve atime and mtime */
	tp[1] = sbuf.st_mtime;
	utime (file,tp);	/* set the times */
      }
    }
  }
				/* in case INBOX but not tenex format */
  else if ((errno == ENOENT) && !compare_cstring (name,"INBOX")) errno = -1;
  return ret;			/* return what we should */
}

/* Tenex manipulate driver parameters
 * Accepts: function code
 *	    function-dependent value
 * Returns: function-dependent return value
 */

void *tenex_parameters (long function,void *value)
{
  void *ret = NIL;
  switch ((int) function) {
  case GET_INBOXPATH:
    if (value) ret = tenex_file ((char *) value,"INBOX");
    break;
  }
  return ret;
}


/* Tenex mail scan mailboxes
 * Accepts: mail stream
 *	    reference
 *	    pattern to search
 *	    string to scan
 */

void tenex_scan (MAILSTREAM *stream,char *ref,char *pat,char *contents)
{
  if (stream) dummy_scan (NIL,ref,pat,contents);
}


/* Tenex mail list mailboxes
 * Accepts: mail stream
 *	    reference
 *	    pattern to search
 */

void tenex_list (MAILSTREAM *stream,char *ref,char *pat)
{
  if (stream) dummy_list (NIL,ref,pat);
}


/* Tenex mail list subscribed mailboxes
 * Accepts: mail stream
 *	    reference
 *	    pattern to search
 */

void tenex_lsub (MAILSTREAM *stream,char *ref,char *pat)
{
  if (stream) dummy_lsub (NIL,ref,pat);
}

/* Tenex mail delete mailbox
 * Accepts: MAIL stream
 *	    mailbox name to delete
 * Returns: T on success, NIL on failure
 */

long tenex_delete (MAILSTREAM *stream,char *mailbox)
{
  return tenex_rename (stream,mailbox,NIL);
}


/* Tenex mail rename mailbox
 * Accepts: MAIL stream
 *	    old mailbox name
 *	    new mailbox name (or NIL for delete)
 * Returns: T on success, NIL on failure
 */

long tenex_rename (MAILSTREAM *stream,char *old,char *newname)
{
  long ret = T;
  char c,*s,tmp[MAILTMPLEN],file[MAILTMPLEN],lock[MAILTMPLEN];
  int fd,ld;
  struct stat sbuf;
  if (!tenex_file (file,old) ||
      (newname && (!((s = mailboxfile (tmp,newname)) && *s) ||
		   ((s = strrchr (tmp,'/')) && !s[1])))) {
    sprintf (tmp,newname ?
	     "Can't rename mailbox %.80s to %.80s: invalid name" :
	     "Can't delete mailbox %.80s: invalid name",
	     old,newname);
    MM_LOG (tmp,ERROR);
    return NIL;
  }
  else if ((fd = open (file,O_RDWR,NIL)) < 0) {
    sprintf (tmp,"Can't open mailbox %.80s: %s",old,strerror (errno));
    MM_LOG (tmp,ERROR);
    return NIL;
  }
				/* get exclusive parse/append permission */
  if ((ld = lockfd (fd,lock,LOCK_EX)) < 0) {
    MM_LOG ("Unable to lock rename mailbox",ERROR);
    return NIL;
  }
				/* lock out other users */
  if (flock (fd,LOCK_EX|LOCK_NB)) {
    close (fd);			/* couldn't lock, give up on it then */
    sprintf (tmp,"Mailbox %.80s is in use by another process",old);
    MM_LOG (tmp,ERROR);
    unlockfd (ld,lock);		/* release exclusive parse/append permission */
    return NIL;
  }

  if (newname) {		/* want rename? */
    if (s = strrchr (tmp,'/')) {/* found superior to destination name? */
      c = *++s;			/* remember first character of inferior */
      *s = '\0';		/* tie off to get just superior */
				/* name doesn't exist, create it */
      if ((stat (tmp,&sbuf) || ((sbuf.st_mode & S_IFMT) != S_IFDIR)) &&
	  !dummy_create_path (stream,tmp,get_dir_protection (newname)))
	ret = NIL;
      else *s = c;		/* restore full name */
    }
				/* rename the file */
    if (ret && rename (file,tmp)) {
      sprintf (tmp,"Can't rename mailbox %.80s to %.80s: %s",old,newname,
	       strerror (errno));
      MM_LOG (tmp,ERROR);
      ret = NIL;		/* set failure */
    }
  }
  else if (unlink (file)) {
    sprintf (tmp,"Can't delete mailbox %.80s: %s",old,strerror (errno));
    MM_LOG (tmp,ERROR);
    ret = NIL;			/* set failure */
  }
  flock (fd,LOCK_UN);		/* release lock on the file */
  close (fd);			/* close the file */
  unlockfd (ld,lock);		/* release exclusive parse/append permission */
				/* recreate file if renamed INBOX */
  if (ret && !compare_cstring (old,"INBOX")) dummy_create (NIL,"mail.txt");
  return ret;			/* return success */
}

/* Tenex Mail status
 * Accepts: mail stream
 *	    mailbox name
 *	    status flags
 * Returns: T on success, NIL on failure
 */

long tenex_status (MAILSTREAM *stream,char *mbx,long flags)
{
  MAILSTATUS status;
  unsigned long i;
  MAILSTREAM *tstream = NIL;
  MAILSTREAM *systream = NIL;
				/* make temporary stream (unless this mbx) */
  if (!stream && !(stream = tstream =
		   mail_open (NIL,mbx,OP_READONLY|OP_SILENT))) return NIL;
  status.flags = flags;		/* return status values */
  status.messages = stream->nmsgs;
  status.recent = stream->recent;
  if (flags & SA_UNSEEN)	/* must search to get unseen messages */
    for (i = 1,status.unseen = 0; i <= stream->nmsgs; i++)
      if (!mail_elt (stream,i)->seen) status.unseen++;
  status.uidnext = stream->uid_last + 1;
  status.uidvalidity = stream->uid_validity;
				/* calculate post-snarf results */
  if (!status.recent && stream->inbox &&
      (systream = mail_open (NIL,sysinbox (),OP_READONLY|OP_SILENT))) {
    status.messages += systream->nmsgs;
    status.recent += systream->recent;
    if (flags & SA_UNSEEN)	/* must search to get unseen messages */
      for (i = 1; i <= systream->nmsgs; i++)
	if (!mail_elt (systream,i)->seen) status.unseen++;
				/* kludge but probably good enough */
    status.uidnext += systream->nmsgs;
  }
  MM_STATUS(stream,mbx,&status);/* pass status to main program */
  if (tstream) mail_close (tstream);
  if (systream) mail_close (systream);
  return T;			/* success */
}

/* Tenex mail open
 * Accepts: stream to open
 * Returns: stream on success, NIL on failure
 */

MAILSTREAM *tenex_open (MAILSTREAM *stream)
{
  int fd,ld;
  char tmp[MAILTMPLEN];
  blocknotify_t bn = (blocknotify_t) mail_parameters (NIL,GET_BLOCKNOTIFY,NIL);
				/* return prototype for OP_PROTOTYPE call */
  if (!stream) return user_flags (&tenexproto);
  if (stream->local) fatal ("tenex recycle stream");
  user_flags (stream);		/* set up user flags */
				/* canonicalize the mailbox name */
  if (!tenex_file (tmp,stream->mailbox)) {
    sprintf (tmp,"Can't open - invalid name: %.80s",stream->mailbox);
    MM_LOG (tmp,ERROR);
  }
  if (stream->rdonly ||
      (fd = open (tmp,O_RDWR,NIL)) < 0) {
    if ((fd = open (tmp,O_RDONLY,NIL)) < 0) {
      sprintf (tmp,"Can't open mailbox: %s",strerror (errno));
      MM_LOG (tmp,ERROR);
      return NIL;
    }
    else if (!stream->rdonly) {	/* got it, but readonly */
      MM_LOG ("Can't get write access to mailbox, access is readonly",WARN);
      stream->rdonly = T;
    }
  }
  stream->local = fs_get (sizeof (TENEXLOCAL));
  LOCAL->buf = (char *) fs_get (CHUNKSIZE);
  LOCAL->buflen = CHUNKSIZE - 1;
  LOCAL->text.data = (unsigned char *) fs_get (CHUNKSIZE);
  LOCAL->text.size = CHUNKSIZE - 1;

				/* note if an INBOX or not */
  stream->inbox = !compare_cstring (stream->mailbox,"INBOX");
  LOCAL->fd = fd;		/* bind the file */
				/* flush old name */
  fs_give ((void **) &stream->mailbox);
				/* save canonical name */
  stream->mailbox = cpystr (tmp);
				/* get shared parse permission */
  if ((ld = lockfd (fd,tmp,LOCK_SH)) < 0) {
    MM_LOG ("Unable to lock open mailbox",ERROR);
    return NIL;
  }
  (*bn) (BLOCK_FILELOCK,NIL);
  flock (LOCAL->fd,LOCK_SH);	/* lock the file */
  (*bn) (BLOCK_NONE,NIL);
  unlockfd (ld,tmp);		/* release shared parse permission */
  LOCAL->filesize = 0;		/* initialize parsed file size */
				/* time not set up yet */
  LOCAL->lastsnarf = LOCAL->filetime = 0;
  LOCAL->mustcheck = LOCAL->shouldcheck = NIL;
  stream->sequence++;		/* bump sequence number */
				/* parse mailbox */
  stream->nmsgs = stream->recent = 0;
  if (tenex_ping (stream) && !stream->nmsgs)
    MM_LOG ("Mailbox is empty",(long) NIL);
  if (!LOCAL) return NIL;	/* failure if stream died */
  stream->perm_seen = stream->perm_deleted =
    stream->perm_flagged = stream->perm_answered = stream->perm_draft =
      stream->rdonly ? NIL : T;
  stream->perm_user_flags = stream->rdonly ? NIL : 0xffffffff;
  return stream;		/* return stream to caller */
}

/* Tenex mail close
 * Accepts: MAIL stream
 *	    close options
 */

void tenex_close (MAILSTREAM *stream,long options)
{
  if (stream && LOCAL) {	/* only if a file is open */
    int silent = stream->silent;
    stream->silent = T;		/* note this stream is dying */
    if (options & CL_EXPUNGE) tenex_expunge (stream,NIL,NIL);
    stream->silent = silent;	/* restore previous status */
    flock (LOCAL->fd,LOCK_UN);	/* unlock local file */
    close (LOCAL->fd);		/* close the local file */
				/* free local text buffer */
    if (LOCAL->buf) fs_give ((void **) &LOCAL->buf);
    if (LOCAL->text.data) fs_give ((void **) &LOCAL->text.data);
				/* nuke the local data */
    fs_give ((void **) &stream->local);
    stream->dtb = NIL;		/* log out the DTB */
  }
}

/* Tenex mail fetch fast data
 * Accepts: MAIL stream
 *	    sequence
 *	    option flags
 */

void tenex_fast (MAILSTREAM *stream,char *sequence,long flags)
{
  STRING bs;
  MESSAGECACHE *elt;
  unsigned long i;
  if (stream && LOCAL &&
      ((flags & FT_UID) ? mail_uid_sequence (stream,sequence) :
       mail_sequence (stream,sequence)))
    for (i = 1; i <= stream->nmsgs; i++)
      if ((elt = mail_elt (stream,i))->sequence) {
	if (!elt->rfc822_size) { /* have header size yet? */
	  lseek (LOCAL->fd,elt->private.special.offset +
		 elt->private.special.text.size,L_SET);
				/* resize bigbuf if necessary */
	  if (LOCAL->buflen < elt->private.msg.full.text.size) {
	    fs_give ((void **) &LOCAL->buf);
	    LOCAL->buflen = elt->private.msg.full.text.size;
	    LOCAL->buf = (char *) fs_get (LOCAL->buflen + 1);
	  }
				/* tie off string */
	  LOCAL->buf[elt->private.msg.full.text.size] = '\0';
				/* read in the message */
	  read (LOCAL->fd,LOCAL->buf,elt->private.msg.full.text.size);
	  INIT (&bs,mail_string,(void *) LOCAL->buf,
		elt->private.msg.full.text.size);
				/* calculate its CRLF size */
	  elt->rfc822_size = strcrlflen (&bs);
	}
	tenex_elt (stream,i);	/* get current flags from file */
      }
}


/* Tenex mail fetch flags
 * Accepts: MAIL stream
 *	    sequence
 *	    option flags
 * Sniffs at file to get flags
 */

void tenex_flags (MAILSTREAM *stream,char *sequence,long flags)
{
  unsigned long i;
  if (stream && LOCAL &&
      ((flags & FT_UID) ? mail_uid_sequence (stream,sequence) :
       mail_sequence (stream,sequence)))
    for (i = 1; i <= stream->nmsgs; i++)
      if (mail_elt (stream,i)->sequence) tenex_elt (stream,i);
}

/* TENEX mail fetch message header
 * Accepts: MAIL stream
 *	    message # to fetch
 *	    pointer to returned header text length
 *	    option flags
 * Returns: message header in RFC822 format
 */

char *tenex_header (MAILSTREAM *stream,unsigned long msgno,
		    unsigned long *length,long flags)
{
  char *s;
  unsigned long i;
  *length = 0;			/* default to empty */
  if (flags & FT_UID) return "";/* UID call "impossible" */
				/* get to header position */
  lseek (LOCAL->fd,tenex_hdrpos (stream,msgno,&i),L_SET);
  if (flags & FT_INTERNAL) {
    if (i > LOCAL->buflen) {	/* resize if not enough space */
      fs_give ((void **) &LOCAL->buf);
      LOCAL->buf = (char *) fs_get (LOCAL->buflen = i + 1);
    }
				/* slurp the data */
    read (LOCAL->fd,LOCAL->buf,*length = i);
  }
  else {
    s = (char *) fs_get (i + 1);/* get readin buffer */
    s[i] = '\0';		/* tie off string */
    read (LOCAL->fd,s,i);	/* slurp the data */
				/* make CRLF copy of string */
    *length = strcrlfcpy (&LOCAL->buf,&LOCAL->buflen,s,i);
    fs_give ((void **) &s);	/* free readin buffer */
  }
  return (char *) LOCAL->buf;
}

/* TENEX mail fetch message text (body only)
 * Accepts: MAIL stream
 *	    message # to fetch
 *	    pointer to returned stringstruct
 *	    option flags
 * Returns: T, always
 */

long tenex_text (MAILSTREAM *stream,unsigned long msgno,STRING *bs,long flags)
{
  char *s;
  unsigned long i,j;
  MESSAGECACHE *elt;
				/* UID call "impossible" */
  if (flags & FT_UID) return NIL;
				/* get message status */
  elt = tenex_elt (stream,msgno);
				/* if message not seen */
  if (!(flags & FT_PEEK) && !elt->seen) {
    elt->seen = T;		/* mark message as seen */
				/* recalculate status */
    tenex_update_status (stream,msgno,T);
    MM_FLAGS (stream,msgno);
  }
  if (flags & FT_INTERNAL) {	/* if internal representation wanted */
				/* find header position */
    i = tenex_hdrpos (stream,msgno,&j);
    if (i > LOCAL->buflen) {	/* resize if not enough space */
      fs_give ((void **) &LOCAL->buf);
      LOCAL->buf = (char *) fs_get (LOCAL->buflen = i + 1);
    }
				/* go to text position */
    lseek (LOCAL->fd,i + j,L_SET);
				/* slurp the data */
    read (LOCAL->fd,LOCAL->buf,i);
				/* set up stringstruct for internal */
    INIT (bs,mail_string,LOCAL->buf,i);
  }
  else {			/* normal form, previous text cached? */
    if (elt->private.uid == LOCAL->uid)
      i = elt->private.msg.text.text.size;
    else {			/* not cached, cache it now */
      LOCAL->uid = elt->private.uid;
				/* find header position */
      i = tenex_hdrpos (stream,msgno,&j);
				/* go to text position */
      lseek (LOCAL->fd,i + j,L_SET);
      s = (char *) fs_get ((i = tenex_size (stream,msgno) - j) + 1);
      s[i] = '\0';		/* tie off string */
      read (LOCAL->fd,s,i);	/* slurp the data */
				/* make CRLF copy of string */
      i = elt->private.msg.text.text.size =
	strcrlfcpy (&LOCAL->text.data,&LOCAL->text.size,s,i);
      fs_give ((void **) &s);	/* free readin buffer */
    }
				/* set up stringstruct */
    INIT (bs,mail_string,LOCAL->text.data,i);
  }
  return T;			/* success */
}

/* Tenex mail modify flags
 * Accepts: MAIL stream
 *	    sequence
 *	    flag(s)
 *	    option flags
 */

void tenex_flag (MAILSTREAM *stream,char *sequence,char *flag,long flags)
{
  time_t tp[2];
  struct stat sbuf;
  if (!stream->rdonly) {	/* make sure the update takes */
    fsync (LOCAL->fd);
    fstat (LOCAL->fd,&sbuf);	/* get current write time */
    tp[1] = LOCAL->filetime = sbuf.st_mtime;
    tp[0] = time (0);		/* make sure read comes after all that */
    utime (stream->mailbox,tp);
  }
}


/* Tenex mail per-message modify flags
 * Accepts: MAIL stream
 *	    message cache element
 */

void tenex_flagmsg (MAILSTREAM *stream,MESSAGECACHE *elt)
{
  struct stat sbuf;
				/* maybe need to do a checkpoint? */
  if (LOCAL->filetime && !LOCAL->shouldcheck) {
    fstat (LOCAL->fd,&sbuf);	/* get current write time */
    if (LOCAL->filetime < sbuf.st_mtime) LOCAL->shouldcheck = T;
    LOCAL->filetime = 0;	/* don't do this test for any other messages */
  }
				/* recalculate status */
  tenex_update_status (stream,elt->msgno,NIL);
}

/* Tenex mail ping mailbox
 * Accepts: MAIL stream
 * Returns: T if stream still alive, NIL if not
 */

long tenex_ping (MAILSTREAM *stream)
{
  unsigned long i = 1;
  long r = T;
  int ld;
  char lock[MAILTMPLEN];
  struct stat sbuf;
  if (stream && LOCAL) {	/* only if stream already open */
    fstat (LOCAL->fd,&sbuf);	/* get current file poop */
    if (LOCAL->filetime && !(LOCAL->mustcheck || LOCAL->shouldcheck) &&
	(LOCAL->filetime < sbuf.st_mtime)) LOCAL->shouldcheck = T;
				/* check for changed message status */
    if (LOCAL->mustcheck || LOCAL->shouldcheck) {
      LOCAL->filetime = sbuf.st_mtime;
      if (LOCAL->shouldcheck)	/* babble when we do this unilaterally */
	MM_NOTIFY (stream,"[CHECK] Checking for flag updates",NIL);
      while (i <= stream->nmsgs) tenex_elt (stream,i++);
      LOCAL->mustcheck = LOCAL->shouldcheck = NIL;
    }
				/* get shared parse/append permission */
    if ((sbuf.st_size != LOCAL->filesize) &&
	((ld = lockfd (LOCAL->fd,lock,LOCK_SH)) >= 0)) {
				/* parse resulting mailbox */
      r = (tenex_parse (stream)) ? T : NIL;
      unlockfd (ld,lock);	/* release shared parse/append permission */
    }
    if (LOCAL) {		/* stream must still be alive */
				/* snarf if this is a read-write inbox */
      if (stream->inbox && !stream->rdonly) {
	tenex_snarf (stream);
	fstat (LOCAL->fd,&sbuf);/* see if file changed now */
	if ((sbuf.st_size != LOCAL->filesize) &&
	    ((ld = lockfd (LOCAL->fd,lock,LOCK_SH)) >= 0)) {
				/* parse resulting mailbox */
	  r = (tenex_parse (stream)) ? T : NIL;
	  unlockfd (ld,lock);	/* release shared parse/append permission */
	}
      }
    }
  }
  return r;			/* return result of the parse */
}


/* Tenex mail check mailbox (reparses status too)
 * Accepts: MAIL stream
 */

void tenex_check (MAILSTREAM *stream)
{
				/* mark that a check is desired */
  if (LOCAL) LOCAL->mustcheck = T;
  if (tenex_ping (stream)) MM_LOG ("Check completed",(long) NIL);
}

/* Tenex mail snarf messages from system inbox
 * Accepts: MAIL stream
 */

void tenex_snarf (MAILSTREAM *stream)
{
  unsigned long i = 0;
  unsigned long j,r,hdrlen,txtlen;
  struct stat sbuf;
  char *hdr,*txt,lock[MAILTMPLEN],tmp[MAILTMPLEN];
  MESSAGECACHE *elt;
  MAILSTREAM *sysibx = NIL;
  int ld;
				/* give up if can't get exclusive permission */
  if ((time (0) >= (LOCAL->lastsnarf +
		    (long) mail_parameters (NIL,GET_SNARFINTERVAL,NIL))) &&
      strcmp (sysinbox (),stream->mailbox) &&
      ((ld = lockfd (LOCAL->fd,lock,LOCK_EX)) >= 0)) {
    MM_CRITICAL (stream);	/* go critical */
				/* sizes match and anything in sysinbox? */
    if (!stat (sysinbox (),&sbuf) && sbuf.st_size &&
	!fstat (LOCAL->fd,&sbuf) && (sbuf.st_size == LOCAL->filesize) && 
	(sysibx = mail_open (sysibx,sysinbox (),OP_SILENT)) &&
	(!sysibx->rdonly) && (r = sysibx->nmsgs)) {
				/* yes, go to end of file in our mailbox */
      lseek (LOCAL->fd,sbuf.st_size,L_SET);
				/* for each message in sysibx mailbox */
      while (r && (++i <= sysibx->nmsgs)) {
				/* snarf message from system INBOX */
	hdr = cpystr (mail_fetchheader_full(sysibx,i,NIL,&hdrlen,FT_INTERNAL));
	txt = mail_fetchtext_full (sysibx,i,&txtlen,FT_INTERNAL|FT_PEEK);
				/* if have a message */
	if (j = hdrlen + txtlen) {
				/* calculate header line */
	  mail_date (LOCAL->buf,elt = mail_elt (sysibx,i));
	  sprintf (LOCAL->buf + strlen (LOCAL->buf),
		   ",%lu;0000000000%02o\n",j,(unsigned)
		   ((fSEEN * elt->seen) + (fDELETED * elt->deleted) +
		    (fFLAGGED * elt->flagged) + (fANSWERED * elt->answered) +
		    (fDRAFT * elt->draft)));
				/* copy message */
	  if ((write (LOCAL->fd,LOCAL->buf,strlen (LOCAL->buf)) < 0) ||
	      (write (LOCAL->fd,hdr,hdrlen) < 0) ||
	      (write (LOCAL->fd,txt,txtlen) < 0)) r = 0;
	}
	fs_give ((void **) &hdr);
      }

				/* make sure all the updates take */
      if (fsync (LOCAL->fd)) r = 0;
      if (r) {			/* delete all the messages we copied */
	if (r == 1) strcpy (tmp,"1");
	else sprintf (tmp,"1:%lu",r);
	mail_flag (sysibx,tmp,"\\Deleted",ST_SET);
	mail_expunge (sysibx);	/* now expunge all those messages */
      }
      else {
	sprintf (LOCAL->buf,"Can't copy new mail: %s",strerror (errno));
	MM_LOG (LOCAL->buf,WARN);
	ftruncate (LOCAL->fd,sbuf.st_size);
      }
      fstat (LOCAL->fd,&sbuf);	/* yes, get current file size */
      LOCAL->filetime = sbuf.st_mtime;
    }
    if (sysibx) mail_close (sysibx);
    MM_NOCRITICAL (stream);	/* release critical */
    unlockfd (ld,lock);		/* release exclusive parse/append permission */
    LOCAL->lastsnarf = time (0);/* note time of last snarf */
  }
}

/* Tenex mail expunge mailbox
 * Accepts: MAIL stream
 *	    sequence to expunge if non-NIL
 *	    expunge options
 * Returns: T, always
 */

long tenex_expunge (MAILSTREAM *stream,char *sequence,long options)
{
  long ret;
  time_t tp[2];
  struct stat sbuf;
  off_t pos = 0;
  int ld;
  unsigned long i = 1;
  unsigned long j,k,m,recent;
  unsigned long n = 0;
  unsigned long delta = 0;
  char lock[MAILTMPLEN];
  MESSAGECACHE *elt;
  blocknotify_t bn = (blocknotify_t) mail_parameters (NIL,GET_BLOCKNOTIFY,NIL);
  if (!(ret = (sequence ? ((options & EX_UID) ?
			   mail_uid_sequence (stream,sequence) :
			   mail_sequence (stream,sequence)) : LONGT) &&
	tenex_ping (stream)));	/* parse sequence if given, ping stream */
  else if (stream->rdonly) MM_LOG ("Expunge ignored on readonly mailbox",WARN);
  else {
    if (LOCAL->filetime && !LOCAL->shouldcheck) {
      fstat (LOCAL->fd,&sbuf);	/* get current write time */
      if (LOCAL->filetime < sbuf.st_mtime) LOCAL->shouldcheck = T;
    }
  /* The cretins who designed flock() created a window of vulnerability in
   * upgrading locks from shared to exclusive or downgrading from exclusive
   * to shared.  Rather than maintain the lock at shared status at a minimum,
   * flock() actually *releases* the former lock.  Obviously they never talked
   * to any database guys.  Fortunately, we have the parse/append permission
   * lock.  If we require this lock before going exclusive on the mailbox,
   * another process can not sneak in and steal the exclusive mailbox lock on
   * us, because it will block on trying to get parse/append permission first.
   */
				/* get exclusive parse/append permission */
    if ((ld = lockfd (LOCAL->fd,lock,LOCK_EX)) < 0)
      MM_LOG ("Unable to lock expunge mailbox",ERROR);
				/* make sure see any newly-arrived messages */
    else if (!tenex_parse (stream));
				/* get exclusive access */
    else if (flock (LOCAL->fd,LOCK_EX|LOCK_NB)) {
      (*bn) (BLOCK_FILELOCK,NIL);
      flock (LOCAL->fd,LOCK_SH);/* recover previous lock */
      (*bn) (BLOCK_NONE,NIL);
      MM_LOG ("Can't expunge because mailbox is in use by another process",
	      ERROR);
      unlockfd (ld,lock);	/* release exclusive parse/append permission */
    }

    else {
      MM_CRITICAL (stream);	/* go critical */
      recent = stream->recent;	/* get recent now that pinged and locked */
				/* for each message */
      while (i <= stream->nmsgs) {
				/* get cache element */
	elt = tenex_elt (stream,i);
				/* number of bytes to smash or preserve */
	k = elt->private.special.text.size + tenex_size (stream,i);
				/* if need to expunge this message */
	if (elt->deleted && (sequence ? elt->sequence : T)) {
				/* if recent, note one less recent message */
	  if (elt->recent) --recent;
	  delta += k;		/* number of bytes to delete */
				/* notify upper levels */
	  mail_expunged (stream,i);
	  n++;			/* count up one more expunged message */
	}
	else if (i++ && delta) {/* preserved message */
				/* first byte to preserve */
	  j = elt->private.special.offset;
	  do {			/* read from source position */
	    m = min (k,LOCAL->buflen);
	    lseek (LOCAL->fd,j,L_SET);
	    read (LOCAL->fd,LOCAL->buf,m);
	    pos = j - delta;	/* write to destination position */
	    lseek (LOCAL->fd,pos,L_SET);
	    while (T) {
	      lseek (LOCAL->fd,pos,L_SET);
	      if (write (LOCAL->fd,LOCAL->buf,m) > 0) break;
	      MM_NOTIFY (stream,strerror (errno),WARN);
	      MM_DISKERROR (stream,errno,T);
	    }
	    pos += m;		/* new position */
	    j += m;		/* next chunk, perhaps */
	  } while (k -= m);		/* until done */
				/* note the new address of this text */
	  elt->private.special.offset -= delta;
	}
				/* preserved but no deleted messages */
	else pos = elt->private.special.offset + k;
      }

      if (n) {			/* truncate file after last message */
	if (pos != (LOCAL->filesize -= delta)) {
	  sprintf (LOCAL->buf,
		   "Calculated size mismatch %lu != %lu, delta = %lu",
		   (unsigned long) pos,(unsigned long) LOCAL->filesize,delta);
	  MM_LOG (LOCAL->buf,WARN);
	  LOCAL->filesize = pos;/* fix it then */
	}
	ftruncate (LOCAL->fd,LOCAL->filesize);
	sprintf (LOCAL->buf,"Expunged %lu messages",n);
				/* output the news */
	MM_LOG (LOCAL->buf,(long) NIL);
      }
      else MM_LOG ("No messages deleted, so no update needed",(long) NIL);
      fsync (LOCAL->fd);		/* force disk update */
      fstat (LOCAL->fd,&sbuf);	/* get new write time */
      tp[1] = LOCAL->filetime = sbuf.st_mtime;
      tp[0] = time (0);		/* reset atime to now */
      utime (stream->mailbox,tp);
      MM_NOCRITICAL (stream);	/* release critical */
				/* notify upper level of new mailbox size */
      mail_exists (stream,stream->nmsgs);
      mail_recent (stream,recent);
      (*bn) (BLOCK_FILELOCK,NIL);
      flock (LOCAL->fd,LOCK_SH);/* allow sharers again */
      (*bn) (BLOCK_NONE,NIL);
      unlockfd (ld,lock);	/* release exclusive parse/append permission */
    }
  }
  return LONGT;
}

/* Tenex mail copy message(s)
 * Accepts: MAIL stream
 *	    sequence
 *	    destination mailbox
 *	    copy options
 * Returns: T if success, NIL if failed
 */

long tenex_copy (MAILSTREAM *stream,char *sequence,char *mailbox,long options)
{
  struct stat sbuf;
  time_t tp[2];
  MESSAGECACHE *elt;
  unsigned long i,j,k;
  long ret = LONGT;
  int fd,ld;
  char file[MAILTMPLEN],lock[MAILTMPLEN];
  mailproxycopy_t pc =
    (mailproxycopy_t) mail_parameters (stream,GET_MAILPROXYCOPY,NIL);
				/* make sure valid mailbox */
  if (!tenex_isvalid (mailbox,LOCAL->buf)) switch (errno) {
  case ENOENT:			/* no such file? */
    MM_NOTIFY (stream,"[TRYCREATE] Must create mailbox before copy",NIL);
    return NIL;
  case 0:			/* merely empty file? */
    break;
  case EACCES:			/* file protected */
    sprintf (LOCAL->buf,"Can't access destination: %.80s",mailbox);
    MM_LOG (LOCAL->buf,ERROR);
    return NIL;
  case EINVAL:
    if (pc) return (*pc) (stream,sequence,mailbox,options);
    sprintf (LOCAL->buf,"Invalid Tenex-format mailbox name: %.80s",mailbox);
    MM_LOG (LOCAL->buf,ERROR);
    return NIL;
  default:
    if (pc) return (*pc) (stream,sequence,mailbox,options);
    sprintf (LOCAL->buf,"Not a Tenex-format mailbox: %.80s",mailbox);
    MM_LOG (LOCAL->buf,ERROR);
    return NIL;
  }
  if (!((options & CP_UID) ? mail_uid_sequence (stream,sequence) :
	mail_sequence (stream,sequence))) return NIL;
				/* got file? */  
  if ((fd = open (tenex_file(file,mailbox),O_RDWR,NIL)) < 0) {
    sprintf (LOCAL->buf,"Unable to open copy mailbox: %s",strerror (errno));
    MM_LOG (LOCAL->buf,ERROR);
    return NIL;
  }
  MM_CRITICAL (stream);		/* go critical */
				/* get exclusive parse/append permission */
  if (flock (fd,LOCK_SH) || ((ld = lockfd (fd,lock,LOCK_EX)) < 0)) {
    MM_LOG ("Unable to lock copy mailbox",ERROR);
    MM_NOCRITICAL (stream);
    return NIL;
  }
  fstat (fd,&sbuf);		/* get current file size */
  lseek (fd,sbuf.st_size,L_SET);/* move to end of file */

				/* for each requested message */
  for (i = 1; ret && (i <= stream->nmsgs); i++) 
    if ((elt = mail_elt (stream,i))->sequence) {
      lseek (LOCAL->fd,elt->private.special.offset,L_SET);
				/* number of bytes to copy */
      k = elt->private.special.text.size + tenex_size (stream,i);
      do {			/* read from source position */
	j = min (k,LOCAL->buflen);
	read (LOCAL->fd,LOCAL->buf,j);
	if (write (fd,LOCAL->buf,j) < 0) ret = NIL;
      } while (ret && (k -= j));/* until done */
    }
				/* make sure all the updates take */
  if (!(ret && (ret = !fsync (fd)))) {
    sprintf (LOCAL->buf,"Unable to write message: %s",strerror (errno));
    MM_LOG (LOCAL->buf,ERROR);
    ftruncate (fd,sbuf.st_size);
  }
  if (ret) tp[0] = time (0) - 1;/* set atime to now-1 if successful copy */
				/* else preserve \Marked status */
  else tp[0] = (sbuf.st_ctime > sbuf.st_atime) ? sbuf.st_atime : time(0);
  tp[1] = sbuf.st_mtime;	/* preserve mtime */
  utime (file,tp);		/* set the times */
  close (fd);			/* close the file */
  unlockfd (ld,lock);		/* release exclusive parse/append permission */
  MM_NOCRITICAL (stream);	/* release critical */
				/* delete all requested messages */
  if (ret && (options & CP_MOVE)) {
    for (i = 1; i <= stream->nmsgs; i++)
      if ((elt = tenex_elt (stream,i))->sequence) {
	elt->deleted = T;	/* mark message deleted */
				/* recalculate status */
	tenex_update_status (stream,i,NIL);
      }
    if (!stream->rdonly) {	/* make sure the update takes */
      fsync (LOCAL->fd);
      fstat (LOCAL->fd,&sbuf);	/* get current write time */
      tp[1] = LOCAL->filetime = sbuf.st_mtime;
      tp[0] = time (0);		/* make sure atime remains greater */
      utime (stream->mailbox,tp);
    }
  }
  if (ret && mail_parameters (NIL,GET_COPYUID,NIL))
    MM_LOG ("Can not return meaningful COPYUID with this mailbox format",WARN);
  return ret;
}

/* Tenex mail append message from stringstruct
 * Accepts: MAIL stream
 *	    destination mailbox
 *	    append callback
 *	    data for callback
 * Returns: T if append successful, else NIL
 */

long tenex_append (MAILSTREAM *stream,char *mailbox,append_t af,void *data)
{
  struct stat sbuf;
  int fd,ld,c;
  char *flags,*date,tmp[MAILTMPLEN],file[MAILTMPLEN],lock[MAILTMPLEN];
  time_t tp[2];
  FILE *df;
  MESSAGECACHE elt;
  long f;
  unsigned long i,j,uf,size;
  STRING *message;
  long ret = LONGT;
				/* default stream to prototype */
  if (!stream) stream = user_flags (&tenexproto);
				/* make sure valid mailbox */
  if (!tenex_isvalid (mailbox,tmp)) switch (errno) {
  case ENOENT:			/* no such file? */
    if (!compare_cstring (mailbox,"INBOX")) dummy_create (NIL,"mail.txt");
    else {
      MM_NOTIFY (stream,"[TRYCREATE] Must create mailbox before append",NIL);
      return NIL;
    }
				/* falls through */
  case 0:			/* merely empty file? */
    break;
  case EACCES:			/* file protected */
    sprintf (tmp,"Can't access destination: %.80s",mailbox);
    MM_LOG (tmp,ERROR);
    return NIL;
  case EINVAL:
    sprintf (tmp,"Invalid TENEX-format mailbox name: %.80s",mailbox);
    MM_LOG (tmp,ERROR);
    return NIL;
  default:
    sprintf (tmp,"Not a TENEX-format mailbox: %.80s",mailbox);
    MM_LOG (tmp,ERROR);
    return NIL;
  }
				/* get first message */
  if (!MM_APPEND (af) (stream,data,&flags,&date,&message)) return NIL;

				/* open destination mailbox */
  if (((fd = open (tenex_file (file,mailbox),O_WRONLY|O_APPEND,NIL)) < 0) ||
      !(df = fdopen (fd,"ab"))) {
    sprintf (tmp,"Can't open append mailbox: %s",strerror (errno));
    MM_LOG (tmp,ERROR);
    return NIL;
  }
				/* get parse/append permission */
  if (flock (fd,LOCK_SH) || ((ld = lockfd (fd,lock,LOCK_EX)) < 0)) {
    MM_LOG ("Unable to lock append mailbox",ERROR);
    close (fd);
    return NIL;
  }
  MM_CRITICAL (stream);		/* go critical */
  fstat (fd,&sbuf);		/* get current file size */
  errno = 0;
  do {				/* parse flags */ 
    if (!SIZE (message)) {	/* guard against zero-length */
      MM_LOG ("Append of zero-length message",ERROR);
      ret = NIL;
      break;
    }
    f = mail_parse_flags (stream,flags,&i);
				/* reverse bits (dontcha wish we had CIRC?) */
    for (uf = 0; i; uf |= 1 << (29 - find_rightmost_bit (&i)));
    if (date) {			/* parse date if given */
      if (!mail_parse_date (&elt,date)) {
	sprintf (tmp,"Bad date in append: %.80s",date);
	MM_LOG (tmp,ERROR);
	ret = NIL;		/* mark failure */
	break;
      }
      mail_date (tmp,&elt);	/* write preseved date */
    }
    else internal_date (tmp);	/* get current date in IMAP format */
    i = GETPOS (message);	/* remember current position */
    for (j = SIZE (message), size = 0; j; --j)
      if (SNX (message) != '\015') ++size;
    SETPOS (message,i);		/* restore position */
				/* write header */
    if (fprintf (df,"%s,%lu;%010lo%02lo\n",tmp,size,uf,(unsigned long) f) < 0)
      ret = NIL;
    else {			/* write message */
      while (size) if ((c = 0xff & SNX (message)) != '\015') {
	if (putc (c,df) != EOF) --size;
	else break;
      }
				/* get next message */
      if (size || !MM_APPEND (af) (stream,data,&flags,&date,&message))
	ret = NIL;
    }
  } while (ret && message);
				/* if error... */
  if (!ret || (fflush (df) == EOF)) {
    ftruncate (fd,sbuf.st_size);/* revert file */
    close (fd);			/* make sure fclose() doesn't corrupt us */
    if (errno) {
      sprintf (tmp,"Message append failed: %s",strerror (errno));
      MM_LOG (tmp,ERROR);
    }
    ret = NIL;
  }
  if (ret) tp[0] = time (0) - 1;/* set atime to now-1 if successful copy */
				/* else preserve \Marked status */
  else tp[0] = (sbuf.st_ctime > sbuf.st_atime) ? sbuf.st_atime : time(0);
  tp[1] = sbuf.st_mtime;	/* preserve mtime */
  utime (file,tp);		/* set the times */
  fclose (df);			/* close the file */
  unlockfd (ld,lock);		/* release exclusive parse/append permission */
  MM_NOCRITICAL (stream);	/* release critical */
  if (ret && mail_parameters (NIL,GET_APPENDUID,NIL))
    MM_LOG ("Can not return meaningful APPENDUID with this mailbox format",
	    WARN);
  return ret;
}

/* Internal routines */


/* Tenex mail return internal message size in bytes
 * Accepts: MAIL stream
 *	    message #
 * Returns: internal size of message
 */

unsigned long tenex_size (MAILSTREAM *stream,unsigned long m)
{
  MESSAGECACHE *elt = mail_elt (stream,m);
  return ((m < stream->nmsgs) ? mail_elt (stream,m+1)->private.special.offset :
	  LOCAL->filesize) -
	    (elt->private.special.offset + elt->private.special.text.size);
}


/* Tenex mail generate file string
 * Accepts: temporary buffer to write into
 *	    mailbox name string
 * Returns: local file string or NIL if failure
 */

char *tenex_file (char *dst,char *name)
{
  char tmp[MAILTMPLEN];
  char *s = mailboxfile (dst,name);
				/* return our standard inbox */
  return (s && !*s) ? mailboxfile (dst,tenex_isvalid ("~/INBOX",tmp) ?
				   "~/INBOX" : "mail.txt") : s;
}

/* Tenex mail parse mailbox
 * Accepts: MAIL stream
 * Returns: T if parse OK
 *	    NIL if failure, stream aborted
 */

long tenex_parse (MAILSTREAM *stream)
{
  struct stat sbuf;
  MESSAGECACHE *elt = NIL;
  unsigned char c,*s,*t,*x;
  char tmp[MAILTMPLEN];
  unsigned long i,j;
  long curpos = LOCAL->filesize;
  long nmsgs = stream->nmsgs;
  long recent = stream->recent;
  short added = NIL;
  short silent = stream->silent;
  fstat (LOCAL->fd,&sbuf);	/* get status */
  if (sbuf.st_size < curpos) {	/* sanity check */
    sprintf (tmp,"Mailbox shrank from %lu to %lu!",
	     (unsigned long) curpos,(unsigned long) sbuf.st_size);
    MM_LOG (tmp,ERROR);
    tenex_close (stream,NIL);
    return NIL;
  }
  stream->silent = T;		/* don't pass up exists events yet */
  while (sbuf.st_size - curpos){/* while there is stuff to parse */
				/* get to that position in the file */
    lseek (LOCAL->fd,curpos,L_SET);
    if ((i = read (LOCAL->fd,LOCAL->buf,64)) <= 0) {
      sprintf (tmp,"Unable to read internal header at %lu, size = %lu: %s",
	       (unsigned long) curpos,(unsigned long) sbuf.st_size,
	       i ? strerror (errno) : "no data read");
      MM_LOG (tmp,ERROR);
      tenex_close (stream,NIL);
      return NIL;
    }
    LOCAL->buf[i] = '\0';	/* tie off buffer just in case */
    if (!(s = strchr (LOCAL->buf,'\012'))) {
      sprintf (tmp,"Unable to find newline at %lu in %lu bytes, text: %s",
	       (unsigned long) curpos,i,(char *) LOCAL->buf);
      MM_LOG (tmp,ERROR);
      tenex_close (stream,NIL);
      return NIL;
    }
    *s = '\0';			/* tie off header line */
    i = (s + 1) - LOCAL->buf;	/* note start of text offset */
    if (!((s = strchr (LOCAL->buf,',')) && (t = strchr (s+1,';')))) {
      sprintf (tmp,"Unable to parse internal header at %lu: %s",
	       (unsigned long) curpos,(char *) LOCAL->buf);
      MM_LOG (tmp,ERROR);
      tenex_close (stream,NIL);
      return NIL;
    }
    *s++ = '\0'; *t++ = '\0';	/* tie off fields */

    added = T;			/* note that a new message was added */
				/* swell the cache */
    mail_exists (stream,++nmsgs);
				/* instantiate an elt for this message */
    (elt = mail_elt (stream,nmsgs))->valid = T;
    elt->private.uid = ++stream->uid_last;
				/* note file offset of header */
    elt->private.special.offset = curpos;
				/* in case error */
    elt->private.special.text.size = 0;
				/* header size not known yet */
    elt->private.msg.header.text.size = 0;
    x = s;			/* parse the header components */
    if (mail_parse_date (elt,LOCAL->buf) &&
	(elt->private.msg.full.text.size = strtoul (s,(char **) &s,10)) &&
	(!(s && *s)) && isdigit (t[0]) && isdigit (t[1]) && isdigit (t[2]) &&
	isdigit (t[3]) && isdigit (t[4]) && isdigit (t[5]) &&
	isdigit (t[6]) && isdigit (t[7]) && isdigit (t[8]) &&
	isdigit (t[9]) && isdigit (t[10]) && isdigit (t[11]) && !t[12])
      elt->private.special.text.size = i;
    else {			/* oops */
      sprintf (tmp,"Unable to parse internal header elements at %ld: %s,%s;%s",
	       curpos,(char *) LOCAL->buf,(char *) x,(char *) t);
      MM_LOG (tmp,ERROR);
      tenex_close (stream,NIL);
      return NIL;
    }
				/* make sure didn't run off end of file */
    if ((curpos += (elt->private.msg.full.text.size + i)) > sbuf.st_size) {
      sprintf (tmp,"Last message (at %lu) runs past end of file (%lu > %lu)",
	       elt->private.special.offset,(unsigned long) curpos,
	       (unsigned long) sbuf.st_size);
      MM_LOG (tmp,ERROR);
      tenex_close (stream,NIL);
      return NIL;
    }
    c = t[10];			/* remember first system flags byte */
    t[10] = '\0';		/* tie off flags */
    j = strtoul (t,NIL,8);	/* get user flags value */
    t[10] = c;			/* restore first system flags byte */
				/* set up all valid user flags (reversed!) */
    while (j) if (((i = 29 - find_rightmost_bit (&j)) < NUSERFLAGS) &&
		  stream->user_flags[i]) elt->user_flags |= 1 << i;
				/* calculate system flags */
    if ((j = ((t[10]-'0') * 8) + t[11]-'0') & fSEEN) elt->seen = T;
    if (j & fDELETED) elt->deleted = T;
    if (j & fFLAGGED) elt->flagged = T;
    if (j & fANSWERED) elt->answered = T;
    if (j & fDRAFT) elt->draft = T;
    if (!(j & fOLD)) {		/* newly arrived message? */
      elt->recent = T;
      recent++;			/* count up a new recent message */
				/* mark it as old */
      tenex_update_status (stream,nmsgs,NIL);
    }
  }
  fsync (LOCAL->fd);		/* make sure all the fOLD flags take */
				/* update parsed file size and time */
  LOCAL->filesize = sbuf.st_size;
  fstat (LOCAL->fd,&sbuf);	/* get status again to ensure time is right */
  LOCAL->filetime = sbuf.st_mtime;
  if (added && !stream->rdonly){/* make sure atime updated */
    time_t tp[2];
    tp[0] = time (0);
    tp[1] = LOCAL->filetime;
    utime (stream->mailbox,tp);
  }
  stream->silent = silent;	/* can pass up events now */
  mail_exists (stream,nmsgs);	/* notify upper level of new mailbox size */
  mail_recent (stream,recent);	/* and of change in recent messages */
  return LONGT;			/* return the winnage */
}

/* Tenex get cache element with status updating from file
 * Accepts: MAIL stream
 *	    message number
 * Returns: cache element
 */

MESSAGECACHE *tenex_elt (MAILSTREAM *stream,unsigned long msgno)
{
  MESSAGECACHE *elt = mail_elt (stream,msgno);
  struct {			/* old flags */
    unsigned int seen : 1;
    unsigned int deleted : 1;
    unsigned int flagged : 1;
    unsigned int answered : 1;
    unsigned int draft : 1;
    unsigned long user_flags;
  } old;
  old.seen = elt->seen; old.deleted = elt->deleted; old.flagged = elt->flagged;
  old.answered = elt->answered; old.draft = elt->draft;
  old.user_flags = elt->user_flags;
  tenex_read_flags (stream,elt);
  if ((old.seen != elt->seen) || (old.deleted != elt->deleted) ||
      (old.flagged != elt->flagged) || (old.answered != elt->answered) ||
      (old.draft != elt->draft) || (old.user_flags != elt->user_flags))
    MM_FLAGS (stream,msgno);	/* let top level know */
  return elt;
}

/* Tenex read flags from file
 * Accepts: MAIL stream
 * Returns: cache element
 */

void tenex_read_flags (MAILSTREAM *stream,MESSAGECACHE *elt)
{
  unsigned long i,j;
				/* noop if readonly and have valid flags */
  if (stream->rdonly && elt->valid) return;
				/* set the seek pointer */
  lseek (LOCAL->fd,(off_t) elt->private.special.offset +
	 elt->private.special.text.size - 13,L_SET);
				/* read the new flags */
  if (read (LOCAL->fd,LOCAL->buf,12) < 0) {
    sprintf (LOCAL->buf,"Unable to read new status: %s",strerror (errno));
    fatal (LOCAL->buf);
  }
				/* calculate system flags */
  i = (((LOCAL->buf[10]-'0') * 8) + LOCAL->buf[11]-'0');
  elt->seen = i & fSEEN ? T : NIL; elt->deleted = i & fDELETED ? T : NIL;
  elt->flagged = i & fFLAGGED ? T : NIL;
  elt->answered = i & fANSWERED ? T : NIL; elt->draft = i & fDRAFT ? T : NIL;
  LOCAL->buf[10] = '\0';	/* tie off flags */
  j = strtoul(LOCAL->buf,NIL,8);/* get user flags value */
				/* set up all valid user flags (reversed!) */
  while (j) if (((i = 29 - find_rightmost_bit (&j)) < NUSERFLAGS) &&
		stream->user_flags[i]) elt->user_flags |= 1 << i;
  elt->valid = T;		/* have valid flags now */
}

/* Tenex update status string
 * Accepts: MAIL stream
 *	    message number
 *	    flag saying whether or not to sync
 */

void tenex_update_status (MAILSTREAM *stream,unsigned long msgno,long syncflag)
{
  time_t tp[2];
  struct stat sbuf;
  MESSAGECACHE *elt = mail_elt (stream,msgno);
  unsigned long j,k = 0;
				/* readonly */
  if (stream->rdonly || !elt->valid) tenex_read_flags (stream,elt);
  else {			/* readwrite */
    j = elt->user_flags;	/* get user flags */
				/* reverse bits (dontcha wish we had CIRC?) */
    while (j) k |= 1 << (29 - find_rightmost_bit (&j));
				/* print new flag string */
    sprintf (LOCAL->buf,"%010lo%02o",k,(unsigned)
	     (fOLD + (fSEEN * elt->seen) + (fDELETED * elt->deleted) +
	      (fFLAGGED * elt->flagged) + (fANSWERED * elt->answered) +
	      (fDRAFT * elt->draft)));
				/* get to that place in the file */
    lseek (LOCAL->fd,(off_t) elt->private.special.offset +
	   elt->private.special.text.size - 13,L_SET);
				/* write new flags */
    write (LOCAL->fd,LOCAL->buf,12);
    if (syncflag) {		/* sync if requested */
      fsync (LOCAL->fd);
      fstat (LOCAL->fd,&sbuf);	/* get new write time */
      tp[1] = LOCAL->filetime = sbuf.st_mtime;
      tp[0] = time (0);		/* make sure read is later */
      utime (stream->mailbox,tp);
    }
  }
}

/* Tenex locate header for a message
 * Accepts: MAIL stream
 *	    message number
 *	    pointer to returned header size
 * Returns: position of header in file
 */

unsigned long tenex_hdrpos (MAILSTREAM *stream,unsigned long msgno,
			    unsigned long *size)
{
  unsigned long siz;
  long i = 0;
  char c = '\0';
  char *s = NIL;
  MESSAGECACHE *elt = tenex_elt (stream,msgno);
  unsigned long ret = elt->private.special.offset +
    elt->private.special.text.size;
  unsigned long msiz = tenex_size (stream,msgno);
				/* is header size known? */
  if (!(*size = elt->private.msg.header.text.size)) {
    lseek (LOCAL->fd,ret,L_SET);/* get to header position */
				/* search message for LF LF */
    for (siz = 0; siz < msiz; siz++) {
      if (--i <= 0)		/* read another buffer as necessary */
	read (LOCAL->fd,s = LOCAL->buf,i = min (msiz-siz,(long) MAILTMPLEN));
				/* two newline sequence? */
      if ((c == '\012') && (*s == '\012')) {
				/* yes, note for later */
	elt->private.msg.header.text.size = (*size = siz + 1);
		
	return ret;		/* return to caller */
      }
      else c = *s++;		/* next character */
    }
				/* header consumes entire message */
    elt->private.msg.header.text.size = *size = msiz;
  }
  return ret;
}
