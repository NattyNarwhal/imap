/* Now static. */

#include "auth_ext.c"
#include "auth_md5.c"
#include "auth_pla.c"
#include "auth_log.c"
#ifdef HAVE_KERBEROS
/* this file must be included before auth_gss */
#include "kerb_mit.c"
#include "auth_gss.c"
#endif