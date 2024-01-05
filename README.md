# Modern UW IMAP Client Library

This is a cleaned up version of the University of Washington's IMAP package,
since the original package was unmaintained.
In particular, it does:

* Merges all of Debian's patches.
  * Debian seems to have the most extensive set of patches for UW IMAP.
* Modern CMake build system.
  * Automatically detects OpenSSL, PAM, and Kerberos.
  * Assumes modern platform support (i.e. IPv6).
* Removed massively unmaintained tools and daemons.
  * You should use something like Dovecot instead if you were using these.
* Cleaned up OS support, dropping decrepit shims and simplifying build.
  * This package should work on arm64 macOS and Linux, as well as AIX.

It shouldn't be used by new applications, but things like PHP's IMAP extension
still use it.
This package passes the PHP IMAP extension unit tests, so it works as a drop-in
replacement.

The files in `docs/` are assumed to be stale regarding OS support.

## Building

Use CMake. The following build flags are interesting:

* `BUILD_MTEST`: Boolean, off by default. Builds the mtest program, which
  exercises c-client functionality and tests linking. Not installed.
* `CMAKE_INSTALL_PREFIX`: Path to where you wish to install c-client.
* `USE_KERBEROS`: Boolean, on by default. Builds with Kerberos support.
  If Kerberos isn't found when this is on, then generation will fail.
* `USE_OPENSSL`: Boolean, on by default. Builds with OpenSSL support on Unix.
  If OpenSSL isn't found when this is on, then generation will fail.

## TODO

* Reintegrate Windows support
* Cleanup
  * Remove functionality only ever used by servers
  * Run clang-format
  * Clean up warnings, -Werror it
