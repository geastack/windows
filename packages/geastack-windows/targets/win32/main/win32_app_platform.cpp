// SPDX-License-Identifier: Apache-2.0
// App launcher platform: `Apps.launch('foo')` opens dist/windows/foo/*.exe next
// to this executable's own dist folder and exits, the same one-process-per-app
// model the macOS target uses.
#include "apps.h"
#include "win32_widgets.h"

#include <shellapi.h>

#include <string>

namespace {

class Win32AppLauncherPlatform final : public gea::framework::apps::AppLauncherPlatform {
public:
	explicit Win32AppLauncherPlatform(std::string currentId) : currentId_(std::move(currentId)) {}

	const char *currentInstalledAppId() override { return currentId_.c_str(); }
	bool runningAppIsLauncher(const char *launcherAppId) override
	{
		return launcherAppId && currentId_ == launcherAppId;
	}

	bool launchInstalledApp(const char *appId) override
	{
		if (!appId || !appId[0]) return false;
		const std::wstring executable = findExecutableForAppId(gea::win32::toWide(appId));
		if (executable.empty()) {
			OutputDebugStringW((L"[gea-launch] no built .exe for '" + gea::win32::toWide(appId) + L"' under dist/windows\n").c_str());
			return false;
		}
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process{};
		std::wstring commandLine = L"\"" + executable + L"\"";
		if (!CreateProcessW(executable.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) return false;
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);
		PostQuitMessage(0);
		return true;
	}

private:
	std::string currentId_;

	// <dist>/<currentId>/<Name>.exe -> <dist>/<appId>/*.exe
	static std::wstring findExecutableForAppId(const std::wstring &appId)
	{
		wchar_t module[MAX_PATH]{};
		GetModuleFileNameW(nullptr, module, MAX_PATH);
		std::wstring path = module;
		const size_t slash = path.find_last_of(L"\/");
		if (slash == std::wstring::npos) return std::wstring();
		std::wstring appDir = path.substr(0, slash);
		const size_t parent = appDir.find_last_of(L"\/");
		if (parent == std::wstring::npos) return std::wstring();
		const std::wstring dist = appDir.substr(0, parent);
		const std::wstring target = dist + L"\\" + appId;
		WIN32_FIND_DATAW data{};
		HANDLE find = FindFirstFileW((target + L"\*.exe").c_str(), &data);
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
	static Win32AppLauncherPlatform *instance = nullptr;
	if (instance) return;
	instance = new Win32AppLauncherPlatform(currentAppId ? currentAppId : "");
	gea::framework::apps::AppManager::setPlatform(instance);
}

}  // namespace gea::win32
