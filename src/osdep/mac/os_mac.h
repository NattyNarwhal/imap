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
 * Program:	Operating-system dependent routines -- Macintosh version
 *
 * Author:	Mark Crispin
 *		Networks and Distributed Computing
 *		Computing & Communications
 *		University of Washington
 *		Administration Building, AG-44
 *		Seattle, WA  98195
 *		Internet: MRC@CAC.Washington.EDU
 *
 * Date:	26 January 1992
 * Last Edited:	30 August 2006
 */


/*  This is a totally new operating-system dependent module for the Macintosh,
 * written using THINK C on my Mac PowerBook-100 in my free time.
 * Unlike earlier efforts, this version requires no external TCP library.  It
 * also takes advantage of the Map panel in System 7 for the timezone.
 */

#include <stdlib.h>
#include <string.h>
#include <types.h>
#include <unix.h>
#ifndef noErr
#include <Desk.h>
#include <Devices.h>
#include <Errors.h>
#include <Events.h>
#include <Fonts.h>
#include <Memory.h>
#include <Menus.h>
#include <ToolUtils.h>
#include <Windows.h>
#endif

#define L_SET SEEK_SET
#define L_INCR SEEK_CUR
#define L_XTND SEEK_END

extern short resolveropen;	/* make this global so caller can sniff */

#include "env_mac.h"
#include "fs.h"
#include "ftl.h"
#include "nl.h"
#include "tcp.h"

#define TCPDRIVER "\p.IPP"

#define gethostid clock

long wait (void);
long random (void);
