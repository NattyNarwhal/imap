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
 * Program:	IMAP Wildcard Matching Routines (case-independent)
 *
 * Author:	Mark Crispin
 *		Networks and Distributed Computing
 *		Computing & Communications
 *		University of Washington
 *		Administration Building, AG-44
 *		Seattle, WA  98195
 *		Internet: MRC@CAC.Washington.EDU
 *
 * Date:	15 June 2000
 * Last Edited:	30 August 2006
 */

/* Wildcard pattern match
 * Accepts: base string
 *	    pattern string
 *	    delimiter character
 * Returns: T if pattern matches base, else NIL
 */

long pmatch_full (unsigned char *s,unsigned char *pat,unsigned char delim)
{
  switch (*pat) {
  case '%':			/* non-recursive */
				/* % at end, OK if no inferiors */
    if (!pat[1]) return (delim && strchr (s,delim)) ? NIL : T;
                                /* scan remainder of string until delimiter */
    do if (pmatch_full (s,pat+1,delim)) return T;
    while ((*s != delim) && *s++);
    break;
  case '*':			/* match 0 or more characters */
    if (!pat[1]) return T;	/* * at end, unconditional match */
				/* scan remainder of string */
    do if (pmatch_full (s,pat+1,delim)) return T;
    while (*s++);
    break;
  case '\0':			/* end of pattern */
    return *s ? NIL : T;	/* success if also end of base */
  default:			/* match this character */
    return compare_uchar (*pat,*s) ? NIL : pmatch_full (s+1,pat+1,delim);
  }
  return NIL;
}

/* Directory pattern match
 * Accepts: base string
 *	    pattern string
 *	    delimiter character
 * Returns: T if base is a matching directory of pattern, else NIL
 */

long dmatch (unsigned char *s,unsigned char *pat,unsigned char delim)
{
  switch (*pat) {
  case '%':			/* non-recursive */
    if (!*s) return T;		/* end of base means have a subset match */
    if (!*++pat) return NIL;	/* % at end, no inferiors permitted */
				/* scan remainder of string until delimiter */
    do if (dmatch (s,pat,delim)) return T;
    while ((*s != delim) && *s++);
    if (*s && !s[1]) return T;	/* ends with delimiter, must be subset */
    return dmatch (s,pat,delim);/* do new scan */
  case '*':			/* match 0 or more characters */
    return T;			/* unconditional match */
  case '\0':			/* end of pattern */
    break;
  default:			/* match this character */
    if (*s) return compare_uchar (*pat,*s) ? NIL : dmatch (s+1,pat+1,delim);
				/* end of base, return if at delimiter */
    else if (*pat == delim) return T;
    break;
  }
  return NIL;
}
