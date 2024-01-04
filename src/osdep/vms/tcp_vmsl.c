/* ========================================================================
 * Copyright 1988-2008 University of Washington
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
 * Program:	VMS TCP/IP routines for Netlib.
 *
 * Author:	Mark Crispin
 *		Networks and Distributed Computing
 *		Computing & Communications
 *		University of Washington
 *		Administration Building, AG-44
 *		Seattle, WA  98195
 *		Internet: MRC@CAC.Washington.EDU
 *
 * Date:	2 August 1994
 * Last Edited:	13 January 2008
 */

/* Thanks to Yehavi Bourvine at The Hebrew University of Jerusalem who
   contributed the original VMS code */

#include <descrip.h>

static char *tcp_getline_work (TCPSTREAM *stream,unsigned long *size,
			       long *contd);

/* TCP/IP manipulate parameters
 * Accepts: function code
 *	    function-dependent value
 * Returns: function-dependent return value
 */

void *tcp_parameters (long function,void *value)
{
  return NIL;
}

 
/* TCP/IP open
 * Accepts: host name
 *	    contact service name
 *	    contact port number
 * Returns: TCP/IP stream if success else NIL
 */

TCPSTREAM *tcp_open (char *host,char *service,unsigned long port)
{
  TCPSTREAM *stream = NIL;
  unsigned long sock;
  int status;
  char tmp[MAILTMPLEN];
				/* hostname to connect to */
  struct dsc$descriptor HostDesc = { 0, DSC$K_DTYPE_T, DSC$K_CLASS_S, NULL };
  port &= 0xffff;		/* erase flags */
				/* assign a local socket */
  if (!((status = net_assign (&sock)) & 0x1)) {
    sprintf (tmp,"Unable to assign to net, status=%d",status);
    mm_log (tmp,ERROR);
    return NIL;
  }
  if (!((status = net_bind (&sock,1)) & 0x1)) {
    sprintf (tmp,"Unable to create local socket, status=%d",status);
    mm_log (tmp,ERROR);
    return NIL;
  }
				/* open connection */
  HostDesc.dsc$w_length = strlen (host);
  HostDesc.dsc$a_pointer = host;
  if (!((status = tcp_connect (&sock,&HostDesc,port)) & 0x1)) {
    sprintf (tmp,"Can't connect to %.80s,%lu: %s",host,port,strerror (errno));
    mm_log (tmp,ERROR);
    return NIL;
  }
				/* create TCP/IP stream */
  stream = (TCPSTREAM *) fs_get (sizeof (TCPSTREAM));
  stream->host = cpystr (host);	/* copy official host name */
				/* copy local host name */
  stream->localhost = cpystr (mylocalhost ());
  stream->port = port;		/* copy port number */
				/* init sockets */
  stream->tcpsi = stream->tcpso = sock;
  stream->ictr = 0;		/* init input counter */
  return stream;		/* return success */
}

/* TCP/IP authenticated open
 * Accepts: NETMBX specifier
 *	    service name
 *	    returned user name buffer
 * Returns: TCP/IP stream if success else NIL
 */

TCPSTREAM *tcp_aopen (NETMBX *mb,char *service,char *usrbuf)
{
  return NIL;
}

/* TCP receive line
 * Accepts: TCP stream
 * Returns: text line string or NIL if failure
 */

char *tcp_getline (TCPSTREAM *stream)
{
  unsigned long n,contd;
  char *ret = tcp_getline_work (stream,&n,&contd);
  if (ret && contd) {		/* got a line needing continuation? */
    STRINGLIST *stl = mail_newstringlist ();
    STRINGLIST *stc = stl;
    do {			/* collect additional lines */
      stc->text.data = (unsigned char *) ret;
      stc->text.size = n;
      stc = stc->next = mail_newstringlist ();
      ret = tcp_getline_work (stream,&n,&contd);
    } while (ret && contd);
    if (ret) {			/* stash final part of line on list */
      stc->text.data = (unsigned char *) ret;
      stc->text.size = n;
				/* determine how large a buffer we need */
      for (n = 0, stc = stl; stc; stc = stc->next) n += stc->text.size;
      ret = fs_get (n + 1);	/* copy parts into buffer */
      for (n = 0, stc = stl; stc; n += stc->text.size, stc = stc->next)
	memcpy (ret + n,stc->text.data,stc->text.size);
      ret[n] = '\0';
    }
    mail_free_stringlist (&stl);/* either way, done with list */
  }
  return ret;
}

