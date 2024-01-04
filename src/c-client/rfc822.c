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
 * Program:	RFC 2822 and MIME routines
 *
 * Author:	Mark Crispin
 *		UW Technology
 *		University of Washington
 *		Seattle, WA  98195
 *		Internet: MRC@Washington.EDU
 *
 * Date:	27 July 1988
 * Last Edited:	14 May 2008
 *
 * This original version of this file is
 * Copyright 1988 Stanford University
 * and was developed in the Symbolic Systems Resources Group of the Knowledge
 * Systems Laboratory at Stanford University in 1987-88, and was funded by the
 * Biomedical Research Technology Program of the NationalInstitutes of Health
 * under grant number RR-00785.
 */


#include <ctype.h>
#include <stdio.h>
#include <time.h>
#include "c-client.h"


/* Support for deprecated features in earlier specifications.  Note that this
 * module follows RFC 2822, and all use of "rfc822" in function names is
 * for compatibility.  Only the code identified by the conditionals below
 * follows the earlier documents.
 */

#define RFC733 1		/* parse "at" */
#define RFC822 0		/* generate A-D-L (MUST be 0 for 2822) */

/* RFC-822 static data */

#define RFC822CONT "    "	/* RFC 2822 continuation */

				/* should have been "Remailed-" */
#define RESENTPREFIX "ReSent-"
static char *resentprefix = RESENTPREFIX;
				/* syntax error host string */
static const char *errhst = ERRHOST;


/* Body formats constant strings, must match definitions in mail.h */

char *body_types[TYPEMAX+1] = {
  "TEXT", "MULTIPART", "MESSAGE", "APPLICATION", "AUDIO", "IMAGE", "VIDEO",
  "MODEL", "X-UNKNOWN"
};


char *body_encodings[ENCMAX+1] = {
  "7BIT", "8BIT", "BINARY", "BASE64", "QUOTED-PRINTABLE", "X-UNKNOWN"
};


/* Token delimiting special characters */

				/* RFC 2822 specials */
const char *specials = " ()<>@,;:\\\"[].\1\2\3\4\5\6\7\10\11\12\13\14\15\16\17\20\21\22\23\24\25\26\27\30\31\32\33\34\35\36\37\177";
				/* RFC 2822 phrase specials (no space) */
const char *rspecials = "()<>@,;:\\\"[].\1\2\3\4\5\6\7\10\11\12\13\14\15\16\17\20\21\22\23\24\25\26\27\30\31\32\33\34\35\36\37\177";
				/* RFC 2822 dot-atom specials (no dot) */
const char *wspecials = " ()<>@,;:\\\"[]\1\2\3\4\5\6\7\10\11\12\13\14\15\16\17\20\21\22\23\24\25\26\27\30\31\32\33\34\35\36\37\177";
				/* RFC 2045 MIME body token specials */
const char *tspecials = " ()<>@,;:\\\"[]/?=\1\2\3\4\5\6\7\10\11\12\13\14\15\16\17\20\21\22\23\24\25\26\27\30\31\32\33\34\35\36\37\177";

/* Subtype defaulting (a no-no, but regretably necessary...)
 * Accepts: type code
 * Returns: default subtype name
 */

char *rfc822_default_subtype (unsigned short type)
{
  switch (type) {
  case TYPETEXT:		/* default is TEXT/PLAIN */
    return "PLAIN";
  case TYPEMULTIPART:		/* default is MULTIPART/MIXED */
    return "MIXED";
  case TYPEMESSAGE:		/* default is MESSAGE/RFC822 */
    return "RFC822";
  case TYPEAPPLICATION:		/* default is APPLICATION/OCTET-STREAM */
    return "OCTET-STREAM";
  case TYPEAUDIO:		/* default is AUDIO/BASIC */
    return "BASIC";
  default:			/* others have no default subtype */
    return "UNKNOWN";
  }
}

/* RFC 2822 parsing routines */


/* Parse an RFC 2822 message
 * Accepts: pointer to return envelope
 *	    pointer to return body
 *	    pointer to header
 *	    header byte count
 *	    pointer to body stringstruct
 *	    pointer to local host name
 *	    recursion depth
 *	    source driver flags
 */

void rfc822_parse_msg_full (ENVELOPE **en,BODY **bdy,char *s,unsigned long i,
			    STRING *bs,char *host,unsigned long depth,
			    unsigned long flags)
{
  char c,*t,*d;
  char *tmp = (char *) fs_get ((size_t) i + 100);
  ENVELOPE *env = (*en = mail_newenvelope ());
  BODY *body = bdy ? (*bdy = mail_newbody ()) : NIL;
  long MIMEp = -1;		/* flag that MIME semantics are in effect */
  parseline_t pl = (parseline_t) mail_parameters (NIL,GET_PARSELINE,NIL);
  if (!host) host = BADHOST;	/* make sure that host is non-null */
  while (i && *s != '\n') {	/* until end of header */
    t = tmp;			/* initialize buffer pointer */
    c = ' ';			/* and previous character */
    while (i && c) {		/* collect text until logical end of line */
      switch (c = *s++) {	/* slurp a character */
      case '\015':		/* return, possible end of logical line */
	if (*s == '\n') break;	/* ignore if LF follows */
      case '\012':		/* LF, possible end of logical line */
				/* tie off unless next line starts with WS */
	if (*s != ' ' && *s != '\t') *t++ = c = '\0';
	break;
      case '\t':		/* tab */
	*t++ = ' ';		/* coerce to space */
	break;
      default:			/* all other characters */
	*t++ = c;		/* insert the character into the line */
	break;
      }
      if (!--i) *t++ = '\0';	/* see if end of header */
    }

				/* find header item type */
    if (t = d = strchr (tmp,':')) {
      *d++ = '\0';		/* tie off header item, point at its data */
      while (*d == ' ') d++;	/* flush whitespace */
      while ((tmp < t--) && (*t == ' ')) *t = '\0';
      ucase (tmp);		/* coerce to uppercase */
				/* external callback */
      if (pl) (*pl) (env,tmp,d,host);
      switch (*tmp) {		/* dispatch based on first character */
      case '>':			/* possible >From: */
	if (!strcmp (tmp+1,"FROM")) rfc822_parse_adrlist (&env->from,d,host);
	break;
      case 'B':			/* possible bcc: */
	if (!strcmp (tmp+1,"CC")) rfc822_parse_adrlist (&env->bcc,d,host);
	break;
      case 'C':			/* possible cc: or Content-<mumble>*/
	if (!strcmp (tmp+1,"C")) rfc822_parse_adrlist (&env->cc,d,host);
	else if ((tmp[1] == 'O') && (tmp[2] == 'N') && (tmp[3] == 'T') &&
		 (tmp[4] == 'E') && (tmp[5] == 'N') && (tmp[6] == 'T') &&
		 (tmp[7] == '-') && body)
	  switch (MIMEp) {
	  case -1:		/* unknown if MIME or not */
	    if (!(MIMEp =	/* see if MIME-Version header exists */
		  search ((unsigned char *) s-1,i,
			  (unsigned char *)"\012MIME-Version",(long) 13))) {
#if 1
	      /* This is a disgusting kludge, and most of the messages which
	       * benefit from it are spam.
	       */
	      if (!strcmp (tmp+8,"TRANSFER-ENCODING") ||
		  (!strcmp (tmp+8,"TYPE") && strchr (d,'/'))) {
		MM_LOG ("Warning: MIME header encountered in non-MIME message",
			PARSE);
		MIMEp = 1;	/* declare MIME now */
	      }
	      else
#endif
		break;		/* non-MIME message */
	    }
	  case T:		/* definitely MIME */
	    rfc822_parse_content_header (body,tmp+8,d);
	  }
	break;
      case 'D':			/* possible Date: */
	if (!env->date && !strcmp (tmp+1,"ATE")) env->date = cpystr (d);
	break;
      case 'F':			/* possible From: */
	if (!strcmp (tmp+1,"ROM")) rfc822_parse_adrlist (&env->from,d,host);
	else if (!strcmp (tmp+1,"OLLOWUP-TO")) {
	  t = env->followup_to = (char *) fs_get (1 + strlen (d));
	  while (c = *d++) if (c != ' ') *t++ = c;
	  *t++ = '\0';
	}
	break;
      case 'I':			/* possible In-Reply-To: */
	if (!env->in_reply_to && !strcmp (tmp+1,"N-REPLY-TO"))
	  env->in_reply_to = cpystr (d);
	break;

      case 'M':			/* possible Message-ID: or MIME-Version: */
	if (!env->message_id && !strcmp (tmp+1,"ESSAGE-ID"))
	  env->message_id = cpystr (d);
	else if (!strcmp (tmp+1,"IME-VERSION")) {
				/* tie off at end of phrase */
	  if (t = rfc822_parse_phrase (d)) *t = '\0';
	  rfc822_skipws (&d);	/* skip whitespace */
				/* known version? */
	  if (strcmp (d,"1.0") && strcmp (d,"RFC-XXXX"))
	    MM_LOG ("Warning: message has unknown MIME version",PARSE);
	  MIMEp = T;		/* note that we are MIME */
	}
	break;
      case 'N':			/* possible Newsgroups: */
	if (!env->newsgroups && !strcmp (tmp+1,"EWSGROUPS")) {
	  t = env->newsgroups = (char *) fs_get (1 + strlen (d));
	  while (c = *d++) if (c != ' ') *t++ = c;
	  *t++ = '\0';
	}
	break;
      case 'R':			/* possible Reply-To: */
	if (!strcmp (tmp+1,"EPLY-TO"))
	  rfc822_parse_adrlist (&env->reply_to,d,host);
	else if (!env->references && !strcmp (tmp+1,"EFERENCES"))
	  env->references = cpystr (d);
	break;
      case 'S':			/* possible Subject: or Sender: */
	if (!env->subject && !strcmp (tmp+1,"UBJECT"))
	  env->subject = cpystr (d);
	else if (!strcmp (tmp+1,"ENDER"))
	  rfc822_parse_adrlist (&env->sender,d,host);
	break;
      case 'T':			/* possible To: */
	if (!strcmp (tmp+1,"O")) rfc822_parse_adrlist (&env->to,d,host);
	break;
      default:
	break;
      }
    }
  }
  fs_give ((void **) &tmp);	/* done with scratch buffer */
				/* default Sender: and Reply-To: to From: */
  if (!env->sender) env->sender = rfc822_cpy_adr (env->from);
  if (!env->reply_to) env->reply_to = rfc822_cpy_adr (env->from);
				/* now parse the body */
  if (body) rfc822_parse_content (body,bs,host,depth,flags);
}

/* Parse a message body content
 * Accepts: pointer to body structure
 *	    body string
 *	    pointer to local host name
 *	    recursion depth
 *	    source driver flags
 */

