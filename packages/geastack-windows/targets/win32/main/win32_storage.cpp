// SPDX-License-Identifier: Apache-2.0
// File-backed StorageService: persistence for the app-facing `localStorage`
// (one opaque blob) and for device-settings strings (a small chunked KV file).
//
// Layout: %GEA_WINDOWS_STORAGE_DIR%, else %LOCALAPPDATA%\gea\<app-id>. Files
// localstorage.bin and settings.bin. Writes are atomic (temp file + ReplaceFile
// / MoveFileEx) so a crash mid-write never corrupts the previous state.

#include "services/storage_service.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace gea::win32 {
// Set by win32_main.cpp before StorageService::init.
std::string &storageAppId()
{
	static std::string id = "app";
	return id;
}
}  // namespace gea::win32

namespace {

std::wstring widen(const std::string &utf8)
{
	if (utf8.empty()) return std::wstring();
	const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
	std::wstring wide(static_cast<size_t>(needed > 0 ? needed : 0), L'\0');
	if (needed > 0) MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), needed);
	return wide;
}

std::wstring storageDir()
{
	static std::wstring dir = [] {
		wchar_t buffer[MAX_PATH] = {0};
		if (GetEnvironmentVariableW(L"GEA_WINDOWS_STORAGE_DIR", buffer, MAX_PATH) > 0 && buffer[0]) return std::wstring(buffer);
		PWSTR local = nullptr;
		std::wstring base;
		if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)) && local) {
			base = local;
			CoTaskMemFree(local);
		} else {
			base = L".";
		}
		return base + L"\\gea\\" + widen(gea::win32::storageAppId());
	}();
	return dir;
}

bool ensureDir(const std::wstring &path)
{
	std::wstring partial;
	for (size_t i = 0; i <= path.size(); i++) {
		if (i < path.size() && path[i] != L'\\' && path[i] != L'/') {
			partial.push_back(path[i]);
			continue;
		}
		if (!partial.empty() && partial.back() != L':') {
			if (!CreateDirectoryW(partial.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
		}
		if (i < path.size()) partial.push_back(L'\\');
	}
	return true;
}

bool readFile(const std::wstring &path, std::string &out)
{
	HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	if (file == INVALID_HANDLE_VALUE) return false;
	LARGE_INTEGER size{};
	if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > (256ll << 20)) {
		CloseHandle(file);
		return false;
	}
	out.resize(static_cast<size_t>(size.QuadPart));
	DWORD read = 0;
	const bool ok = size.QuadPart == 0 || (ReadFile(file, out.data(), static_cast<DWORD>(out.size()), &read, nullptr) && read == out.size());
	CloseHandle(file);
	if (!ok) out.clear();
	return ok;
}

bool writeFileAtomic(const std::wstring &path, const std::string &data)
{
	if (!ensureDir(storageDir())) return false;
	const std::wstring temp = path + L".tmp";
	HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) return false;
	DWORD written = 0;
	const bool wrote = data.empty() || (WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size());
	const bool flushed = FlushFileBuffers(file) != FALSE;
	CloseHandle(file);
	if (!wrote || !flushed) {
		DeleteFileW(temp.c_str());
		return false;
	}
	if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		DeleteFileW(temp.c_str());
		return false;
	}
	return true;
}

std::wstring localStoragePath() { return storageDir() + L"\\localstorage.bin"; }
std::wstring settingsPath() { return storageDir() + L"\\settings.bin"; }

std::unordered_map<std::string, std::string> &settings()
{
	static std::unordered_map<std::string, std::string> map = [] {
		std::unordered_map<std::string, std::string> loaded;
		std::string blob;
		if (readFile(settingsPath(), blob)) {
			size_t pos = 0;
			auto readChunk = [&](std::string &out) {
				if (pos + sizeof(std::uint32_t) > blob.size()) return false;
				std::uint32_t len = 0;
				std::memcpy(&len, blob.data() + pos, sizeof(len));
				pos += sizeof(len);
				if (pos + len > blob.size()) return false;
				out.assign(blob.data() + pos, len);
				pos += len;
				return true;
			};
			std::string k, v;
			while (readChunk(k) && readChunk(v)) loaded[k] = v;
		}
		return loaded;
	}();
	return map;
}

bool persistSettings()
{
	std::string blob;
	for (const auto &kv : settings()) {
		for (const std::string *part : {&kv.first, &kv.second}) {
			const std::uint32_t len = static_cast<std::uint32_t>(part->size());
			char header[sizeof(len)];
			std::memcpy(header, &len, sizeof(len));
			blob.append(header, sizeof(header));
			blob.append(*part);
		}
	}
	return writeFileAtomic(settingsPath(), blob);
}

}  // namespace

namespace gea::framework::services {

bool StorageService::init() { return ensureDir(storageDir()); }

bool StorageService::getString(const char *key, char *buf, unsigned capacity)
{
	if (!key || !buf || capacity == 0) return false;
	const auto &map = settings();
	const auto it = map.find(key);
	if (it == map.end()) return false;
	strncpy_s(buf, capacity, it->second.c_str(), capacity - 1);
	return true;
}

bool StorageService::setString(const char *key, const char *value)
{
	if (!key) return false;
	settings()[key] = value ? value : "";
	return persistSettings();
}

bool StorageService::loadKv(std::string &out)
{
	out.clear();
	readFile(localStoragePath(), out);
	return true;
}

void StorageService::saveKv(const std::string &blob)
{
	if (blob.empty()) {
		DeleteFileW(localStoragePath().c_str());
		return;
	}
	writeFileAtomic(localStoragePath(), blob);
}

}  // namespace gea::framework::services