/* TCP receive line or partial line
 * Accepts: TCP stream
 *	    pointer to return size
 *	    pointer to return continuation flag
 * Returns: text line string, size and continuation flag, or NIL if failure
 */

static char *tcp_getline_work (TCPSTREAM *stream,unsigned long *size,
			       long *contd)
{
  unsigned long n;
  char *s,*ret,c,d;
  *contd = NIL;			/* assume no continuation */
				/* make sure have data */
  if (!tcp_getdata (stream)) return NIL;
  for (s = stream->iptr, n = 0, c = '\0'; stream->ictr--; n++, c = d) {
    d = *stream->iptr++;	/* slurp another character */
    if ((c == '\015') && (d == '\012')) {
      ret = (char *) fs_get (n--);
      memcpy (ret,s,*size = n);	/* copy into a free storage string */
      ret[n] = '\0';		/* tie off string with null */
      return ret;
    }
  }
				/* copy partial string from buffer */
  memcpy ((ret = (char *) fs_get (n)),s,*size = n);
				/* get more data from the net */
  if (!tcp_getdata (stream)) fs_give ((void **) &ret);
				/* special case of newline broken by buffer */
  else if ((c == '\015') && (*stream->iptr == '\012')) {
    stream->iptr++;		/* eat the line feed */
    stream->ictr--;
    ret[*size = --n] = '\0';	/* tie off string with null */
  }
  else *contd = LONGT;		/* continuation needed */
  return ret;
}

/* TCP/IP receive buffer
 * Accepts: TCP/IP stream
 *	    size in bytes
 *	    buffer to read into
 * Returns: T if success, NIL otherwise
 */

long tcp_getbuffer (TCPSTREAM *stream,unsigned long size,char *buffer)
{
  unsigned long n;
  char *bufptr = buffer;
  while (size > 0) {		/* until request satisfied */
    if (!tcp_getdata (stream)) return NIL;
    n = min (size,stream->ictr);/* number of bytes to transfer */
				/* do the copy */
    memcpy (bufptr,stream->iptr,n);
    bufptr += n;		/* update pointer */
    stream->iptr +=n;
    size -= n;			/* update # of bytes to do */
    stream->ictr -=n;
  }
  bufptr[0] = '\0';		/* tie off string */
  return T;
}


/* TCP/IP receive data
 * Accepts: TCP/IP stream
 * Returns: T if success, NIL otherwise
 */

long tcp_getdata (TCPSTREAM *stream)
{
  char tmp[MAILTMPLEN];
  int i,status;
  /* Note: the doc says we need here dynamic descriptor, but we need static
   * one... */
  struct dsc$descriptor BufDesc = {BUFLEN,DSC$K_DTYPE_T,DSC$K_CLASS_S,
				     stream->ibuf};
  static short iosb[4];
  if (stream->tcpsi < 0) return NIL;
  while (stream->ictr < 1) {	/* if nothing in the buffer */
    if (!((status = tcp_receive(&(stream->tcpsi), &BufDesc, iosb)) & 0x1)) {
      sprintf (tmp,"Error reading from TcpIp/NETLIB, status=%d",status);
      mm_log (tmp,ERROR);
      return tcp_abort (stream);
    }
    if (iosb[1] > BUFLEN) i = BUFLEN;
    else i = iosb[1];
    if (i < 1) return tcp_abort (stream);
    stream->ictr = i;		/* set new byte count */
    stream->iptr = stream->ibuf;/* point at TCP buffer */
  }
  return T;
}