void rfc822_parse_content (BODY *body,STRING *bs,char *h,unsigned long depth,
			   unsigned long flags)
{
  char c,c1,*s,*s1;
  int f;
  unsigned long i,j,k,m;
  PARAMETER *param;
  PART *part = NIL;
  if (depth > MAXMIMEDEPTH) {	/* excessively deep recursion? */
    body->type = TYPETEXT;	/* yes, probably a malicious MIMEgram */
    MM_LOG ("Ignoring excessively deep MIME recursion",PARSE);
  }
  if (!body->subtype)		/* default subtype if still unknown */
    body->subtype = cpystr (rfc822_default_subtype (body->type));
				/* note offset and sizes */
  body->contents.offset = GETPOS (bs);
				/* note internal body size in all cases */
  body->size.bytes = body->contents.text.size = i = SIZE (bs);
  if (!(flags & DR_CRLF)) body->size.bytes = strcrlflen (bs);
  switch (body->type) {		/* see if anything else special to do */
  case TYPETEXT:		/* text content */
    if (!body->parameter) {	/* no parameters set */
      body->parameter = mail_newbody_parameter ();
      body->parameter->attribute = cpystr ("CHARSET");
      while (i--) {		/* count lines and guess charset */
	c = SNX (bs);		/* get current character */
				/* charset still unknown? */
	if (!body->parameter->value) {
	  if ((c == I2C_ESC) && (i && i--) && ((c = SNX (bs)) == I2C_MULTI) &&
	      (i && i--) && (((c = SNX (bs)) == I2CS_94x94_JIS_NEW) ||
			     (c == I2CS_94x94_JIS_OLD)))
	    body->parameter->value = cpystr ("ISO-2022-JP");
	  else if (c & 0x80) body->parameter->value = cpystr ("X-UNKNOWN");
	}
	if (c == '\n') body->size.lines++;
      }
				/* 7-bit content */
      if (!body->parameter->value) switch (body->encoding) {
      case ENC7BIT:		/* unadorned 7-bit */
      case ENC8BIT:		/* unadorned 8-bit (but 7-bit content) */
      case ENCBINARY:		/* binary (but 7-bit content( */
	body->parameter->value = cpystr ("US-ASCII");
	break;
      default:			/* QUOTED-PRINTABLE, BASE64, etc. */
	body->parameter->value = cpystr ("X-UNKNOWN");
	break;
      }
    }
				/* just count lines */
    else while (i--) if ((SNX (bs)) == '\n') body->size.lines++;
    break;

  case TYPEMESSAGE:		/* encapsulated message */
				/* encapsulated RFC-822 message? */
    if (!strcmp (body->subtype,"RFC822")) {
      body->nested.msg = mail_newmsg ();
      switch (body->encoding) {	/* make sure valid encoding */
      case ENC7BIT:		/* these are valid nested encodings */
      case ENC8BIT:
      case ENCBINARY:
	break;
      default:
	MM_LOG ("Ignoring nested encoding of message contents",PARSE);
      }
				/* hunt for blank line */
      for (c = '\012',j = 0; (i > j) && ((c != '\012') || (CHR(bs) != '\012'));
	   j++) if ((c1 = SNX (bs)) != '\015') c = c1;
      if (i > j) {		/* unless no more text */
	c1 = SNX (bs);		/* body starts here */
	j++;			/* advance count */
      }
				/* note body text offset and header size */
      body->nested.msg->header.text.size = j;
      body->nested.msg->text.text.size = body->contents.text.size - j;
      body->nested.msg->text.offset = GETPOS (bs);
      body->nested.msg->full.offset = body->nested.msg->header.offset =
	body->contents.offset;
      body->nested.msg->full.text.size = body->contents.text.size;
				/* copy header string */
      SETPOS (bs,body->contents.offset);
      s = (char *) fs_get ((size_t) j + 1);
      for (s1 = s,k = j; k--; *s1++ = SNX (bs));
      s[j] = '\0';		/* tie off string (not really necessary) */
				/* now parse the body */
      rfc822_parse_msg_full (&body->nested.msg->env,&body->nested.msg->body,s,
			     j,bs,h,depth+1,flags);
      fs_give ((void **) &s);	/* free header string */
				/* restore position */
      SETPOS (bs,body->contents.offset);
    }
				/* count number of lines */
    while (i--) if (SNX (bs) == '\n') body->size.lines++;
    break;
  case TYPEMULTIPART:		/* multiple parts */
    switch (body->encoding) {	/* make sure valid encoding */
    case ENC7BIT:		/* these are valid nested encodings */
    case ENC8BIT:
    case ENCBINARY:
      break;
    default:
      MM_LOG ("Ignoring nested encoding of multipart contents",PARSE);
    }
				/* remember if digest */
    f = !strcmp (body->subtype,"DIGEST");
				/* find cookie */
    for (s1 = NIL,param = body->parameter; param && !s1; param = param->next)
      if (!strcmp (param->attribute,"BOUNDARY")) s1 = param->value;
    if (!s1) s1 = "-";		/* yucky default */
    j = strlen (s1) + 2;	/* length of cookie and header */
    c = '\012';			/* initially at beginning of line */

    while (i > j) {		/* examine data */
      if (m = GETPOS (bs)) m--;	/* get position in front of character */
      switch (c) {		/* examine each line */
      case '\015':		/* handle CRLF form */
	if (CHR (bs) == '\012'){/* following LF? */
	  c = SNX (bs); i--;	/* yes, slurp it */
	}
      case '\012':		/* at start of a line, start with -- ? */
	if (!(i && i-- && ((c = SNX (bs)) == '-') && i-- &&
	      ((c = SNX (bs)) == '-'))) break;
				/* see if cookie matches */
	if (k = j - 2) for (s = s1; i-- && *s++ == (c = SNX (bs)) && --k;);
	if (k) break;		/* strings didn't match if non-zero */
				/* terminating delimiter? */
	if ((c = ((i && i--) ? (SNX (bs)) : '\012')) == '-') {
	  if ((i && i--) && ((c = SNX (bs)) == '-') &&
	      ((i && i--) ? (((c = SNX (bs)) == '\015') || (c=='\012')):T)) {
				/* if have a final part calculate its size */
	    if (part) part->body.mime.text.size =
	      (m > part->body.mime.offset) ? (m - part->body.mime.offset) :0;
	    part = NIL; i = 1;	/* terminate scan */
	  }
	  break;
	}
				/* swallow trailing whitespace */
	while ((c == ' ') || (c == '\t'))
	  c = ((i && i--) ? (SNX (bs)) : '\012');
	switch (c) {		/* need newline after all of it */
	case '\015':		/* handle CRLF form */
	  if (i && CHR (bs) == '\012') {
	    c = SNX (bs); i--;/* yes, slurp it */
	  }
	case '\012':		/* new line */
	  if (part) {		/* calculate size of previous */
	    part->body.mime.text.size =
	      (m > part->body.mime.offset) ? (m-part->body.mime.offset) : 0;
	    /* instantiate next */
	    part = part->next = mail_newbody_part ();
	  }			/* otherwise start new list */
	  else part = body->nested.part = mail_newbody_part ();
				/* digest has a different default */
	  if (f) part->body.type = TYPEMESSAGE;
				/* note offset from main body */
	  part->body.mime.offset = GETPOS (bs);
	  break;
	default:		/* whatever it was it wasn't valid */
	  break;
	}
	break;
      default:			/* not at a line */
	c = SNX (bs); i--;	/* get next character */
	break;
      }
    }

				/* calculate size of any final part */
    if (part) part->body.mime.text.size = i +
      ((GETPOS(bs) > part->body.mime.offset) ?
       (GETPOS(bs) - part->body.mime.offset) : 0);
				/* make a scratch buffer */
    s1 = (char *) fs_get ((size_t) (k = MAILTMPLEN));
				/* in case empty multipart */
    if (!body->nested.part) body->nested.part = mail_newbody_part ();
				/* parse non-empty body parts */
    for (part = body->nested.part; part; part = part->next) {
				/* part non-empty (header and/or content)? */
      if (i = part->body.mime.text.size) {
				/* move to that part of the body */
	SETPOS (bs,part->body.mime.offset);
				/* until end of header */
	while (i && ((c = CHR (bs)) != '\015') && (c != '\012')) {
				/* collect text until logical end of line */
	  for (j = 0,c = ' '; c; ) {
				/* make sure buffer big enough */
	    if (j > (k - 10)) fs_resize ((void **) &s1,k += MAILTMPLEN);
	    switch (c1 = SNX (bs)) {
	    case '\015':	/* return */
	      if (i && (CHR (bs) == '\012')) {
		c1 = SNX (bs);	/* eat any LF following */
		i--;
	      }
	    case '\012':	/* newline, possible end of logical line */
				/* tie off unless continuation */
	      if (!i || ((CHR (bs) != ' ') && (CHR (bs) != '\t')))
		s1[j] = c = '\0';
	      break;
	    case '\t':		/* tab */
	    case ' ':		/* insert whitespace if not already there */
	      if (c != ' ') s1[j++] = c = ' ';
	      break;
	    default:		/* all other characters */
	      s1[j++] = c = c1;	/* insert the character into the line */
	      break;
	    }
				/* end of data ties off the header */
	    if (!i || !--i) s1[j++] = c = '\0';
	  }

				/* find header item type */
	  if (((s1[0] == 'C') || (s1[0] == 'c')) &&
	      ((s1[1] == 'O') || (s1[1] == 'o')) &&
	      ((s1[2] == 'N') || (s1[2] == 'n')) &&
	      ((s1[3] == 'T') || (s1[3] == 't')) &&
	      ((s1[4] == 'E') || (s1[4] == 'e')) &&
	      ((s1[5] == 'N') || (s1[5] == 'n')) &&
	      ((s1[6] == 'T') || (s1[6] == 't')) &&
	      (s1[7] == '-') && (s = strchr (s1+8,':'))) {
				/* tie off and flush whitespace */
	    for (*s++ = '\0'; *s == ' '; s++);
				/* parse the header */
	    rfc822_parse_content_header (&part->body,ucase (s1+8),s);
	  }
	}
				/* skip header trailing (CR)LF */
	if (i && (CHR (bs) =='\015')) {i--; c1 = SNX (bs);}
	if (i && (CHR (bs) =='\012')) {i--; c1 = SNX (bs);}
	j = bs->size;		/* save upper level size */
				/* set offset for next level, fake size to i */
	bs->size = GETPOS (bs) + i;
	part->body.mime.text.size -= i;
				/* now parse it */
	rfc822_parse_content (&part->body,bs,h,depth+1,flags);
	bs->size = j;		/* restore current level size */
      }
      else {			/* zero-length part, use default subtype */
	part->body.subtype = cpystr (rfc822_default_subtype (part->body.type));
				/* see if anything else special to do */
	switch (part->body.type) {
	case TYPETEXT:		/* text content */
				/* default parameters */
	  if (!part->body.parameter) {
	    part->body.parameter = mail_newbody_parameter ();
	    part->body.parameter->attribute = cpystr ("CHARSET");
				/* only assume US-ASCII if 7BIT */
	    part->body.parameter->value =
	      cpystr ((part->body.encoding == ENC7BIT) ?
		      "US-ASCII" : "X-UNKNOWN");
	  }
	  break;
	case TYPEMESSAGE:	/* encapsulated message in digest */
	  part->body.nested.msg = mail_newmsg ();
	  break;
	default:
	  break;
	}
      }
    }
    fs_give ((void **) &s1);	/* finished with scratch buffer */
    break;
  default:			/* nothing special to do in any other case */
    break;
  }
}

/* Parse RFC 2822 body content header
 * Accepts: body to write to
 *	    possible content name
 *	    remainder of header
 */

