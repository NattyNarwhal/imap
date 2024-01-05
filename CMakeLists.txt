cmake_minimum_required(VERSION 3.23)

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
	target_sources(c-client PRIVATE
		# drivers that it didn't include otherwise
		src/osdep/unix/tenex.c
		src/osdep/unix/phile.c
		src/osdep/unix/mbx.c
		src/osdep/unix/mh.c
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
		src/osdep/unix/tz_bsd.c # same
		# unix proto
		src/osdep/unix/unix.c
		src/osdep/unix/pseudo.c
		src/osdep/unix/fdstring.c
		src/osdep/unix/dummy.c
	)

	# XXX: Kerberos
	# old Makefile.gss bits: -lgssapi_krb5 -lkrb5 -lcrypto -lcom_err, -DGSS_C_NT_HOSTBASED_SERVICE=gss_nt_service_name -DKRB5_DEPRECATED=1

	# SSL library
	find_package(OpenSSL)
	if (OPENSSL_FOUND)
		target_link_libraries(c-client
			OpenSSL::SSL
		)
		target_sources(c-client PRIVATE
			src/osdep/unix/ssl_unix.c
		)
	else()
		target_sources(c-client PRIVATE
			src/osdep/unix/ssl_none.c
		)
	endif()

	# Authentication library
	find_library(PAM_LIB pam)
	if (PAM_LIB)
		target_sources(c-client PRIVATE
			src/osdep/unix/ckp_pam.c
		)

		target_link_libraries(c-client
			${PAM_LIB}
		)
	else()
		# XXX: not PAM but still has auth, if even relevant
		target_sources(c-client PRIVATE
			src/osdep/unix/ckp_nul.c
		)
	endif()

	# Extremely gross: mm_* callbacks
	# XXX: Assumes ld64 or binutils
	if(CMAKE_SYSTEM_NAME STREQUAL Darwin)
		target_link_options(c-client PRIVATE "-Wl,-undefined,dynamic_lookup")
	else()
		target_link_options(c-client PRIVATE "-Wl,--unresolved-symbols=ignore-in-shared-libs")
	endif()
endif()

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
	src/glue/*.h
	# XXX: Platform specific AF
	src/osdep/unix/env_unix.h
	src/osdep/unix/tcp_unix.h
)
install(FILES ${HEADERS} DESTINATION include/c-client)