/* TCP/IP send string as record
 * Accepts: TCP/IP stream
 *	    string pointer
 * Returns: T if success else NIL
 */

long tcp_soutr (TCPSTREAM *stream,char *string)
{
  return tcp_sout (stream,string,(unsigned long) strlen (string));
}


/* TCP/IP send string
 * Accepts: TCP/IP stream
 *	    string pointer
 *	    byte count
 * Returns: T if success else NIL
 */

long tcp_sout (TCPSTREAM *stream,char *string,unsigned long size)
{
  int status;
  struct dsc$descriptor_s BufDesc = {strlen(string),DSC$K_DTYPE_T,
				       DSC$K_CLASS_S,string };
				/* 2 = Do not add \r\n */
  return ((status = tcp_send (&(stream->tcpso),&BufDesc,2)) & 0x1) ? T :
    tcp_abort (stream);
}

/* TCP/IP close
 * Accepts: TCP/IP stream
 */

void tcp_close (TCPSTREAM *stream)
{
  tcp_abort (stream);		/* nuke the stream */
				/* flush host names */
  fs_give ((void **) &stream->host);
  fs_give ((void **) &stream->localhost);
  fs_give ((void **) &stream);	/* flush the stream */
}


/* TCP/IP abort stream
 * Accepts: TCP/IP stream
 * Returns: NIL always
 */

long tcp_abort (TCPSTREAM *stream)
{
  if (stream->tcpsi >= 0) {	/* no-op if no socket */
				/* nuke the socket */
    tcp_disconnect (&(stream->tcpsi));
    stream->tcpsi = stream->tcpso = -1;
  }
  return NIL;
}

/* TCP/IP get host name
 * Accepts: TCP/IP stream
 * Returns: host name for this stream
 */

char *tcp_host (TCPSTREAM *stream)
{
  return stream->host;		/* return host name */
}


/* TCP/IP get remote host name
 * Accepts: TCP/IP stream
 * Returns: host name for this stream
 */

char *tcp_remotehost (TCPSTREAM *stream)
{
  return stream->host;		/* return host name */
}


/* TCP/IP return port for this stream
 * Accepts: TCP/IP stream
 * Returns: port number for this stream
 */

unsigned long tcp_port (TCPSTREAM *stream)
{
  return stream->port;		/* return port number */
}


/* TCP/IP get local host name
 * Accepts: TCP/IP stream
 * Returns: local host name
 */

char *tcp_localhost (TCPSTREAM *stream)
{
  return stream->localhost;	/* return local host name */
}

/* Return my local host name
 * Returns: my local host name
 */

char *mylocalhost ()
{
  int status;
  char tmp[MAILTMPLEN];
  if (!myLocalHost) {		/* have local host yet? */
				/* receives local host name */
    struct dsc$descriptor LocalhostDesc = {0,DSC$K_DTYPE_T,DSC$K_CLASS_D,NULL};
    if (!((status = net_get_hostname (&LocalhostDesc)) & 0x1)) {
      sprintf (tmp,"Can't get local hostname, status=%d",status);
      mm_log (tmp,ERROR);
      return "UNKNOWN";
    }
    strncpy (tmp,LocalhostDesc.dsc$a_pointer,LocalhostDesc.dsc$w_length);
    tmp[LocalhostDesc.dsc$w_length] = '\0';
    str$free1_dx (&LocalhostDesc);
    myLocalHost = cpystr (tmp);
  }
  return myLocalHost;
}

/* TCP/IP return canonical form of host name
 * Accepts: host name
 * Returns: canonical form of host name
 */

char *tcp_canonical (char *name)
{
  return name;
}


/* TCP/IP get client host name (server calls only)
 * Returns: client host name
 */

char *tcp_clienthost ()
{
  return "UNKNOWN";
}