void rfc822_parse_content_header (BODY *body,char *name,char *s)
{
  char c,*t,tmp[MAILTMPLEN];
  long i;
  STRINGLIST *stl;
  rfc822_skipws (&s);		/* skip leading comments */
				/* flush whitespace */
  if (t = strchr (name,' ')) *t = '\0';
  switch (*name) {		/* see what kind of content */
  case 'I':			/* possible Content-ID */
    if (!(strcmp (name+1,"D") || body->id)) body->id = cpystr (s);
    break;
  case 'D':			/* possible Content-Description */
    if (!(strcmp (name+1,"ESCRIPTION") || body->description))
      body->description = cpystr (s);
    if (!(strcmp (name+1,"ISPOSITION") || body->disposition.type)) {
				/* get type word */
      if (!(name = rfc822_parse_word (s,tspecials))) break;
      c = *name;		/* remember delimiter */
      *name = '\0';		/* tie off type */
      body->disposition.type = ucase (cpystr (s));
      *name = c;		/* restore delimiter */
      rfc822_skipws (&name);	/* skip whitespace */
      rfc822_parse_parameter (&body->disposition.parameter,name);
    }
    break;
  case 'L':			/* possible Content-Language */
    if (!(strcmp (name+1,"ANGUAGE") || body->language)) {
      stl = NIL;		/* process languages */
      while (s && (name = rfc822_parse_word (s,tspecials))) {
	c = *name;		/* save delimiter */
	*name = '\0';		/* tie off subtype */
	if (stl) stl = stl->next = mail_newstringlist ();
	else stl = body->language = mail_newstringlist ();
	stl->text.data = (unsigned char *) ucase (cpystr (s));
	stl->text.size = strlen ((char *) stl->text.data);
	*name = c;		/* restore delimiter */
	rfc822_skipws (&name);	/* skip whitespace */
	if (*name == ',') {	/* any more languages? */
	  s = ++name;		/* advance to it them */
	  rfc822_skipws (&s);
	}
	else s = NIL;		/* bogus or end of list */
      }
    }
    else if (!(strcmp (name+1,"OCATION") || body->location))
      body->location = cpystr (s);
    break;
  case 'M':			/* possible Content-MD5 */
    if (!(strcmp (name+1,"D5") || body->md5)) body->md5 = cpystr (s);
    break;

  case 'T':			/* possible Content-Type/Transfer-Encoding */
    if (!(strcmp (name+1,"YPE") || body->subtype || body->parameter)) {
				/* get type word */
      if (!(name = rfc822_parse_word (s,tspecials))) break;
      c = *name;		/* remember delimiter */
      *name = '\0';		/* tie off type */
				/* search for body type */
      for (i = 0,s = rfc822_cpy (s);
	   (i <= TYPEMAX) && body_types[i] &&
	     compare_cstring (s,body_types[i]); i++);
      if (i > TYPEMAX) {	/* fell off end of loop? */
	body->type = TYPEOTHER;	/* coerce to X-UNKNOWN */
	sprintf (tmp,"MIME type table overflow: %.100s",s);
	MM_LOG (tmp,PARSE);
      }
      else {			/* record body type index */
	body->type = (unsigned short) i;
				/* and name if new type */
	if (body_types[body->type]) fs_give ((void **) &s);
	else {			/* major MIME body type unknown to us */
	  body_types[body->type] = ucase (s);
	  sprintf (tmp,"Unknown MIME type: %.100s",s);
	  MM_LOG (tmp,PARSE);
	}
      }
      *name = c;		/* restore delimiter */
      rfc822_skipws (&name);	/* skip whitespace */
      if ((*name == '/') &&	/* subtype? */
	  (name = rfc822_parse_word ((s = ++name),tspecials))) {
	c = *name;		/* save delimiter */
	*name = '\0';		/* tie off subtype */
	rfc822_skipws (&s);	/* copy subtype */
	if (s) body->subtype = ucase (rfc822_cpy (s));
	*name = c;		/* restore delimiter */
	rfc822_skipws (&name);	/* skip whitespace */
      }
      else if (!name) {		/* no subtype, was a subtype delimiter? */
	name = s;		/* barf, restore pointer */
	rfc822_skipws (&name);	/* skip leading whitespace */
      }
      rfc822_parse_parameter (&body->parameter,name);
    }

    else if (!strcmp (name+1,"RANSFER-ENCODING")) {
      if (!(name = rfc822_parse_word (s,tspecials))) break;
      c = *name;		/* remember delimiter */
      *name = '\0';		/* tie off encoding */
				/* search for body encoding */      
      for (i = 0,s = rfc822_cpy (s);
	   (i <= ENCMAX) && body_encodings[i] &&
	     compare_cstring (s,body_encodings[i]); i++);
      if (i > ENCMAX) {		/* fell off end of loop? */
	body->encoding = ENCOTHER;
	sprintf (tmp,"MIME encoding table overflow: %.100s",s);
	MM_LOG (tmp,PARSE);
      }
      else {			/* record body encoding index */
	body->encoding = (unsigned short) i;
				/* and name if new encoding */
	if (body_encodings[body->encoding]) fs_give ((void **) &s);
	else {
	  body_encodings[body->encoding] = ucase (s);
	  sprintf (tmp,"Unknown MIME transfer encoding: %.100s",s);
	  MM_LOG (tmp,PARSE);
	}
      }
      *name = c;		/* restore delimiter */
      /* ??check for cruft here?? */
    }
    break;
  default:			/* otherwise unknown */
    break;
  }
}

/* Parse RFC 2822 body parameter list
 * Accepts: parameter list to write to
 *	    text of list
 */

void rfc822_parse_parameter (PARAMETER **par,char *text)
{
  char c,*s,tmp[MAILTMPLEN];
  PARAMETER *param = NIL;
				/* parameter list? */
  while (text && (*text == ';') &&
	 (text = rfc822_parse_word ((s = ++text),tspecials))) {
    c = *text;			/* remember delimiter */
    *text = '\0';		/* tie off attribute name */
    rfc822_skipws (&s);		/* skip leading attribute whitespace */
    if (!*s) *text = c;		/* must have an attribute name */
    else {			/* instantiate a new parameter */
      if (*par) param = param->next = mail_newbody_parameter ();
      else param = *par = mail_newbody_parameter ();
      param->attribute = ucase (cpystr (s));
      *text = c;		/* restore delimiter */
      rfc822_skipws (&text);	/* skip whitespace before equal sign */
      if ((*text == '=') &&	/* make sure have value */
	  (text = rfc822_parse_word ((s = ++text),tspecials))) {
	c = *text;		/* remember delimiter */
	*text = '\0';		/* tie off value */
	rfc822_skipws (&s);	/* skip leading value whitespace */
	if (*s) param->value = rfc822_cpy (s);
	*text = c;		/* restore delimiter */
	rfc822_skipws (&text);
      }
      if (!param->value) {	/* value not found? */
	param->value = cpystr ("MISSING_PARAMETER_VALUE");
	sprintf (tmp,"Missing parameter value: %.80s",param->attribute);
	MM_LOG (tmp,PARSE);
      }
    }
  }
				/* string not present */
  if (!text) MM_LOG ("Missing parameter",PARSE);
  else if (*text) {		/* must be end of poop */
    sprintf (tmp,"Unexpected characters at end of parameters: %.80s",text);
    MM_LOG (tmp,PARSE);
  }
}

/* Parse RFC 2822 address list
 * Accepts: address list to write to
 *	    input string
 *	    default host name
 */

void rfc822_parse_adrlist (ADDRESS **lst,char *string,char *host)
{
  int c;
  char *s,tmp[MAILTMPLEN];
  ADDRESS *last = *lst;
  ADDRESS *adr;
  if (!string) return;		/* no string */
  rfc822_skipws (&string);	/* skip leading WS */
  if (!*string) return;		/* empty string */
				/* run to tail of list */
  if (last) while (last->next) last = last->next;
  while (string) {		/* loop until string exhausted */
    while (*string == ',') {	/* RFC 822 allowed null addresses!! */
      ++string;			/* skip the comma */
      rfc822_skipws (&string);	/* and any leading WS */
    }
    if (!*string) string = NIL;	/* punt if ran out of string */
				/* got an address? */
    else if (adr = rfc822_parse_address (lst,last,&string,host,0)) {
      last = adr;		/* new tail address */
      if (string) {		/* analyze what follows */
	rfc822_skipws (&string);
	switch (c = *(unsigned char *) string) {
	case ',':		/* comma? */
	  ++string;		/* then another address follows */
	  break;
	default:
	  s = isalnum (c) ? "Must use comma to separate addresses: %.80s" :
	    "Unexpected characters at end of address: %.80s";
	  sprintf (tmp,s,string);
	  MM_LOG (tmp,PARSE);
	  last = last->next = mail_newaddr ();
	  last->mailbox = cpystr ("UNEXPECTED_DATA_AFTER_ADDRESS");
	  last->host = cpystr (errhst);
				/* falls through */
	case '\0':		/* null-specified address? */
	  string = NIL;		/* punt remainder of parse */
	  break;
	}
      }
    }
    else if (string) {		/* bad mailbox */
      rfc822_skipws (&string);	/* skip WS */
      if (!*string) strcpy (tmp,"Missing address after comma");
      else sprintf (tmp,"Invalid mailbox list: %.80s",string);
      MM_LOG (tmp,PARSE);
      string = NIL;
      (adr = mail_newaddr ())->mailbox = cpystr ("INVALID_ADDRESS");
      adr->host = cpystr (errhst);
      if (last) last = last->next = adr;
      else *lst = last = adr;
      break;
    }
  }
}

/* Parse RFC 2822 address
 * Accepts: address list to write to
 *	    tail of address list
 *	    pointer to input string
 *	    default host name
 *	    group nesting depth
 * Returns: new list tail
 */

ADDRESS *rfc822_parse_address (ADDRESS **lst,ADDRESS *last,char **string,
			       char *defaulthost,unsigned long depth)
{
  ADDRESS *adr;
  if (!*string) return NIL;	/* no string */
  rfc822_skipws (string);	/* skip leading WS */
  if (!**string) return NIL;	/* empty string */
  if (adr = rfc822_parse_group (lst,last,string,defaulthost,depth)) last = adr;
				/* got an address? */
  else if (adr = rfc822_parse_mailbox (string,defaulthost)) {
    if (!*lst) *lst = adr;	/* yes, first time through? */
    else last->next = adr;	/* no, append to the list */
				/* set for subsequent linking */
    for (last = adr; last->next; last = last->next);
  }
  else if (*string) return NIL;
  return last;
}

/* Parse RFC 2822 group
 * Accepts: address list to write to
 *	    pointer to tail of address list
 *	    pointer to input string
 *	    default host name
 *	    group nesting depth
 */

