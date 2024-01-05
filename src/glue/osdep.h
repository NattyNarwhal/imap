#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#ifdef HAVE_USTAT_H
#include <ustat.h>
#endif
/* for unix.c */
#include <utime.h>

/* To avoid having to redefine it everywhere */
#define CHUNKSIZE 65536

/* Awful workaround for dummy.c */
#define direct dirent

/* IPv6 */
#include <sys/socket.h>
#include <netdb.h>

/* Don't include mail.h and c-client.h here */
#include "env_unix.h"
#include "fs.h"
#include "ftl.h"
#include "nl.h"
#include "tcp_unix.h"
#include "tcp.h"
/* Internal usage */
#ifdef NEED_FLOCKSIM
#include "flocksim.h"
#endif