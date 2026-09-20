// SPDX-License-Identifier: Apache-2.0
// Host shell execution for gea apps (`DeviceControl.exec`) and the
// Windows-native `runDeviceCommand`. Both run the command through cmd.exe and
// return its combined output.
//
// DeviceControl.exec is NON-BLOCKING, as on macOS: the frame loop runs on the
// UI thread and a command can take seconds, so exec() returns the last cached
// result for a command and refreshes it on a worker thread.
#include "host/device_control.h"
#include "win32_widgets.h"

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>

namespace gea::win32 {

std::string runShellCommandBlocking(const std::string &command)
{
	SECURITY_ATTRIBUTES attributes{};
	attributes.nLength = sizeof(attributes);
	attributes.bInheritHandle = TRUE;
	HANDLE readEnd = nullptr;
	HANDLE writeEnd = nullptr;
	if (!CreatePipe(&readEnd, &writeEnd, &attributes, 0)) return std::string();
	SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);
	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
	startup.hStdOutput = writeEnd;
	startup.hStdError = writeEnd;
	startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	startup.wShowWindow = SW_HIDE;
	PROCESS_INFORMATION process{};
	std::wstring commandLine = L"cmd.exe /d /s /c \"" + toWide(command) + L"\"";
	const BOOL created = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
	CloseHandle(writeEnd);
	std::string output;
	if (created) {
		char buffer[4096];
		DWORD read = 0;
		while (ReadFile(readEnd, buffer, sizeof(buffer), &read, nullptr) && read > 0) output.append(buffer, read);
		WaitForSingleObject(process.hProcess, INFINITE);
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);
	}
	CloseHandle(readEnd);
	while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) output.pop_back();
	return output;
}

}  // namespace gea::win32

namespace {
std::mutex g_mutex;
std::map<std::string, std::string> g_cache;
std::set<std::string> g_inflight;
}  // namespace

std::string gea::host::DeviceControlFacade::exec(const std::string &command) const
{
	std::string cached;
	bool shouldDispatch = false;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		auto it = g_cache.find(command);
		if (it != g_cache.end()) cached = it->second;
		if (g_inflight.find(command) == g_inflight.end()) {
			g_inflight.insert(command);
			shouldDispatch = true;
		}
	}
	if (shouldDispatch) {
		std::thread([command] {
			std::string result = gea::win32::runShellCommandBlocking(command);
			std::lock_guard<std::mutex> lock(g_mutex);
			g_cache[command] = result;
			g_inflight.erase(command);
		}).detach();
	}
	return cached;
}