ADDRESS *rfc822_parse_group (ADDRESS **lst,ADDRESS *last,char **string,
			     char *defaulthost,unsigned long depth)
{
  char tmp[MAILTMPLEN];
  char *p,*s;
  ADDRESS *adr;
  if (depth > MAXGROUPDEPTH) {	/* excessively deep recursion? */
    MM_LOG ("Ignoring excessively deep group recursion",PARSE);
    return NIL;			/* probably abusive */
  }
  if (!*string) return NIL;	/* no string */
  rfc822_skipws (string);	/* skip leading WS */
  if (!**string ||		/* trailing whitespace or not group */
      ((*(p = *string) != ':') && !(p = rfc822_parse_phrase (*string))))
    return NIL;
  s = p;			/* end of candidate phrase */
  rfc822_skipws (&s);		/* find delimiter */
  if (*s != ':') return NIL;	/* not really a group */
  *p = '\0';			/* tie off group name */
  p = ++s;			/* continue after the delimiter */
  rfc822_skipws (&p);		/* skip subsequent whitespace */
				/* write as address */
  (adr = mail_newaddr ())->mailbox = rfc822_cpy (*string);
  if (!*lst) *lst = adr;	/* first time through? */
  else last->next = adr;	/* no, append to the list */
  last = adr;			/* set for subsequent linking */
  *string = p;			/* continue after this point */
  while (*string && **string && (**string != ';')) {
    if (adr = rfc822_parse_address (lst,last,string,defaulthost,depth+1)) {
      last = adr;		/* new tail address */
      if (*string) {		/* anything more? */
	rfc822_skipws (string);	/* skip whitespace */
	switch (**string) {	/* see what follows */
	case ',':		/* another address? */
	  ++*string;		/* yes, skip past the comma */
	case ';':		/* end of group? */
	case '\0':		/* end of string */
	  break;
	default:
	  sprintf (tmp,"Unexpected characters after address in group: %.80s",
		   *string);
	  MM_LOG (tmp,PARSE);
	  *string = NIL;	/* cancel remainder of parse */
	  last = last->next = mail_newaddr ();
	  last->mailbox = cpystr ("UNEXPECTED_DATA_AFTER_ADDRESS_IN_GROUP");
	  last->host = cpystr (errhst);
	}
      }
    }
    else {			/* bogon */
      sprintf (tmp,"Invalid group mailbox list: %.80s",*string);
      MM_LOG (tmp,PARSE);
      *string = NIL;		/* cancel remainder of parse */
      (adr = mail_newaddr ())->mailbox = cpystr ("INVALID_ADDRESS_IN_GROUP");
      adr->host = cpystr (errhst);
      last = last->next = adr;
    }
  }
  if (*string) {		/* skip close delimiter */
    if (**string == ';') ++*string;
    rfc822_skipws (string);
  }
				/* append end of address mark to the list */
  last->next = (adr = mail_newaddr ());
  last = adr;			/* set for subsequent linking */
  return last;			/* return the tail */
}

/* Parse RFC 2822 mailbox
 * Accepts: pointer to string pointer
 *	    default host
 * Returns: address list
 *
 * Updates string pointer
 */

ADDRESS *rfc822_parse_mailbox (char **string,char *defaulthost)
{
  ADDRESS *adr = NIL;
  char *s,*end;
  parsephrase_t pp = (parsephrase_t) mail_parameters (NIL,GET_PARSEPHRASE,NIL);
  if (!*string) return NIL;	/* no string */
  rfc822_skipws (string);	/* flush leading whitespace */
  if (!**string) return NIL;	/* empty string */
  if (*(s = *string) == '<') 	/* note start, handle case of phraseless RA */
    adr = rfc822_parse_routeaddr (s,string,defaulthost);
				/* otherwise, expect at least one word */
  else if (end = rfc822_parse_phrase (s)) {
    if ((adr = rfc822_parse_routeaddr (end,string,defaulthost))) {
				/* phrase is a personal name */
      if (adr->personal) fs_give ((void **) &adr->personal);
      *end = '\0';		/* tie off phrase */
      adr->personal = rfc822_cpy (s);
    }
				/* call external phraseparser if phrase only */
    else if (pp && rfc822_phraseonly (end) &&
	     (adr = (*pp) (s,end,defaulthost))) {
      *string = end;		/* update parse pointer */
      rfc822_skipws (string);	/* skip WS in the normal way */
    }
    else adr = rfc822_parse_addrspec (s,string,defaulthost);
  }
  return adr;			/* return the address */
}


/* Check if address is a phrase only
 * Accepts: pointer to end of phrase
 * Returns: T if phrase only, else NIL;
 */

long rfc822_phraseonly (char *end)
{
  while (*end == ' ') ++end;	/* call rfc822_skipws() instead?? */
  switch (*end) {
  case '\0': case ',': case ';':
    return LONGT;		/* is a phrase only */
  }
  return NIL;			/* something other than phase is here */
}

/* Parse RFC 2822 route-address
 * Accepts: string pointer
 *	    pointer to string pointer to update
 * Returns: address
 *
 * Updates string pointer
 */

ADDRESS *rfc822_parse_routeaddr (char *string,char **ret,char *defaulthost)
{
  char tmp[MAILTMPLEN];
  ADDRESS *adr;
  char *s,*t,*adl;
  size_t adllen,i;
  if (!string) return NIL;
  rfc822_skipws (&string);	/* flush leading whitespace */
				/* must start with open broket */
  if (*string != '<') return NIL;
  t = ++string;			/* see if A-D-L there */
  rfc822_skipws (&t);		/* flush leading whitespace */
  for (adl = NIL,adllen = 0;	/* parse possible A-D-L */
       (*t == '@') && (s = rfc822_parse_domain (t+1,&t));) {
    i = strlen (s) + 2;		/* @ plus domain plus delimiter or NUL */
    if (adl) {			/* have existing A-D-L? */
      fs_resize ((void **) &adl,adllen + i);
      sprintf (adl + adllen - 1,",@%s",s);
    }
				/* write initial A-D-L */
    else sprintf (adl = (char *) fs_get (i),"@%s",s);
    adllen += i;		/* new A-D-L length */
    fs_give ((void **) &s);	/* don't need domain any more */
    rfc822_skipws (&t);		/* skip WS */
    if (*t != ',') break;	/* put if not comma */
    t++;			/* skip the comma */
    rfc822_skipws (&t);		/* skip WS */
  }
  if (adl) {			/* got an A-D-L? */
    if (*t != ':') {		/* make sure syntax good */
      sprintf (tmp,"Unterminated at-domain-list: %.80s%.80s",adl,t);
      MM_LOG (tmp,PARSE);
    }
    else string = ++t;		/* continue parse from this point */
  }

				/* parse address spec */
  if (!(adr = rfc822_parse_addrspec (string,ret,defaulthost))) {
    if (adl) fs_give ((void **) &adl);
    return NIL;
  }
  if (adl) adr->adl = adl;	/* have an A-D-L? */
  if (*ret) if (**ret == '>') {	/* make sure terminated OK */
    ++*ret;			/* skip past the broket */
    rfc822_skipws (ret);	/* flush trailing WS */
    if (!**ret) *ret = NIL;	/* wipe pointer if at end of string */
    return adr;			/* return the address */
  }
  sprintf (tmp,"Unterminated mailbox: %.80s@%.80s",adr->mailbox,
	   *adr->host == '@' ? "<null>" : adr->host);
  MM_LOG (tmp,PARSE);
  adr->next = mail_newaddr ();
  adr->next->mailbox = cpystr ("MISSING_MAILBOX_TERMINATOR");
  adr->next->host = cpystr (errhst);
  return adr;			/* return the address */
}

/* Parse RFC 2822 address-spec
 * Accepts: string pointer
 *	    pointer to string pointer to update
 *	    default host
 * Returns: address
 *
 * Updates string pointer
 */

ADDRESS *rfc822_parse_addrspec (char *string,char **ret,char *defaulthost)
{
  ADDRESS *adr;
  char c,*s,*t,*v,*end;
  if (!string) return NIL;	/* no string */
  rfc822_skipws (&string);	/* flush leading whitespace */
  if (!*string) return NIL;	/* empty string */
				/* find end of mailbox */
  if (!(t = rfc822_parse_word (string,wspecials))) return NIL;
  adr = mail_newaddr ();	/* create address block */
  c = *t;			/* remember delimiter */
  *t = '\0';			/* tie off mailbox */
				/* copy mailbox */
  adr->mailbox = rfc822_cpy (string);
  *t = c;			/* restore delimiter */
  end = t;			/* remember end of mailbox */
  rfc822_skipws (&t);		/* skip whitespace */
  while (*t == '.') {		/* some cretin taking RFC 822 too seriously? */
    string = ++t;		/* skip past the dot and any WS */
    rfc822_skipws (&string);
				/* get next word of mailbox */
    if (t = rfc822_parse_word (string,wspecials)) {
      end = t;			/* remember new end of mailbox */
      c = *t;			/* remember delimiter */
      *t = '\0';		/* tie off word */
      s = rfc822_cpy (string);	/* copy successor part */
      *t = c;			/* restore delimiter */
				/* build new mailbox */
      sprintf (v = (char *) fs_get (strlen (adr->mailbox) + strlen (s) + 2),
	       "%s.%s",adr->mailbox,s);
      fs_give ((void **) &adr->mailbox);
      adr->mailbox = v;		/* new host name */
      rfc822_skipws (&t);	/* skip WS after mailbox */
    }
    else {			/* barf */
      MM_LOG ("Invalid mailbox part after .",PARSE);
      break;
    }
  }
  t = end;			/* remember delimiter in case no host */

  rfc822_skipws (&end);		/* sniff ahead at what follows */
#if RFC733			/* RFC 733 used "at" instead of "@" */
  if (((*end == 'a') || (*end == 'A')) &&
      ((end[1] == 't') || (end[1] == 'T')) &&
      ((end[2] == ' ') || (end[2] == '\t') || (end[2] == '\015') ||
       (end[2] == '\012') || (end[2] == '(')))
    *++end = '@';
#endif
  if (*end != '@') end = t;	/* host name missing */
				/* otherwise parse host name */
  else if (!(adr->host = rfc822_parse_domain (++end,&end)))
    adr->host = cpystr (errhst);
				/* default host if missing */
  if (!adr->host) adr->host = cpystr (defaulthost);
				/* try person name in comments if missing */
  if (end && !(adr->personal && *adr->personal)) {
    while (*end == ' ') ++end;	/* see if we can find a person name here */
    if ((*end == '(') && (s = rfc822_skip_comment (&end,LONGT)) && strlen (s))
      adr->personal = rfc822_cpy (s);
    rfc822_skipws (&end);	/* skip any other WS in the normal way */
  }
				/* set return to end pointer */
  *ret = (end && *end) ? end : NIL;
  return adr;			/* return the address we got */
}

/* Parse RFC 2822 domain
 * Accepts: string pointer
 *	    pointer to return end of domain
 * Returns: domain name or NIL if failure
 */

char *rfc822_parse_domain (char *string,char **end)
{
  char *ret = NIL;
  char c,*s,*t,*v;
  rfc822_skipws (&string);	/* skip whitespace */
  if (*string == '[') {		/* domain literal? */
    if (!(*end = rfc822_parse_word (string + 1,"]\\")))
      MM_LOG ("Empty domain literal",PARSE);
    else if (**end != ']') MM_LOG ("Unterminated domain literal",PARSE);
    else {
      size_t len = ++*end - string;
      strncpy (ret = (char *) fs_get (len + 1),string,len);
      ret[len] = '\0';		/* tie off literal */
    }
  }
				/* search for end of host */
  else if (t = rfc822_parse_word (string,wspecials)) {
    c = *t;			/* remember delimiter */
    *t = '\0';			/* tie off host */
    ret = rfc822_cpy (string);	/* copy host */
    *t = c;			/* restore delimiter */
    *end = t;			/* remember end of domain */
    rfc822_skipws (&t);		/* skip WS after host */
    while (*t == '.') {		/* some cretin taking RFC 822 too seriously? */
      string = ++t;		/* skip past the dot and any WS */
      rfc822_skipws (&string);
      if (string = rfc822_parse_domain (string,&t)) {
	*end = t;		/* remember new end of domain */
	c = *t;			/* remember delimiter */
	*t = '\0';		/* tie off host */
	s = rfc822_cpy (string);/* copy successor part */
	*t = c;			/* restore delimiter */
				/* build new domain */
	sprintf (v = (char *) fs_get (strlen (ret) + strlen (s) + 2),
		 "%s.%s",ret,s);
	fs_give ((void **) &ret);
	ret = v;		/* new host name */
	rfc822_skipws (&t);	/* skip WS after domain */
      }
      else {			/* barf */
	MM_LOG ("Invalid domain part after .",PARSE);
	break;
      }
    }
  }
  else MM_LOG ("Missing or invalid host name after @",PARSE);
  return ret;
}

