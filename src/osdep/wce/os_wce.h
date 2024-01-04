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
 * Program:	Operating-system dependent routines -- WCE version
 *
 * Author:	Mark Crispin
 *		Networks and Distributed Computing
 *		Computing & Communications
 *		University of Washington
 *		Administration Building, AG-44
 *		Seattle, WA  98195
 *		Internet: MRC@CAC.Washington.EDU
 *
 * Date:	11 May 1989
 * Last Edited:	30 August 2006
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
				/* missing in string.h */
_CRTIMP char * __cdecl strpbrk (const char *, const char *);
_CRTIMP	char * __cdecl strrchr(const char *, int);
#include <sys\types.h>
#undef ERROR
#include <windows.h>
#undef ERROR
#define ERROR (long) 2		/* must match mail.h */
#include <time.h>
#include <io.h>

#define gethostid clock
#define	WSA_VERSION	((1 << 8) | 1)

#include "env_wce.h"
#include "fs.h"
#include "ftl.h"
#include "nl.h"
#include "tcp.h"

#undef noErr
#undef MAC
