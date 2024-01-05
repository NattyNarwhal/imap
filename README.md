# Modern UW IMAP Client Library

This is a cleaned up version of the University of Washington's IMAP package,
since the original package was unmaintained.
In particular, it does:

* Merges all of Debian's patches.
  * Debian seems to have the most extensive set of patches for UW IMAP.
* Modern CMake build system.
  * Automatically detects OpenSSL.
  * Assumes modern platform support (i.e. IPv6).
  * Builds with no patching on modern operating systems (i.e. macOS/arm64).
* Removed massively unmaintained tools and daemons
  * You should use something like Dovecot instead if you were using these.
* Cleaned up OS support

It shouldn't be used by new applications, but things like PHP's IMAP extension
still use it.

The files in `docs/` are assumed to be stale regarding OS support.

## Building

Use CMake.

## TODO

* Reintegrate Windows support
* Make features optional (Kerberos, PAM, OpenSSL)
* Cleanup
  * Remove functionality only ever used by servers
  * Run clang-format
  * Clean up warnings, -Werror it