/* Parse RFC 2822 phrase
 * Accepts: string pointer
 * Returns: pointer to end of phrase
 */

char *rfc822_parse_phrase (char *s)
{
  char *curpos;
  if (!s) return NIL;		/* no-op if no string */
				/* find first word of phrase */
  curpos = rfc822_parse_word (s,NIL);
  if (!curpos) return NIL;	/* no words means no phrase */
  if (!*curpos) return curpos;	/* check if string ends with word */
  s = curpos;			/* sniff past the end of this word and WS */
  rfc822_skipws (&s);		/* skip whitespace */
				/* recurse to see if any more */
  return (s = rfc822_parse_phrase (s)) ? s : curpos;
}

/* Parse RFC 2822 word
 * Accepts: string pointer
 *	    delimiter (or NIL for phrase word parsing)
 * Returns: pointer to end of word
 */

char *rfc822_parse_word (char *s,const char *delimiters)
{
  char *st,*str;
  if (!s) return NIL;		/* no string */
  rfc822_skipws (&s);		/* flush leading whitespace */
  if (!*s) return NIL;		/* empty string */
  str = s;			/* hunt pointer for strpbrk */
  while (T) {			/* look for delimiter, return if none */
    if (!(st = strpbrk (str,delimiters ? delimiters : wspecials)))
      return str + strlen (str);
				/* ESC in phrase */
    if (!delimiters && (*st == I2C_ESC)) {
      str = ++st;		/* always skip past ESC */
      switch (*st) {		/* special hack for RFC 1468 (ISO-2022-JP) */
      case I2C_MULTI:		/* multi byte sequence */
	switch (*++st) {
	case I2CS_94x94_JIS_OLD:/* old JIS (1978) */
	case I2CS_94x94_JIS_NEW:/* new JIS (1983) */
	  str = ++st;		/* skip past the shift to JIS */
	  while (st = strchr (st,I2C_ESC))
	    if ((*++st == I2C_G0_94) && ((st[1] == I2CS_94_ASCII) ||
					 (st[1] == I2CS_94_JIS_ROMAN) ||
					 (st[1] == I2CS_94_JIS_BUGROM))) {
	      str = st += 2;	/* skip past the shift back to ASCII */
	      break;
	    }
				/* eats entire text if no shift back */
	  if (!st || !*st) return str + strlen (str);
	}
	break;
      case I2C_G0_94:		/* single byte sequence */
	switch (st[1]) {
	case I2CS_94_ASCII:	/* shift to ASCII */
	case I2CS_94_JIS_ROMAN:	/* shift to JIS-Roman */
	case I2CS_94_JIS_BUGROM:/* old buggy definition of JIS-Roman */
	  str = st + 2;		/* skip past the shift */
	  break;
	}
      }
    }

    else switch (*st) {		/* dispatch based on delimiter */
    case '"':			/* quoted string */
				/* look for close quote */
      while (*++st != '"') switch (*st) {
      case '\0':		/* unbalanced quoted string */
	return NIL;		/* sick sick sick */
      case '\\':		/* quoted character */
	if (!*++st) return NIL;	/* skip the next character */
      default:			/* ordinary character */
	break;			/* no special action */
      }
      str = ++st;		/* continue parse */
      break;
    case '\\':			/* quoted character */
      /* This is wrong; a quoted-pair can not be part of a word.  However,
       * domain-literal is parsed as a word and quoted-pairs can be used
       * *there*.  Either way, it's pretty pathological.
       */
      if (st[1]) {		/* not on NUL though... */
	str = st + 2;		/* skip quoted character and go on */
	break;
      }
    default:			/* found a word delimiter */
      return (st == s) ? NIL : st;
    }
  }
}

/* Copy an RFC 2822 format string
 * Accepts: string
 * Returns: copy of string
 */

char *rfc822_cpy (char *src)
{
				/* copy and unquote */
  return rfc822_quote (cpystr (src));
}


/* Unquote an RFC 2822 format string
 * Accepts: string
 * Returns: string
 */

char *rfc822_quote (char *src)
{
  char *ret = src;
  if (strpbrk (src,"\\\"")) {	/* any quoting in string? */
    char *dst = ret;
    while (*src) {		/* copy string */
      if (*src == '\"') src++;	/* skip double quote entirely */
      else {
	if (*src == '\\') src++;/* skip over single quote, copy next always */
	*dst++ = *src++;	/* copy character */
      }
    }
    *dst = '\0';		/* tie off string */
  }
  return ret;			/* return our string */
}


/* Copy address list
 * Accepts: address list
 * Returns: address list
 */

ADDRESS *rfc822_cpy_adr (ADDRESS *adr)
{
  ADDRESS *dadr;
  ADDRESS *ret = NIL;
  ADDRESS *prev = NIL;
  while (adr) {			/* loop while there's still an MAP adr */
    dadr = mail_newaddr ();	/* instantiate a new address */
    if (!ret) ret = dadr;	/* note return */
    if (prev) prev->next = dadr;/* tie on to the end of any previous */
    dadr->personal = cpystr (adr->personal);
    dadr->adl = cpystr (adr->adl);
    dadr->mailbox = cpystr (adr->mailbox);
    dadr->host = cpystr (adr->host);
    prev = dadr;		/* this is now the previous */
    adr = adr->next;		/* go to next address in list */
  }
  return (ret);			/* return the MTP address list */
}

/* Skips RFC 2822 whitespace
 * Accepts: pointer to string pointer
 */

void rfc822_skipws (char **s)
{
  while (T) switch (**s) {
  case ' ': case '\t': case '\015': case '\012':
    ++*s;			/* skip all forms of LWSP */
    break;
  case '(':			/* start of comment */
    if (rfc822_skip_comment (s,(long) NIL)) break;
  default:
    return;			/* end of whitespace */
  }
}


/* Skips RFC 2822 comment
 * Accepts: pointer to string pointer
 *	    trim flag
 * Returns: pointer to first non-blank character of comment
 */

char *rfc822_skip_comment (char **s,long trim)
{
  char *ret,tmp[MAILTMPLEN];
  char *s1 = *s;
  char *t = NIL;
				/* skip past whitespace */
  for (ret = ++s1; *ret == ' '; ret++);
  do switch (*s1) {		/* get character of comment */
  case '(':			/* nested comment? */
    if (!rfc822_skip_comment (&s1,(long) NIL)) return NIL;
    t = --s1;			/* last significant char at end of comment */
    break;
  case ')':			/* end of comment? */
    *s = ++s1;			/* skip past end of comment */
    if (trim) {			/* if level 0, must trim */
      if (t) t[1] = '\0';	/* tie off comment string */
      else *ret = '\0';		/* empty comment */
    }
    return ret;
  case '\\':			/* quote next character? */
    if (*++s1) {		/* next character non-null? */
      t = s1;			/* update last significant character pointer */
      break;			/* all OK */
    }
  case '\0':			/* end of string */
    sprintf (tmp,"Unterminated comment: %.80s",*s);
    MM_LOG (tmp,PARSE);
    **s = '\0';			/* nuke duplicate messages in case reparse */
    return NIL;			/* this is wierd if it happens */
  case ' ':			/* whitespace isn't significant */
    break;
  default:			/* random character */
    t = s1;			/* update last significant character pointer */
    break;
  } while (s1++);
  return NIL;			/* impossible, but pacify lint et al */
}

/* Buffered output routines */


/* Output character to buffer
 * Accepts: buffer
 *	    character to write
 * Returns: T if success, NIL if error
 */

static long rfc822_output_char (RFC822BUFFER *buf,int c)
{
  if ((buf->cur == buf->end) && !rfc822_output_flush (buf)) return NIL;
  *buf->cur++ = c;		/* add character, soutr buffer if full */
  return (buf->cur == buf->end) ? rfc822_output_flush (buf) : LONGT;
}


/* Output data to buffer
 * Accepts: buffer
 *	    data to write
 *	    size of data
 * Returns: T if success, NIL if error
 */

static long rfc822_output_data (RFC822BUFFER *buf,char *string,long len)
{
  while (len) {			/* until request satified */
    long i;
    if (i = min (len,buf->end - buf->cur)) {
      memcpy (buf->cur,string,i);
      buf->cur += i;		/* blat data */
      string += i;
      len -= i;
    }
				/* soutr buffer now if full */
    if ((len || (buf->cur == buf->end)) && !rfc822_output_flush (buf))
      return NIL;
  }
  return LONGT;
}

/* Output string to buffer
 * Accepts: buffer
 *	    string to write
 * Returns: T if success, NIL if error
 */

static long rfc822_output_string (RFC822BUFFER *buf,char *string)
{
  return rfc822_output_data (buf,string,strlen (string));
}


/* Flush buffer
 * Accepts: buffer
 *	    I/O routine
 *	    stream for I/O routine
 * Returns: T if success, NIL if error
 */

long rfc822_output_flush (RFC822BUFFER *buf)
{
  *buf->cur = '\0';		/* tie off buffer at this point */
  return (*buf->f) (buf->s,buf->cur = buf->beg);
}

/* Message writing routines */


/* Output RFC 822 message
 * Accepts: temporary buffer as a SIZEDTEXT
 *	    envelope
 *	    body
 *	    I/O routine
 *	    stream for I/O routine
 *	    non-zero if 8-bit output desired
 * Returns: T if successful, NIL if failure
 *
 * This routine always uses standard specials for phrases and does not write
 * bcc entries, since it is called from the SMTP and NNTP routines.  If you
 * need to do something different you need to arm an rfc822outfull_t and/or
 * rfc822out_t function.
 */

long rfc822_output_full (RFC822BUFFER *buf,ENVELOPE *env,BODY *body,long ok8)
{
  rfc822outfull_t r822of =
    (rfc822outfull_t) mail_parameters (NIL,GET_RFC822OUTPUTFULL,NIL);
  rfc822out_t r822o = (rfc822out_t) mail_parameters (NIL,GET_RFC822OUTPUT,NIL);
				/* call external RFC 2822 output generator */
  if (r822of) return (*r822of) (buf,env,body,ok8);
  else if (r822o) return (*r822o) (buf->cur,env,body,buf->f,buf->s,ok8);
				/* encode body as necessary */
  if (ok8) rfc822_encode_body_8bit (env,body);
  else rfc822_encode_body_7bit (env,body);
				/* output header and body */
  return rfc822_output_header (buf,env,body,NIL,NIL) &&
    rfc822_output_text (buf,body) && rfc822_output_flush (buf);
}

/* Output RFC 822 header
 * Accepts: buffer
 *	    envelope
 *	    body
 *	    non-standard specials to be used for phrases if non-NIL
 *	    flags (non-zero to include bcc
 * Returns: T if success, NIL if failure
 */

