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
 * Program:	BSD-style Time Zone String
 *
 * Author:	Mark Crispin
 *		Networks and Distributed Computing
 *		Computing & Communications
 *		University of Washington
 *		Administration Building, AG-44
 *		Seattle, WA  98195
 *		Internet: MRC@CAC.Washington.EDU
 *
 * Date:	30 August 1994
 * Last Edited:	30 August 2006
 */

#include "mail.h"

#include "osdep.h"

#include "misc.h"


/* Append local timezone name
 * Accepts: destination string
 */

void rfc822_timezone (char *s,void *t)
{
				/* append timezone from tm struct */
  sprintf (s + strlen (s)," (%.50s)",((struct tm *) t)->tm_zone);
}
