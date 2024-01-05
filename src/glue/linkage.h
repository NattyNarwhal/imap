/* 
 * Note that PHP at least scans linkage.h for if GSS support is here.
 * Because of this, put a specific one in place of this file on install.
 */

#ifdef HAVE_KERBEROS
#include "linkage_gss.h"
#else
extern DRIVER mboxdriver;
extern DRIVER imapdriver;
extern DRIVER nntpdriver;
extern DRIVER pop3driver;
extern DRIVER mixdriver;
extern DRIVER mxdriver;
extern DRIVER mbxdriver;
extern DRIVER tenexdriver;
extern DRIVER mtxdriver;
extern DRIVER mhdriver;
extern DRIVER mmdfdriver;
extern DRIVER unixdriver;
extern DRIVER newsdriver;
extern DRIVER philedriver;
extern DRIVER dummydriver;
extern AUTHENTICATOR auth_ext;
extern AUTHENTICATOR auth_md5;
extern AUTHENTICATOR auth_pla;
extern AUTHENTICATOR auth_log;
#endif