long rfc822_output_header (RFC822BUFFER *buf,ENVELOPE *env,BODY *body,
			   const char *specials,long flags)
{
  long i = env->remail ? strlen (env->remail) : 0;
  return			/* write header */
    (!i ||		      /* snip extra CRLF from remail header */
     rfc822_output_data (buf,env->remail,
			 ((i > 4) && (env->remail[i-4] == '\015')) ?
			 i - 2 : i)) &&
    rfc822_output_header_line (buf,"Newsgroups",i,env->newsgroups) &&
    rfc822_output_header_line (buf,"Date",i,env->date) &&
    rfc822_output_address_line (buf,"From",i,env->from,specials) &&
    rfc822_output_address_line (buf,"Sender",i,env->sender,specials) &&
    rfc822_output_address_line (buf,"Reply-To",i,env->reply_to,specials) &&
    rfc822_output_header_line (buf,"Subject",i,env->subject) &&
    ((env->bcc && !(env->to || env->cc)) ?
     rfc822_output_string (buf,"To: undisclosed recipients: ;\015\012") :
     LONGT) &&
    rfc822_output_address_line (buf,"To",i,env->to,specials) &&
    rfc822_output_address_line (buf,"cc",i,env->cc,specials) &&
    (flags ? rfc822_output_address_line (buf,"bcc",i,env->bcc,specials) : T) &&
    rfc822_output_header_line (buf,"In-Reply-To",i,env->in_reply_to) &&
    rfc822_output_header_line (buf,"Message-ID",i,env->message_id) &&
    rfc822_output_header_line (buf,"Followup-to",i,env->followup_to) &&
    rfc822_output_header_line (buf,"References",i,env->references) &&
    (env->remail || !body ||
     (rfc822_output_string (buf,"MIME-Version: 1.0\015\012") &&
      rfc822_output_body_header (buf,body))) &&
				/* write terminating blank line */
    rfc822_output_string (buf,"\015\012");
}

/* Output RFC 2822 header text line
 * Accepts: buffer
 *	    pointer to header type
 *	    non-NIL if resending
 *	    pointer to text
 * Returns: T if success, NIL if failure
 */

long rfc822_output_header_line (RFC822BUFFER *buf,char *type,long resent,
				char *text)
{
  return !text ||
    ((resent ? rfc822_output_string (buf,resentprefix) : LONGT) &&
     rfc822_output_string (buf,type) && rfc822_output_string (buf,": ") &&
     rfc822_output_string (buf,text) && rfc822_output_string (buf,"\015\012"));
}


/* Output RFC 2822 header address line
 * Accepts: buffer
 *	    pointer to header type
 *	    non-NIL if resending
 *	    address(s) to interpret
 *	    non-standard specials to be used for phrases if non-NIL
 * Returns: T if success, NIL if failure
 */

long rfc822_output_address_line (RFC822BUFFER *buf,char *type,long resent,
				 ADDRESS *adr,const char *specials)
{
  long pretty = strlen (type);
  return !adr ||
    ((resent ? rfc822_output_string (buf,resentprefix) : LONGT) &&
     rfc822_output_data (buf,type,pretty) && rfc822_output_string (buf,": ") &&
     rfc822_output_address_list (buf,adr,
				 resent ? pretty + sizeof (RESENTPREFIX) - 1 :
				 pretty,specials) &&
     rfc822_output_string (buf,"\015\012"));
}

/* Output RFC 2822 address list
 * Accepts: buffer
 *	    pointer to address list
 *	    non-zero if pretty-printing
 *	    non-standard specials to be used for phrases if non-NIL
 * Returns: T if success, NIL if failure
 */

long rfc822_output_address_list (RFC822BUFFER *buf,ADDRESS *adr,long pretty,
				 const char *specials)
{
  long n;
				/* default to rspecials */
  if (!specials) specials = rspecials;
  for (n = 0; adr; adr = adr->next) {
    char *base = buf->cur;
    if (adr->host) {		/* ordinary address? */
      if (!(pretty && n)) {	/* suppress if pretty and in group */
	if (			/* use phrase <route-addr> if phrase */
#if RFC822
	    adr->adl ||		/* or A-D-L */
#endif
	    (adr->personal && *adr->personal)) {
	  if (!((adr->personal ? rfc822_output_cat (buf,adr->personal,
						    rspecials) : LONGT) &&
		rfc822_output_string (buf," <") &&
		rfc822_output_address (buf,adr) &&
		rfc822_output_string (buf,">"))) return NIL;
	}
	else if (!rfc822_output_address (buf,adr)) return NIL;
	if (adr->next && adr->next->mailbox &&
	    !rfc822_output_string (buf,", ")) return NIL;
      }
    }
    else if (adr->mailbox) {	/* start of group? */
				/* yes, write group */
      if (!(rfc822_output_cat (buf,adr->mailbox,rspecials) &&
	    rfc822_output_string (buf,": "))) return NIL;
      ++n;			/* in a group now */
    }
    else if (n) {		/* must be end of group (but be paranoid) */
      if (!rfc822_output_char (buf,';') ||
	  ((!--n && adr->next && adr->next->mailbox) &&
	   !rfc822_output_string (buf,", "))) return NIL;
    }
    if (pretty && adr->next &&	/* pretty printing? */
	((pretty += ((buf->cur > base) ? buf->cur - base :
		     (buf->end - base) + (buf->cur - buf->beg))) >= 78)) {
      if (!(rfc822_output_string (buf,"\015\012") &&
	    rfc822_output_string (buf,RFC822CONT))) return NIL;
      base = buf->cur;	/* update base for pretty printing */
      pretty = sizeof (RFC822CONT) - 1;
    }
  }
  return LONGT;
}

/* Write RFC 2822 route-address to string
 * Accepts: buffer
 *	    pointer to single address
 * Returns: T if success, NIL if failure
 */

long rfc822_output_address (RFC822BUFFER *buf,ADDRESS *adr)
{
  return !adr || !adr->host ||
    (
#if RFC822			/* old code with A-D-L support */
     (!adr->adl || (rfc822_output_string (buf,adr->adl) &&
		    rfc822_output_char (buf,':'))) &&
#endif
     rfc822_output_cat (buf,adr->mailbox,NIL) &&
     ((*adr->host == '@') ||	/* unless null host (HIGHLY discouraged!) */
      (rfc822_output_char (buf,'@') &&
       rfc822_output_cat (buf,adr->host,NIL))));
}


/* Output RFC 2822 string with concatenation
 * Accepts: buffer
 *	    string to concatenate
 *	    list of special characters or NIL for dot-atom format
 * Returns: T if success, NIL if failure
 */

long rfc822_output_cat (RFC822BUFFER *buf,char *src,const char *specials)
{
  char *s;
  if (!*src ||			/* empty string or any specials present? */
      (specials ? (T && strpbrk (src,specials)) :
       (strpbrk (src,wspecials) || (*src == '.') || strstr (src,"..") ||
	(src[strlen (src) - 1] == '.')))) {
				/* yes, write as quoted string*/
    if (!rfc822_output_char (buf,'"')) return NIL;
				/* embedded quote characters? */
    for (; s = strpbrk (src,"\\\""); src = s + 1) {
				/* yes, insert quoting */
      if (!(rfc822_output_data (buf,src,s-src) &&
	    rfc822_output_char (buf,'\\') &&
	    rfc822_output_char (buf,*s))) return NIL;
    }
				/* return string and trailing quote*/
    return rfc822_output_string (buf,src) && rfc822_output_char (buf,'"');
  }
				/* easy case */
  return rfc822_output_string (buf,src);
}

/* Output MIME parameter list
 * Accepts: buffer
 *	    parameter list
 * Returns: T if success, NIL if failure
 */

long rfc822_output_parameter (RFC822BUFFER *buf,PARAMETER *param)
{
  while (param) {
    if (rfc822_output_string (buf,"; ") &&
	rfc822_output_string (buf,param->attribute) &&
	rfc822_output_char (buf,'=') &&
	rfc822_output_cat (buf,param->value,tspecials)) param = param->next;
    else return NIL;
  }
  return LONGT;
}


/* Output RFC 2822 stringlist
 * Accepts: buffer
 *	    stringlist
 * Returns: T if success, NIL if failure
 */

long rfc822_output_stringlist (RFC822BUFFER *buf,STRINGLIST *stl)
{
  while (stl)
    if (!rfc822_output_cat (buf,(char *) stl->text.data,tspecials) ||
	((stl = stl->next) && !rfc822_output_string (buf,", ")))
      return NIL;
  return LONGT;
}

/* Output body content header
 * Accepts: buffer
 *	    body to interpret
 * Returns: T if success, NIL if failure
 */

long rfc822_output_body_header (RFC822BUFFER *buf,BODY *body)
{
  return			/* type and subtype*/
    rfc822_output_string (buf,"Content-Type: ") &&
    rfc822_output_string (buf,body_types[body->type]) &&
    rfc822_output_char (buf,'/') &&
    rfc822_output_string (buf,body->subtype ? body->subtype :
			  rfc822_default_subtype (body->type)) &&
				/* parameters (w/ US-ASCII default */
    (body->parameter ? rfc822_output_parameter (buf,body->parameter) :
     ((body->type != TYPETEXT) ||
      (rfc822_output_string (buf,"; CHARSET=") &&
       rfc822_output_string (buf,(body->encoding == ENC7BIT) ?
			     "US-ASCII" : "X-UNKNOWN")))) &&
    (!body->encoding ||	    /* note: 7BIT never output as encoding! */
     (rfc822_output_string (buf,"\015\012Content-Transfer-Encoding: ") &&
      rfc822_output_string (buf,body_encodings[body->encoding]))) &&
    (!body->id ||		/* identification */
     (rfc822_output_string (buf,"\015\012Content-ID: ") &&
      rfc822_output_string (buf,body->id))) &&
    (!body->description ||	/* description */
     (rfc822_output_string (buf,"\015\012Content-Description: ") &&
      rfc822_output_string (buf,body->description))) &&
    (!body->md5 ||		/* MD5 checksum */
     (rfc822_output_string (buf,"\015\012Content-MD5: ") &&
      rfc822_output_string (buf,body->md5))) &&
    (!body->language ||		/* language */
     (rfc822_output_string (buf,"\015\012Content-Language: ") &&
      rfc822_output_stringlist (buf,body->language))) &&
    (!body->location ||		/* location */
     (rfc822_output_string (buf,"\015\012Content-Location: ") &&
      rfc822_output_string (buf,body->location))) &&
    (!body->disposition.type ||	/* disposition */
     (rfc822_output_string (buf,"\015\012Content-Disposition: ") &&
      rfc822_output_string (buf,body->disposition.type) &&
      rfc822_output_parameter (buf,body->disposition.parameter))) &&
    rfc822_output_string (buf,"\015\012");
}

/* Encode a body for 7BIT transmittal
 * Accepts: envelope
 *	    body
 */

void rfc822_encode_body_7bit (ENVELOPE *env,BODY *body)
{
  void *f;
  PART *part;
  PARAMETER **param;
  if (body) switch (body->type) {
  case TYPEMULTIPART:		/* multi-part */
    for (param = &body->parameter;
	 *param && strcmp ((*param)->attribute,"BOUNDARY");
	 param = &(*param)->next);
    if (!*param) {		/* cookie not set up yet? */
      char tmp[MAILTMPLEN];	/* make cookie not in BASE64 or QUOTEPRINT*/
      sprintf (tmp,"%lu-%lu-%lu=:%lu",(unsigned long) gethostid (),
	       (unsigned long) random (),(unsigned long) time (0),
	       (unsigned long) getpid ());
      (*param) = mail_newbody_parameter ();
      (*param)->attribute = cpystr ("BOUNDARY");
      (*param)->value = cpystr (tmp);
    }
    part = body->nested.part;	/* encode body parts */
    do rfc822_encode_body_7bit (env,&part->body);
    while (part = part->next);	/* until done */
    break;
  case TYPEMESSAGE:		/* encapsulated message */
    switch (body->encoding) {
    case ENC7BIT:
      break;
    case ENC8BIT:
      MM_LOG ("8-bit included message in 7-bit message body",PARSE);
      break;
    case ENCBINARY:
      MM_LOG ("Binary included message in 7-bit message body",PARSE);
      break;
    default:
      fatal ("Invalid rfc822_encode_body_7bit message encoding");
    }
    break;			/* can't change encoding */
  default:			/* all else has some encoding */
    switch (body->encoding) {
    case ENC8BIT:		/* encode 8BIT into QUOTED-PRINTABLE */
				/* remember old 8-bit contents */
      f = (void *) body->contents.text.data;
      body->contents.text.data =
	rfc822_8bit (body->contents.text.data,
		     body->contents.text.size,&body->contents.text.size);
      body->encoding = ENCQUOTEDPRINTABLE;
      fs_give (&f);		/* flush old binary contents */
      break;
    case ENCBINARY:		/* encode binary into BASE64 */
				/* remember old binary contents */
      f = (void *) body->contents.text.data;
      body->contents.text.data =
	rfc822_binary ((void *) body->contents.text.data,
		       body->contents.text.size,&body->contents.text.size);
      body->encoding = ENCBASE64;
      fs_give (&f);		/* flush old binary contents */
    default:			/* otherwise OK */
      break;
    }
    break;
  }
}

