// SPDX-License-Identifier: Apache-2.0
// The Windows GeaOS host. Apps on Windows are bundled with the shell: the ones
// built next to this one (dist/windows/<id>/<Name>.exe) are its bundledApps(),
// and launching one starts that executable and exits this process -- the same
// one-process-per-app model the macOS target uses. Apps are never downloaded
// and loaded at run time on this target.
//
// An app that has not been built yet is not listed; build it first.
#include "geaos.h"
#include "win32_widgets.h"

#include <shellapi.h>

#include <string>
#include <vector>

namespace {

// The executable that plays Home.
constexpr const char *kHomeAppId = "app-launcher";

class Win32AppHost final : public gea::geaos::AppHost {
public:
	explicit Win32AppHost(std::string currentId) : currentId_(std::move(currentId)) {}

	const char *targetKey() const override { return "windows"; }
	// Bundled apps are built with this shell; there is nothing to mismatch.
	const char *abi() const override { return "windows-bundled"; }
	const char *firmwareId() const override { return currentId_.c_str(); }
	const char *runningAppId() const override { return currentId_ == kHomeAppId ? "" : currentId_.c_str(); }

	std::vector<gea::geaos::InstalledApp> bundledApps() const override
	{
		std::vector<gea::geaos::InstalledApp> apps;
		const std::wstring dist = distDirectory();
		if (dist.empty()) return apps;
		WIN32_FIND_DATAW data{};
		HANDLE find = FindFirstFileW((dist + L"\\*").c_str(), &data);
		if (find == INVALID_HANDLE_VALUE) return apps;
		do {
			if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
			const std::string id = gea::win32::fromWide(data.cFileName);
			if (!gea::geaos::isValidAppId(id) || id == kHomeAppId) continue;
			const std::wstring executable = findExecutableForAppId(data.cFileName);
			if (executable.empty()) continue;
			gea::geaos::InstalledApp app;
			app.manifest.id = id;
			std::wstring name = executable.substr(executable.find_last_of(L"\\/") + 1);
			if (name.size() > 4) name.resize(name.size() - 4);  // drop ".exe"
			app.manifest.name = name.empty() ? id : gea::win32::fromWide(name);
			app.manifest.abi = abi();
			app.volumeId = "bundle";
			app.location = gea::win32::fromWide(executable);
			apps.push_back(std::move(app));
		} while (FindNextFileW(find, &data));
		FindClose(find);
		return apps;
	}

	bool launch(const gea::geaos::InstalledApp &app, std::string &error) override { return open(app.manifest.id, error); }

	bool home(std::string &error) override
	{
		if (currentId_ == kHomeAppId) {
			error = "Home is already running";
			return false;
		}
		return open(kHomeAppId, error);
	}

private:
	std::string currentId_;

	static bool open(const std::string &appId, std::string &error)
	{
		const std::wstring executable = findExecutableForAppId(gea::win32::toWide(appId));
		if (executable.empty()) {
			OutputDebugStringW((L"[gea-launch] no built .exe for '" + gea::win32::toWide(appId) + L"' under dist/windows\n").c_str());
			error = "no built app for " + appId;
			return false;
		}
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process{};
		std::wstring commandLine = L"\"" + executable + L"\"";
		if (!CreateProcessW(executable.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
			error = "could not start " + gea::win32::fromWide(executable);
			return false;
		}
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);
		PostQuitMessage(0);
		return true;
	}

	// <dist>/<currentId>/<Name>.exe: two parent traversals land on the folder
	// whose children are app ids.
	static std::wstring distDirectory()
	{
		wchar_t module[MAX_PATH]{};
		GetModuleFileNameW(nullptr, module, MAX_PATH);
		std::wstring path = module;
		const size_t slash = path.find_last_of(L"\\/");
		if (slash == std::wstring::npos) return std::wstring();
		const std::wstring appDir = path.substr(0, slash);
		const size_t parent = appDir.find_last_of(L"\\/");
		if (parent == std::wstring::npos) return std::wstring();
		return appDir.substr(0, parent);
	}

	// <dist>/<appId>/*.exe
	static std::wstring findExecutableForAppId(const std::wstring &appId)
	{
		const std::wstring dist = distDirectory();
		if (dist.empty()) return std::wstring();
		const std::wstring target = dist + L"\\" + appId;
		WIN32_FIND_DATAW data{};
		HANDLE find = FindFirstFileW((target + L"\\*.exe").c_str(), &data);
		if (find == INVALID_HANDLE_VALUE) return std::wstring();
		std::wstring result = target + L"\\" + data.cFileName;
		FindClose(find);
		return result;
	}
};

}  // namespace

namespace gea::win32 {

void installAppLauncherPlatform(const char *currentAppId)
{
	static Win32AppHost *instance = nullptr;
	if (instance) return;
	instance = new Win32AppHost(currentAppId ? currentAppId : "");
	gea::geaos::Launcher::setHost(instance);
}

}  // namespace gea::win32
