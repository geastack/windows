// POSIX <dirent.h> for the Win32 target. The framework's host layer walks
// directories with opendir/readdir (host/host/image.cpp, tile_loader.cpp);
// MSVC's CRT has no dirent, so this header supplies the four functions those
// files use over FindFirstFileW. It lives on the target's include path only.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdlib>
#include <cstring>
#include <string>

#define DT_UNKNOWN 0
#define DT_DIR 4
#define DT_REG 8

struct dirent {
	unsigned char d_type;
	char d_name[MAX_PATH * 3];
};

struct DIR {
	HANDLE find = INVALID_HANDLE_VALUE;
	WIN32_FIND_DATAW data{};
	bool pending = false;
	dirent entry{};
};

inline DIR *opendir(const char *path)
{
	if (!path) return nullptr;
	std::string pattern = path;
	if (pattern.empty()) pattern = ".";
	if (pattern.back() != '/' && pattern.back() != '\\') pattern.push_back('\\');
	pattern += "*";
	int needed = MultiByteToWideChar(CP_UTF8, 0, pattern.c_str(), -1, nullptr, 0);
	if (needed <= 0) return nullptr;
	std::wstring wide(static_cast<size_t>(needed), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, pattern.c_str(), -1, wide.data(), needed);
	DIR *dir = new DIR();
	dir->find = FindFirstFileW(wide.c_str(), &dir->data);
	if (dir->find == INVALID_HANDLE_VALUE) {
		delete dir;
		return nullptr;
	}
	dir->pending = true;
	return dir;
}

inline dirent *readdir(DIR *dir)
{
	if (!dir || dir->find == INVALID_HANDLE_VALUE) return nullptr;
	for (;;) {
		if (!dir->pending) {
			if (!FindNextFileW(dir->find, &dir->data)) return nullptr;
		}
		dir->pending = false;
		WideCharToMultiByte(CP_UTF8, 0, dir->data.cFileName, -1, dir->entry.d_name, sizeof(dir->entry.d_name), nullptr, nullptr);
		dir->entry.d_type = (dir->data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? DT_DIR : DT_REG;
		return &dir->entry;
	}
}

inline int closedir(DIR *dir)
{
	if (!dir) return -1;
	if (dir->find != INVALID_HANDLE_VALUE) FindClose(dir->find);
	delete dir;
	return 0;
}

inline void rewinddir(DIR *) {}