/* Encode a body for 8BIT transmittal
 * Accepts: envelope
 *	    body
 */

void rfc822_encode_body_8bit (ENVELOPE *env,BODY *body)
{
  void *f;
  PART *part;
  PARAMETER **param;
  if (body) switch (body->type) {
  case TYPEMULTIPART:		/* multi-part */
    for (param = &body->parameter;
	 *param && strcmp ((*param)->attribute,"BOUNDARY");
	 param = &(*param)->next);
    if (!*param) {		/* cookie not set up yet? */
      char tmp[MAILTMPLEN];	/* make cookie not in BASE64 or QUOTEPRINT*/
      sprintf (tmp,"%lu-%lu-%lu=:%lu",(unsigned long) gethostid (),
	       (unsigned long) random (),(unsigned long) time (0),
	       (unsigned long) getpid ());
      (*param) = mail_newbody_parameter ();
      (*param)->attribute = cpystr ("BOUNDARY");
      (*param)->value = cpystr (tmp);
    }
    part = body->nested.part;	/* encode body parts */
    do rfc822_encode_body_8bit (env,&part->body);
    while (part = part->next);	/* until done */
    break;
  case TYPEMESSAGE:		/* encapsulated message */
    switch (body->encoding) {
    case ENC7BIT:
    case ENC8BIT:
      break;
    case ENCBINARY:
      MM_LOG ("Binary included message in 8-bit message body",PARSE);
      break;
    default:
      fatal ("Invalid rfc822_encode_body_7bit message encoding");
    }
    break;			/* can't change encoding */
  default:			/* other type, encode binary into BASE64 */
    if (body->encoding == ENCBINARY) {
				/* remember old binary contents */
      f = (void *) body->contents.text.data;
      body->contents.text.data =
	rfc822_binary ((void *) body->contents.text.data,
		       body->contents.text.size,&body->contents.text.size);
      body->encoding = ENCBASE64;
      fs_give (&f);		/* flush old binary contents */
    }
    break;
  }
}

/* Output RFC 822 text
 * Accepts: buffer
 *	    body
 * Returns: T if successful, NIL if failure
 */

long rfc822_output_text (RFC822BUFFER *buf,BODY *body)
{
				/* MULTIPART gets special handling */
  if (body->type == TYPEMULTIPART) {
    char *cookie,tmp[MAILTMPLEN];
    PARAMETER *param;
    PART *part;
				/* find cookie */
    for (param = body->parameter; param && strcmp (param->attribute,"BOUNDARY");
	 param = param->next);
    if (param) cookie = param->value;
    else {		  /* make cookie not in BASE64 or QUOTEPRINT*/
      sprintf (cookie = tmp,"%lu-%lu-%lu=:%lu",(unsigned long) gethostid (),
	       (unsigned long) random (),(unsigned long) time (0),
	       (unsigned long) getpid ());
      (param = mail_newbody_parameter ())->attribute = cpystr ("BOUNDARY");
      param->value = cpystr (tmp);
      param->next = body->parameter;
      body->parameter = param;
    }
				/* output each part */
    for (part = body->nested.part; part; part = part->next)
      if (!(rfc822_output_string (buf,"--") &&
	    rfc822_output_string (buf,cookie) &&
	    rfc822_output_string (buf,"\015\012") &&
	    rfc822_output_body_header (buf,&part->body) &&
	    rfc822_output_string (buf,"\015\012") &&
	    rfc822_output_text (buf,&part->body))) return NIL;
				/* output trailing cookie */
    return rfc822_output_string (buf,"--") &&
      rfc822_output_string (buf,cookie) &&
      rfc822_output_string (buf,"--\015\012");
  }
				/* output segment and trailing CRLF */
  return (!body->contents.text.data ||
	  rfc822_output_string (buf,(char *) body->contents.text.data)) &&
    rfc822_output_string (buf,"\015\012");
}

/* Body contents encoding/decoding routines */


/* Convert BASE64 contents to binary
 * Accepts: source
 *	    length of source
 *	    pointer to return destination length
 * Returns: destination as binary or NIL if error
 */

#define WSP 0176		/* NUL, TAB, LF, FF, CR, SPC */
#define JNK 0177
#define PAD 0100

void *rfc822_base64 (unsigned char *src,unsigned long srcl,unsigned long *len)
{
  char c,*s,tmp[MAILTMPLEN];
  void *ret = fs_get ((size_t) ((*len = 4 + ((srcl * 3) / 4))) + 1);
  char *d = (char *) ret;
  int e;
  static char decode[256] = {
   WSP,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,WSP,WSP,JNK,WSP,WSP,JNK,JNK,
   JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,
   WSP,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,076,JNK,JNK,JNK,077,
   064,065,066,067,070,071,072,073,074,075,JNK,JNK,JNK,PAD,JNK,JNK,
   JNK,000,001,002,003,004,005,006,007,010,011,012,013,014,015,016,
   017,020,021,022,023,024,025,026,027,030,031,JNK,JNK,JNK,JNK,JNK,
   JNK,032,033,034,035,036,037,040,041,042,043,044,045,046,047,050,
   051,052,053,054,055,056,057,060,061,062,063,JNK,JNK,JNK,JNK,JNK,
   JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,
   JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,
   JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,
   JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,
   JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,
   JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,
   JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,
   JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK
  };
				/* initialize block */
  memset (ret,0,((size_t) *len) + 1);
  *len = 0;			/* in case we return an error */

				/* simple-minded decode */
  for (e = 0; srcl--; ) switch (c = decode[*src++]) {
  default:			/* valid BASE64 data character */
    switch (e++) {		/* install based on quantum position */
    case 0:
      *d = c << 2;		/* byte 1: high 6 bits */
      break;
    case 1:
      *d++ |= c >> 4;		/* byte 1: low 2 bits */
      *d = c << 4;		/* byte 2: high 4 bits */
      break;
    case 2:
      *d++ |= c >> 2;		/* byte 2: low 4 bits */
      *d = c << 6;		/* byte 3: high 2 bits */
      break;
    case 3:
      *d++ |= c;		/* byte 3: low 6 bits */
      e = 0;			/* reinitialize mechanism */
      break;
    }
    break;
  case WSP:			/* whitespace */
    break;
  case PAD:			/* padding */
    switch (e++) {		/* check quantum position */
    case 3:			/* one = is good enough in quantum 3 */
				/* make sure no data characters in remainder */
      for (; srcl; --srcl) switch (decode[*src++]) {
				/* ignore space, junk and extraneous padding */
      case WSP: case JNK: case PAD:
	break;
      default:			/* valid BASE64 data character */
	/* This indicates bad MIME.  One way that it can be caused is if
	   a single-section message was BASE64 encoded and then something
	   (e.g. a mailing list processor) appended text.  The problem is
	   that in 1 out of 3 cases, there is no padding and hence no way
	   to detect the end of the data.  Consequently, prudent software
	   will always encapsulate a BASE64 segment inside a MULTIPART.
	   */
	sprintf (tmp,"Possible data truncation in rfc822_base64(): %.80s",
		 (char *) src - 1);
	if (s = strpbrk (tmp,"\015\012")) *s = NIL;
	mm_log (tmp,PARSE);
	srcl = 1;		/* don't issue any more messages */
	break;
      }
      break;
    case 2:			/* expect a second = in quantum 2 */
      if (srcl && (*src == '=')) break;
    default:			/* impossible quantum position */
      fs_give (&ret);
      return NIL;
    }
    break;
  case JNK:			/* junk character */
    fs_give (&ret);
    return NIL;
  }
  *len = d - (char *) ret;	/* calculate data length */
  *d = '\0';			/* NUL terminate just in case */
  return ret;			/* return the string */
}

/* Convert binary contents to BASE64
 * Accepts: source
 *	    length of source
 *	    pointer to return destination length
 * Returns: destination as BASE64
 */

unsigned char *rfc822_binary (void *src,unsigned long srcl,unsigned long *len)
{
  unsigned char *ret,*d;
  unsigned char *s = (unsigned char *) src;
  char *v = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  unsigned long i = ((srcl + 2) / 3) * 4;
  *len = i += 2 * ((i / 60) + 1);
  d = ret = (unsigned char *) fs_get ((size_t) ++i);
				/* process tuplets */
  for (i = 0; srcl >= 3; s += 3, srcl -= 3) {
    *d++ = v[s[0] >> 2];	/* byte 1: high 6 bits (1) */
				/* byte 2: low 2 bits (1), high 4 bits (2) */
    *d++ = v[((s[0] << 4) + (s[1] >> 4)) & 0x3f];
				/* byte 3: low 4 bits (2), high 2 bits (3) */
    *d++ = v[((s[1] << 2) + (s[2] >> 6)) & 0x3f];
    *d++ = v[s[2] & 0x3f];	/* byte 4: low 6 bits (3) */
    if ((++i) == 15) {		/* output 60 characters? */
      i = 0;			/* restart line break count, insert CRLF */
      *d++ = '\015'; *d++ = '\012';
    }
  }
  if (srcl) {
    *d++ = v[s[0] >> 2];	/* byte 1: high 6 bits (1) */
				/* byte 2: low 2 bits (1), high 4 bits (2) */
    *d++ = v[((s[0] << 4) + (--srcl ? (s[1] >> 4) : 0)) & 0x3f];
				/* byte 3: low 4 bits (2), high 2 bits (3) */
    *d++ = srcl ? v[((s[1] << 2) + (--srcl ? (s[2] >> 6) : 0)) & 0x3f] : '=';
				/* byte 4: low 6 bits (3) */
    *d++ = srcl ? v[s[2] & 0x3f] : '=';
    if (srcl) srcl--;		/* count third character if processed */
    if ((++i) == 15) {		/* output 60 characters? */
      i = 0;			/* restart line break count, insert CRLF */
      *d++ = '\015'; *d++ = '\012';
    }
  }
  *d++ = '\015'; *d++ = '\012';	/* insert final CRLF */
  *d = '\0';			/* tie off string */
  if (((unsigned long) (d - ret)) != *len) fatal ("rfc822_binary logic flaw");
  return ret;			/* return the resulting string */
}

/* Convert QUOTED-PRINTABLE contents to 8BIT
 * Accepts: source
 *	    length of source
 * 	    pointer to return destination length
 * Returns: destination as 8-bit text or NIL if error
 */

