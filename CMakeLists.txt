cmake_minimum_required(VERSION 3.16)
include(CheckFunctionExists)
include(CheckIncludeFile)

option(BUILD_MTEST "Builds the mtest program that uses c-client" FALSE)
option(USE_KERBEROS "Use GSS/Kerberos" TRUE)
option(USE_OPENSSL "Use OpenSSL on Unix-likes" TRUE)

if(CMAKE_SYSTEM_NAME STREQUAL AIX OR CMAKE_SYSTEM_NAME STREQUAL OS400) # OS400 = IBM i PASE
	set(AIX TRUE)
endif()

project(c-client VERSION 2024.1 DESCRIPTION "Modern UW IMAP Library")

# auth_*.c is included by env_unix.c by way of auths.c
# XXX: Where did the drivers go?
add_library(c-client SHARED
	src/c-client/flstring.c
	src/c-client/imap4r1.c
	src/c-client/mail.c
	src/c-client/misc.c
	src/c-client/netmsg.c
	src/c-client/newsrc.c
	src/c-client/nntp.c
	src/c-client/pop3.c
	src/c-client/rfc822.c
	src/c-client/smanager.c
	src/c-client/smtp.c
	src/c-client/utf8.c
	src/c-client/utf8aux.c
)

if(WIN32)
	# XXX: Add Windows support here
else() # Unix
	# Missing on AIX somehow
	check_function_exists(flock HAVE_FLOCK_FUNCTION)
	if(NOT HAVE_FLOCK_FUNCTION)
		add_definitions(-DNEED_FLOCKSIM)
		target_sources(c-client PRIVATE src/osdep/unix/flocksim.c)
	endif()
	# In turn, this file is required by flocksim.c, but doesn't exist everywhere
	check_include_file("ustat.h" HAVE_USTAT_H)
	if(HAVE_USTAT_H)
		add_definitions(-DHAVE_USTAT_H)
	endif()

	target_sources(c-client PRIVATE
		# drivers that it didn't include otherwise
		src/osdep/unix/tenex.c
		src/osdep/unix/phile.c
		src/osdep/unix/mbx.c
		src/osdep/unix/mh.c
		src/osdep/unix/mix.c
		src/osdep/unix/mmdf.c
		src/osdep/unix/mtx.c
		src/osdep/unix/mx.c
		src/osdep/unix/news.c
		# platform specific, allegedly
		src/osdep/unix/tcp_unix.c
		src/osdep/unix/fs_unix.c
		src/osdep/unix/env_unix.c
		src/osdep/unix/ftl_unix.c
		src/osdep/unix/nl_unix.c
		src/osdep/unix/gr_waitp.c # either works but waitp is posix
		src/osdep/unix/sig_psx.c # shouldn't be a problem in 2024, right?
		src/osdep/unix/log_std.c # same, import from old tree if we need one
		# unix proto
		src/osdep/unix/unix.c
		src/osdep/unix/pseudo.c
		src/osdep/unix/fdstring.c
		src/osdep/unix/dummy.c
	)

	# System V vs. BSD timezone support (check is not ideal)
	if(AIX)
		target_sources(c-client PRIVATE src/osdep/unix/tz_sv4.c)
	else()
		target_sources(c-client PRIVATE	src/osdep/unix/tz_bsd.c)
	endif()

	# Kerberos: Only MIT flavour is known to be supported.
	# old Makefile.gss bits: -lgssapi_krb5 -lkrb5 -lcrypto -lcom_err
	check_include_file(gssapi/gssapi_krb5.h HAVE_KRB5_H)
	if(USE_KERBEROS AND HAVE_KRB5_H)
		# Workaround for PHP build system detecting Kerberos with a header grep
		install(FILES src/glue/linkage_gss.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/c-client RENAME linkage.h)
		# this will make auths.c include the needed files,
		# the defs after HAVE_KERBEROS are from the old build system
		add_definitions(-DHAVE_KERBEROS -DGSS_C_NT_HOSTBASED_SERVICE=gss_nt_service_name -DKRB5_DEPRECATED=1)

		# XXX: Consider linking against GSS.framework on Apple platforms
		find_library(KRB5_LIB krb5)
		if(KRB5_LIB)
			target_link_libraries(c-client ${KRB5_LIB})
		endif()
		find_library(GSSAPI_KRB5_LIB gssapi_krb5)
		if(GSSAPI_KRB5_LIB)
			target_link_libraries(c-client ${GSSAPI_KRB5_LIB})
		endif()
		message(STATUS "Building with Kerberos support")
	elseif(USE_KERBERROS)
		message(SEND_ERROR "Kerberos is enabled, but not found. Disable it to build without it.")
	else()
		install(FILES src/glue/linkage.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/c-client)
		message(STATUS "Building without Kerberos support")
	endif()

	# SSL library
	find_package(OpenSSL)
	if (USE_OPENSSL AND OPENSSL_FOUND)
		target_link_libraries(c-client OpenSSL::SSL)
		target_sources(c-client PRIVATE src/osdep/unix/ssl_unix.c)
		message(STATUS "Building with OpenSSL support")
	elseif(USE_OPENSSL)
		message(SEND_ERROR "OpenSSL is enabled, but not found. Disable it to build without it.")
	else()
		target_sources(c-client PRIVATE src/osdep/unix/ssl_none.c)
		message(STATUS "Building without OpenSSL support")
	endif()

	# Authentication library
	find_library(PAM_LIB pam)
	if (PAM_LIB)
		target_sources(c-client PRIVATE src/osdep/unix/ckp_pam.c)

		target_link_libraries(c-client ${PAM_LIB})

		message(STATUS "Building with PAM support")
	else()
		# XXX: not PAM but still has auth, if even relevant
		target_sources(c-client PRIVATE src/osdep/unix/ckp_nul.c)
		message(STATUS "Building without PAM support")
	endif()

	# Extremely gross: mm_* callbacks come from application, so don't link here
	if(APPLE)
		target_link_options(c-client PRIVATE "-Wl,-undefined,dynamic_lookup")
	elseif(AIX)
		target_link_options(c-client PRIVATE "-Wl,-brtl,-berok,-bnosymbolic-,-bM:SRE")
	else()
		target_link_options(c-client PRIVATE "-Wl,--unresolved-symbols=ignore-in-object-files")
	endif()
