// <sys/stat.h> for the Win32 target: the CRT header plus the POSIX pieces the
// framework's host layer expects (S_ISDIR/S_ISREG, a two-argument mkdir).
#pragma once
#include_next <sys/stat.h>
#include <direct.h>

#ifndef S_ISDIR
#define S_ISDIR(mode) (((mode) & _S_IFMT) == _S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(mode) (((mode) & _S_IFMT) == _S_IFREG)
#endif

#ifdef __cplusplus
inline int gea_win32_mkdir(const char *path, int) { return _mkdir(path); }
#define mkdir gea_win32_mkdir
#endif