unsigned char *rfc822_qprint (unsigned char *src,unsigned long srcl,
			      unsigned long *len)
{
  char tmp[MAILTMPLEN];
  unsigned int bogon = NIL;
  unsigned char *ret = (unsigned char *) fs_get ((size_t) srcl + 1);
  unsigned char *d = ret;
  unsigned char *t = d;
  unsigned char *s = src;
  unsigned char c,e;
  *len = 0;			/* in case we return an error */
				/* until run out of characters */
  while (((unsigned long) (s - src)) < srcl) {
    switch (c = *s++) {		/* what type of character is it? */
    case '=':			/* quoting character */
      if (((unsigned long) (s - src)) < srcl) switch (c = *s++) {
      case '\0':		/* end of data */
	s--;			/* back up pointer */
	break;
      case '\015':		/* non-significant line break */
	if ((((unsigned long) (s - src)) < srcl) && (*s == '\012')) s++;
      case '\012':		/* bare LF */
	t = d;			/* accept any leading spaces */
	break;
      default:			/* two hex digits then */
	if (!(isxdigit (c) && (((unsigned long) (s - src)) < srcl) &&
	      (e = *s++) && isxdigit (e))) {
	  /* This indicates bad MIME.  One way that it can be caused is if
	     a single-section message was QUOTED-PRINTABLE encoded and then
	     something (e.g. a mailing list processor) appended text.  The
	     problem is that there is no way to determine where the encoded
	     data ended and the appended crud began.  Consequently, prudent
	     software will always encapsulate a QUOTED-PRINTABLE segment
	     inside a MULTIPART.
	   */
	  if (!bogon++) {	/* only do this once */
	    sprintf (tmp,"Invalid quoted-printable sequence: =%.80s",
		   (char *) s - 1);
	    mm_log (tmp,PARSE);
	  }
	  *d++ = '=';		/* treat = as ordinary character */
	  *d++ = c;		/* and the character following */
	  t = d;		/* note point of non-space */
	  break;
	}
	*d++ = hex2byte (c,e);	/* merge the two hex digits */
	t = d;			/* note point of non-space */
	break;
      }
      break;
    case ' ':			/* space, possibly bogus */
      *d++ = c;			/* stash the space but don't update s */
      break;
    case '\015':		/* end of line */
    case '\012':		/* bare LF */
      d = t;			/* slide back to last non-space, drop in */
    default:
      *d++ = c;			/* stash the character */
      t = d;			/* note point of non-space */
    }      
  }
  *d = '\0';			/* tie off results */
  *len = d - ret;		/* calculate length */
  return ret;			/* return the string */
}

/* Convert 8BIT contents to QUOTED-PRINTABLE
 * Accepts: source
 *	    length of source
 * 	    pointer to return destination length
 * Returns: destination as quoted-printable text
 */

#define MAXL (size_t) 75	/* 76th position only used by continuation = */

unsigned char *rfc822_8bit (unsigned char *src,unsigned long srcl,
			    unsigned long *len)
{
  unsigned long lp = 0;
  unsigned char *ret = (unsigned char *)
    fs_get ((size_t) (3*srcl + 3*(((3*srcl)/MAXL) + 1)));
  unsigned char *d = ret;
  char *hex = "0123456789ABCDEF";
  unsigned char c;
  while (srcl--) {		/* for each character */
				/* true line break? */
    if (((c = *src++) == '\015') && (*src == '\012') && srcl) {
      *d++ = '\015'; *d++ = *src++; srcl--;
      lp = 0;			/* reset line count */
    }
    else {			/* not a line break */
				/* quoting required? */
      if (iscntrl (c) || (c == 0x7f) || (c & 0x80) || (c == '=') ||
	  ((c == ' ') && (*src == '\015'))) {
	if ((lp += 3) > MAXL) {	/* yes, would line overflow? */
	  *d++ = '='; *d++ = '\015'; *d++ = '\012';
	  lp = 3;		/* set line count */
	}
	*d++ = '=';		/* quote character */
	*d++ = hex[c >> 4];	/* high order 4 bits */
	*d++ = hex[c & 0xf];	/* low order 4 bits */
      }
      else {			/* ordinary character */
	if ((++lp) > MAXL) {	/* would line overflow? */
	  *d++ = '='; *d++ = '\015'; *d++ = '\012';
	  lp = 1;		/* set line count */
	}
	*d++ = c;		/* ordinary character */
      }
    }
  }
  *d = '\0';			/* tie off destination */
  *len = d - ret;		/* calculate true size */
				/* try to give some space back */
  fs_resize ((void **) &ret,(size_t) *len + 1);
  return ret;
}

/* Legacy Routines */

/*
 * WARNING: These routines are for compatibility with old software only.
 *
 * Their use in new software is to be avoided.
 *
 * These interfaces do not provide satisfactory buffer checking.  In
 * versions of c-client prior to imap-2005, they did not provide any
 * buffer checking at all.
 *
 * As a half-hearted attempt, these new compatability functions for the
 * legacy interfaces limit what they write to size SENDBUFLEN and will
 * fatal() if more than that is written.  However, that isn't good enough
 * since several of these functions *append* to the buffer, and return an
 * updated pointer.  Consequently, there is no way of knowing what the
 * actual available space is in the buffer, yet the function will still
 * write up to SENDBUFLEN bytes even if there is much less space actually
 * available.  The result is a buffer overflow.
 *
 * You won't get a buffer overflow if you never attempt to append using
 * these interfaces, but you can get the fatal() if it tries to write
 * more than SENDBUFLEN bytes.
 *
 * To avoid this problem, use the corresponding rfc822_output_???()
 * functions instead, e.g., rfc822_output_address() instead of
 * rfc822_address().
 *
 */

/* Flush routine, only called if overflow
 * Accepts: stream
 *	    string to output
 * Returns: never
 */

static long rfc822_legacy_soutr (void *stream,char *string)
{
  fatal ("rfc822.c legacy routine buffer overflow");
  return NIL;
}

/* Legacy write RFC 2822 header from message structure
 * Accepts: scratch buffer to write into
 *	    message envelope
 *	    message body
 */

void rfc822_header (char *header,ENVELOPE *env,BODY *body)
{
  RFC822BUFFER buf;
				/* write at start of buffer */
  buf.end = (buf.beg = buf.cur = header) + SENDBUFLEN - 1;
  buf.f = rfc822_legacy_soutr;
  buf.s = NIL;
  rfc822_output_header (&buf,env,body,NIL,NIL);
  *buf.cur = '\0';		/* tie off buffer */
}


/* Legacy write RFC 2822 text from header line
 * Accepts: pointer to destination string pointer
 *	    pointer to header type
 *	    message to interpret
 *	    pointer to text
 */

void rfc822_header_line (char **header,char *type,ENVELOPE *env,char *text)
{
  RFC822BUFFER buf;
				/* append to buffer */
  buf.end = (buf.beg = buf.cur = *header + strlen (*header)) + SENDBUFLEN - 1;
  buf.f = rfc822_legacy_soutr;
  buf.s = NIL;
  rfc822_output_header_line (&buf,type,env->remail ? LONGT : NIL,text);
  *(*header = buf.cur) = '\0';	/* tie off buffer */
}

/* Legacy write RFC 2822 address from header line
 * Accepts: pointer to destination string pointer
 *	    pointer to header type
 *	    message to interpret
 *	    address to interpret
 */

void rfc822_address_line (char **header,char *type,ENVELOPE *env,ADDRESS *adr)
{
  RFC822BUFFER buf;
				/* append to buffer */
  buf.end = (buf.beg = buf.cur = *header + strlen (*header)) + SENDBUFLEN - 1;
  buf.f = rfc822_legacy_soutr;
  buf.s = NIL;
  rfc822_output_address_line (&buf,type,env->remail ? LONGT : NIL,adr,NIL);
  *(*header = buf.cur) = '\0';	/* tie off buffer */
}


/* Legacy write RFC 2822 address list
 * Accepts: pointer to destination string
 *	    address to interpret
 *	    header base if pretty-printing
 * Returns: end of destination string
 */

char *rfc822_write_address_full (char *dest,ADDRESS *adr,char *base)
{
  RFC822BUFFER buf;
				/* append to buffer */
  buf.end = (buf.beg = buf.cur = dest + strlen (dest)) + SENDBUFLEN - 1;
  buf.f = rfc822_legacy_soutr;
  buf.s = NIL;
  rfc822_output_address_list (&buf,adr,base ? dest - base : 0,NIL);
  *buf.cur = '\0';		/* tie off buffer */
  return buf.cur;
}


/* Legacy write RFC 2822 route-address to string
 * Accepts: pointer to destination string
 *	    address to interpret
 */

void rfc822_address (char *dest,ADDRESS *adr)
{
  RFC822BUFFER buf;
				/* append to buffer */
  buf.end = (buf.beg = buf.cur = dest + strlen (dest)) + SENDBUFLEN - 1;
  buf.f = rfc822_legacy_soutr;
  buf.s = NIL;
  rfc822_output_address (&buf,adr);
  *buf.cur = '\0';		/* tie off buffer */
}

/* Concatenate RFC 2822 string
 * Accepts: pointer to destination string
 *	    pointer to string to concatenate
 *	    list of special characters or NIL for dot-atom format
 */

void rfc822_cat (char *dest,char *src,const char *specials)
{
  RFC822BUFFER buf;
				/* append to buffer */
  buf.end = (buf.beg = buf.cur = dest + strlen (dest)) + SENDBUFLEN - 1;
  buf.f = rfc822_legacy_soutr;
  buf.s = NIL;
  rfc822_output_cat (&buf,src,specials);
  *buf.cur = '\0';		/* tie off buffer */
}


/* Legacy write body content header
 * Accepts: pointer to destination string pointer
 *	    pointer to body to interpret
 */

void rfc822_write_body_header (char **dst,BODY *body)
{
  RFC822BUFFER buf;
				/* append to buffer */
  buf.end = (buf.beg = buf.cur = *dst + strlen (*dst)) + SENDBUFLEN - 1;
  buf.f = rfc822_legacy_soutr;
  buf.s = NIL;
  rfc822_output_body_header (&buf,body);
  *(*dst = buf.cur) = '\0';	/* tie off buffer */
}

/* Legacy output RFC 822 message
 * Accepts: temporary buffer
 *	    envelope
 *	    body
 *	    I/O routine
 *	    stream for I/O routine
 *	    non-zero if 8-bit output desired
 * Returns: T if successful, NIL if failure
 */

long rfc822_output (char *t,ENVELOPE *env,BODY *body,soutr_t f,void *s,
		    long ok8bit)
{
  long ret;
  rfc822out_t r822o = (rfc822out_t) mail_parameters (NIL,GET_RFC822OUTPUT,NIL);
				/* call external RFC 2822 output generator */
  if (r822o) ret = (*r822o) (t,env,body,f,s,ok8bit);
  else {			/* output generator not armed */
    RFC822BUFFER buf;		/* use our own buffer rather than trust */
    char tmp[SENDBUFLEN+1];	/*  client to give us a big enough one */
    buf.f = f;
    buf.s = s;
    buf.end = (buf.beg = buf.cur = t) + SENDBUFLEN - 1;
    tmp[SENDBUFLEN] = '\0';	/* must have additional guard byte */
    ret = rfc822_output_full (&buf,env,body,ok8bit);
  }
  return ret;
}


/* Legacy output RFC 822 body
 * Accepts: body
 *	    I/O routine
 *	    stream for I/O routine
 * Returns: T if successful, NIL if failure
 */

long rfc822_output_body (BODY *body,soutr_t f,void *s)
{
  RFC822BUFFER buf;
  char tmp[SENDBUFLEN+1];
  buf.f = f;
  buf.s = s;
  buf.end = (buf.beg = buf.cur = tmp) + SENDBUFLEN;
  tmp[SENDBUFLEN] = '\0';	/* must have additional guard byte */
  return rfc822_output_text (&buf,body) && rfc822_output_flush (&buf);
}