endif()

#set(CMAKE_C_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Werror -Wno-error=pointer-sign -Wno-error=parentheses")

set_target_properties(c-client PROPERTIES VERSION ${PROJECT_VERSION})
set_target_properties(c-client PROPERTIES SOVERSION ${PROJECT_VERSION_MAJOR})

target_include_directories(c-client PRIVATE src/glue)
target_include_directories(c-client PRIVATE src/c-client)
target_include_directories(c-client PRIVATE src/charset)
target_include_directories(c-client PRIVATE src/osdep/unix)

include(GNUInstallDirs)
install(TARGETS c-client
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
# XXX: Can we do it in the install above?
file(GLOB HEADERS
	src/c-client/*.h
	src/glue/osdep.h
	# XXX: Platform specific AF
	src/osdep/unix/env_unix.h
	src/osdep/unix/tcp_unix.h
)
install(FILES ${HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/c-client)

# mtest program
if(BUILD_MTEST)
	add_executable(mtest src/mtest/mtest.c)
	target_include_directories(mtest PRIVATE src/glue)
	target_include_directories(mtest PRIVATE src/c-client)
	target_include_directories(mtest PRIVATE src/charset)
	target_include_directories(mtest PRIVATE src/osdep/unix)
	target_link_libraries(mtest c-client)
	message(STATUS "Building mtest program")
endif()

# mlock program
if(NOT WIN32)
	add_executable(mlock src/mlock/mlock.c)
	set(LOCKPGM ${CMAKE_INSTALL_FULL_BINDIR}/mlock)
	install(TARGETS mlock RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
	message(STATUS "Building mlock program (needed for mailbox locking)")
endif()

# Currently only used for mlock, but generally useful anyways
configure_file(src/glue/config.h.in src/glue/config.h)
target_include_directories(c-client PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/src/glue")
