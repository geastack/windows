// POSIX <unistd.h> for the Win32 target: the handful of calls the framework's
// desktop arms make (access/unlink/rmdir/getcwd/usleep/sleep), over the MSVC
// CRT equivalents in <io.h>/<direct.h>. On the target's include path only.
#pragma once

#include <direct.h>
#include <io.h>
#include <process.h>
#include <stdlib.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#ifndef F_OK
#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 0
#endif

#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#endif

typedef long long ssize_t;

inline int gea_win32_usleep(unsigned long usec)
{
	Sleep(static_cast<DWORD>((usec + 999) / 1000));
	return 0;
}
inline unsigned int gea_win32_sleep(unsigned int seconds)
{
	Sleep(seconds * 1000);
	return 0;
}
#define usleep gea_win32_usleep
#define sleep gea_win32_sleep
#define fsync _commit
#define fileno _fileno
