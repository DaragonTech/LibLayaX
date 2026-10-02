/* Minimal stand-in for the macOS SDK's <tzfile.h>, which zig's bundled macOS headers lack.
 * ICU's putil.cpp only needs these two paths. */
#ifndef LAYA_STUB_TZFILE_H
#define LAYA_STUB_TZFILE_H
#define TZDIR "/usr/share/zoneinfo"
#define TZDEFAULT "/etc/localtime"
#endif
