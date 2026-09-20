// SPDX-License-Identifier: Apache-2.0
// The raw Windows API groupings of @geastack/windows -- User32, Kernel32,
// Shell32, Dwmapi -- as the thunks the generated bridge header declares.
// Handles travel as doubles (an HWND fits a double's 53-bit mantissa on
// every Windows process), strings as UTF-8 std::string converted at the edge.

#include "gea/windows/native_bridge.h"

#include "../win32_main.h"
#include "../win32_widgets.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "advapi32.lib")

namespace {

HWND hwndOf(double handle)
{
	return reinterpret_cast<HWND>(static_cast<std::uintptr_t>(std::llround(handle)));
}

double handleOf(HWND hwnd)
{
	return static_cast<double>(reinterpret_cast<std::uintptr_t>(hwnd));
}

std::wstring wide(const std::string &utf8) { return gea::windows::text::toWide(utf8); }
std::string narrow(const std::wstring &text) { return gea::windows::text::fromWide(text); }

gea::windows::Win32::POINT pointOf(POINT point)
{
	return gea::windows::Win32::POINT{static_cast<double>(point.x), static_cast<double>(point.y)};
}

gea::windows::Win32::RECT rectOf(RECT rect)
{
	return gea::windows::Win32::RECT{static_cast<double>(rect.left), static_cast<double>(rect.top), static_cast<double>(rect.right),
	                                  static_cast<double>(rect.bottom)};
}

std::string readEnvironment(const wchar_t *name)
{
	DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
	if (needed == 0) return std::string();
	std::wstring value(needed, L'\0');
	GetEnvironmentVariableW(name, value.data(), needed);
	value.resize(needed - 1);
	return narrow(value);
}

}  // namespace

// --- User32 ------------------------------------------------------------------------------

namespace gea::windows::User32 {

double MessageBoxW(double hwnd, std::string text, std::string caption, double type)
{
	return static_cast<double>(::MessageBoxW(hwndOf(hwnd), wide(text).c_str(), wide(caption).c_str(), static_cast<UINT>(type)));
}
bool MessageBeep(double type) { return ::MessageBeep(static_cast<UINT>(type)) != FALSE; }
double GetSystemMetrics(double index) { return ::GetSystemMetrics(static_cast<int>(index)); }
double GetSystemMetricsForDpi(double index, double dpi) { return ::GetSystemMetricsForDpi(static_cast<int>(index), static_cast<UINT>(dpi)); }
double GetForegroundWindow() { return handleOf(::GetForegroundWindow()); }
double GetDesktopWindow() { return handleOf(::GetDesktopWindow()); }
double FindWindowW(std::string className, std::string windowName)
{
	const std::wstring cls = wide(className);
	const std::wstring name = wide(windowName);
	return handleOf(::FindWindowW(cls.empty() ? nullptr : cls.c_str(), name.empty() ? nullptr : name.c_str()));
}
bool IsWindow(double hwnd) { return ::IsWindow(hwndOf(hwnd)) != FALSE; }
bool IsWindowVisible(double hwnd) { return ::IsWindowVisible(hwndOf(hwnd)) != FALSE; }
bool SetWindowTextW(double hwnd, std::string text) { return ::SetWindowTextW(hwndOf(hwnd), wide(text).c_str()) != FALSE; }
std::string GetWindowTextW(double hwnd)
{
	HWND window = hwndOf(hwnd);
	const int length = ::GetWindowTextLengthW(window);
	if (length <= 0) return std::string();
	std::wstring text(static_cast<size_t>(length) + 1, L'\0');
	::GetWindowTextW(window, text.data(), length + 1);
	text.resize(static_cast<size_t>(length));
	return narrow(text);
}
bool ShowWindow(double hwnd, double command) { return ::ShowWindow(hwndOf(hwnd), static_cast<int>(command)) != FALSE; }
bool SetForegroundWindow(double hwnd) { return ::SetForegroundWindow(hwndOf(hwnd)) != FALSE; }
bool FlashWindow(double hwnd, bool invert) { return ::FlashWindow(hwndOf(hwnd), invert ? TRUE : FALSE) != FALSE; }
double GetDpiForWindow(double hwnd) { return ::GetDpiForWindow(hwndOf(hwnd)); }
double GetDpiForSystem() { return ::GetDpiForSystem(); }
gea::windows::Win32::POINT GetCursorPos()
{
	POINT point{};
	::GetCursorPos(&point);
	return pointOf(point);
}
bool SetCursorPos(double x, double y) { return ::SetCursorPos(static_cast<int>(x), static_cast<int>(y)) != FALSE; }
gea::windows::Win32::RECT GetWindowRect(double hwnd)
{
	RECT rect{};
	::GetWindowRect(hwndOf(hwnd), &rect);
	return rectOf(rect);
}
gea::windows::Win32::RECT GetClientRect(double hwnd)
{
	RECT rect{};
	::GetClientRect(hwndOf(hwnd), &rect);
	return rectOf(rect);
}
gea::windows::Win32::POINT ClientToScreen(double hwnd, gea::windows::Win32::POINT point)
{
	POINT native{static_cast<LONG>(point.x), static_cast<LONG>(point.y)};
	::ClientToScreen(hwndOf(hwnd), &native);
	return pointOf(native);
}
gea::windows::Win32::POINT ScreenToClient(double hwnd, gea::windows::Win32::POINT point)
{
	POINT native{static_cast<LONG>(point.x), static_cast<LONG>(point.y)};
	::ScreenToClient(hwndOf(hwnd), &native);
	return pointOf(native);
}
bool MoveWindow(double hwnd, double x, double y, double width, double height, bool repaint)
{
	return ::MoveWindow(hwndOf(hwnd), static_cast<int>(x), static_cast<int>(y), static_cast<int>(width), static_cast<int>(height), repaint ? TRUE : FALSE) != FALSE;
}
bool SetWindowPos(double hwnd, double insertAfter, double x, double y, double width, double height, double flags)
{
	return ::SetWindowPos(hwndOf(hwnd), hwndOf(insertAfter), static_cast<int>(x), static_cast<int>(y), static_cast<int>(width), static_cast<int>(height),
	                      static_cast<UINT>(flags)) != FALSE;
}
bool PostMessageW(double hwnd, double message, double wParam, double lParam)
{
	return ::PostMessageW(hwndOf(hwnd), static_cast<UINT>(message), static_cast<WPARAM>(std::llround(wParam)), static_cast<LPARAM>(std::llround(lParam))) != FALSE;
}
double SendMessageW(double hwnd, double message, double wParam, double lParam)
{
	return static_cast<double>(::SendMessageW(hwndOf(hwnd), static_cast<UINT>(message), static_cast<WPARAM>(std::llround(wParam)), static_cast<LPARAM>(std::llround(lParam))));
}
double GetKeyState(double virtualKey) { return ::GetKeyState(static_cast<int>(virtualKey)); }
double GetAsyncKeyState(double virtualKey) { return ::GetAsyncKeyState(static_cast<int>(virtualKey)); }
double GetDoubleClickTime() { return ::GetDoubleClickTime(); }
double GetSysColor(double index) { return ::GetSysColor(static_cast<int>(index)); }
double GetWindowThreadProcessId(double hwnd)
{
	DWORD process = 0;
	::GetWindowThreadProcessId(hwndOf(hwnd), &process);
	return process;
}
bool SetClipboardText(std::string text)
{
	if (!::OpenClipboard(gea::win32::mainWindow())) return false;
	::EmptyClipboard();
	const std::wstring value = wide(text);
	HGLOBAL memory = ::GlobalAlloc(GMEM_MOVEABLE, (value.size() + 1) * sizeof(wchar_t));
	bool ok = false;
	if (memory) {
		void *destination = ::GlobalLock(memory);
		if (destination) {
			std::memcpy(destination, value.c_str(), (value.size() + 1) * sizeof(wchar_t));
			::GlobalUnlock(memory);
			ok = ::SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
		}
		if (!ok) ::GlobalFree(memory);
	}
	::CloseClipboard();
	return ok;
}
std::string GetClipboardText()
{
	if (!::OpenClipboard(gea::win32::mainWindow())) return std::string();
	std::string result;
	if (HANDLE data = ::GetClipboardData(CF_UNICODETEXT)) {
		if (const wchar_t *text = static_cast<const wchar_t *>(::GlobalLock(data))) {
			result = narrow(text);
			::GlobalUnlock(data);
		}
	}
	::CloseClipboard();
	return result;
}

}  // namespace gea::windows::User32

// --- Kernel32 ------------------------------------------------------------------------------

namespace gea::windows::Kernel32 {

double GetTickCount64() { return static_cast<double>(::GetTickCount64()); }
void Sleep(double milliseconds) { ::Sleep(static_cast<DWORD>(std::max(0.0, milliseconds))); }
std::string GetComputerNameW()
{
	wchar_t name[MAX_COMPUTERNAME_LENGTH + 1] = {0};
	DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
	if (!::GetComputerNameW(name, &size)) return std::string();
	return narrow(name);
}
std::string GetUserNameW()
{
	return readEnvironment(L"USERNAME");
}
std::string GetEnvironmentVariableW(std::string name) { return readEnvironment(wide(name).c_str()); }
bool SetEnvironmentVariableW(std::string name, std::string value)
{
	return ::SetEnvironmentVariableW(wide(name).c_str(), value.empty() ? nullptr : wide(value).c_str()) != FALSE;
}
double GetCurrentProcessId() { return ::GetCurrentProcessId(); }
double GetCurrentThreadId() { return ::GetCurrentThreadId(); }
double GetLastError() { return ::GetLastError(); }
std::string GetSystemDirectoryW()
{
	wchar_t buffer[MAX_PATH] = {0};
	::GetSystemDirectoryW(buffer, MAX_PATH);
	return narrow(buffer);
}
std::string GetWindowsDirectoryW()
{
	wchar_t buffer[MAX_PATH] = {0};
	::GetWindowsDirectoryW(buffer, MAX_PATH);
	return narrow(buffer);
}
std::string GetTempPathW()
{
	wchar_t buffer[MAX_PATH + 1] = {0};
	::GetTempPathW(MAX_PATH + 1, buffer);
	return narrow(buffer);
}
std::string GetCommandLineW() { return narrow(::GetCommandLineW()); }
std::string GetModuleFileNameW()
{
	wchar_t buffer[MAX_PATH] = {0};
	::GetModuleFileNameW(nullptr, buffer, MAX_PATH);
	return narrow(buffer);
}
void OutputDebugStringW(std::string text) { ::OutputDebugStringW(wide(text).c_str()); }
double QueryPerformanceCounter()
{
	LARGE_INTEGER value{};
	::QueryPerformanceCounter(&value);
	return static_cast<double>(value.QuadPart);
}
double QueryPerformanceFrequency()
{
	LARGE_INTEGER value{};
	::QueryPerformanceFrequency(&value);
	return static_cast<double>(value.QuadPart);
}
double GetLogicalProcessorCount()
{
	SYSTEM_INFO info{};
	::GetSystemInfo(&info);
	return info.dwNumberOfProcessors;
}
double GetPhysicalMemoryBytes()
{
	MEMORYSTATUSEX status{};
	status.dwLength = sizeof(status);
	return ::GlobalMemoryStatusEx(&status) ? static_cast<double>(status.ullTotalPhys) : 0;
}
double GetAvailableMemoryBytes()
{
	MEMORYSTATUSEX status{};
	status.dwLength = sizeof(status);
	return ::GlobalMemoryStatusEx(&status) ? static_cast<double>(status.ullAvailPhys) : 0;
}
std::string GetOsVersionString()
{
	// RtlGetVersion reports the real version regardless of the manifest.
	typedef LONG(WINAPI * RtlGetVersionFn)(PRTL_OSVERSIONINFOW);
	RTL_OSVERSIONINFOW info{};
	info.dwOSVersionInfoSize = sizeof(info);
	if (HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll")) {
		if (auto getVersion = reinterpret_cast<RtlGetVersionFn>(::GetProcAddress(ntdll, "RtlGetVersion"))) getVersion(&info);
	}
	return std::to_string(info.dwMajorVersion) + "." + std::to_string(info.dwMinorVersion) + "." + std::to_string(info.dwBuildNumber);
}
bool FileExistsW(std::string path)
{
	const DWORD attributes = ::GetFileAttributesW(wide(path).c_str());
	return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}
std::string ReadTextFileW(std::string path)
{
	HANDLE file = ::CreateFileW(wide(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	if (file == INVALID_HANDLE_VALUE) return std::string();
	LARGE_INTEGER size{};
	::GetFileSizeEx(file, &size);
	std::string contents(static_cast<size_t>(std::max<LONGLONG>(0, size.QuadPart)), '\0');
	DWORD read = 0;
	if (!contents.empty()) ::ReadFile(file, contents.data(), static_cast<DWORD>(contents.size()), &read, nullptr);
	::CloseHandle(file);
	contents.resize(read);
	return contents;
}
bool WriteTextFileW(std::string path, std::string contents)
{
	HANDLE file = ::CreateFileW(wide(path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) return false;
	DWORD written = 0;
	const bool ok = contents.empty() || (::WriteFile(file, contents.data(), static_cast<DWORD>(contents.size()), &written, nullptr) && written == contents.size());
	::CloseHandle(file);
	return ok;
}
bool CreateDirectoryW(std::string path) { return ::CreateDirectoryW(wide(path).c_str(), nullptr) != FALSE || ::GetLastError() == ERROR_ALREADY_EXISTS; }
bool DeleteFileW(std::string path) { return ::DeleteFileW(wide(path).c_str()) != FALSE; }

}  // namespace gea::windows::Kernel32

// --- Shell32 ---------------------------------------------------------------------------------

namespace gea::windows::Shell32 {

double ShellExecuteW(std::string operation, std::string file, std::string parameters, std::string directory, double showCommand)
{
	const std::wstring op = wide(operation);
	const std::wstring params = wide(parameters);
	const std::wstring dir = wide(directory);
	HINSTANCE result = ::ShellExecuteW(gea::win32::mainWindow(), op.empty() ? nullptr : op.c_str(), wide(file).c_str(), params.empty() ? nullptr : params.c_str(),
	                                   dir.empty() ? nullptr : dir.c_str(), static_cast<int>(showCommand));
	return static_cast<double>(reinterpret_cast<std::uintptr_t>(result));
}
bool OpenUrl(std::string url)
{
	return reinterpret_cast<std::uintptr_t>(::ShellExecuteW(nullptr, L"open", wide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}
std::string SHGetKnownFolderPath(std::string folder)
{
	const KNOWNFOLDERID *id = &FOLDERID_Documents;
	if (folder == "Desktop") id = &FOLDERID_Desktop;
	else if (folder == "Downloads") id = &FOLDERID_Downloads;
	else if (folder == "Pictures") id = &FOLDERID_Pictures;
	else if (folder == "Music") id = &FOLDERID_Music;
	else if (folder == "Videos") id = &FOLDERID_Videos;
	else if (folder == "LocalAppData") id = &FOLDERID_LocalAppData;
	else if (folder == "RoamingAppData") id = &FOLDERID_RoamingAppData;
	else if (folder == "Profile") id = &FOLDERID_Profile;
	else if (folder == "ProgramFiles") id = &FOLDERID_ProgramFiles;
	PWSTR path = nullptr;
	std::string result;
	if (SUCCEEDED(::SHGetKnownFolderPath(*id, 0, nullptr, &path)) && path) {
		result = narrow(path);
		::CoTaskMemFree(path);
	}
	return result;
}
bool RevealInExplorer(std::string path)
{
	const std::wstring target = wide(path);
	PIDLIST_ABSOLUTE item = ::ILCreateFromPathW(target.c_str());
	if (!item) return false;
	const bool ok = SUCCEEDED(::SHOpenFolderAndSelectItems(item, 0, nullptr, 0));
	::ILFree(item);
	return ok;
}
bool ShowNotification(std::string title, std::string text)
{
	NOTIFYICONDATAW data{};
	data.cbSize = sizeof(data);
	data.hWnd = gea::win32::mainWindow();
	data.uID = 1;
	data.uFlags = NIF_INFO | NIF_ICON | NIF_TIP;
	data.dwInfoFlags = NIIF_INFO;
	data.hIcon = ::LoadIconW(::GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
	if (!data.hIcon) data.hIcon = ::LoadIconW(nullptr, IDI_APPLICATION);
	wcsncpy_s(data.szInfoTitle, wide(title).c_str(), _TRUNCATE);
	wcsncpy_s(data.szInfo, wide(text).c_str(), _TRUNCATE);
	wcsncpy_s(data.szTip, wide(title).c_str(), _TRUNCATE);
	::Shell_NotifyIconW(NIM_ADD, &data);
	const bool ok = ::Shell_NotifyIconW(NIM_MODIFY, &data) != FALSE;
	return ok;
}

}  // namespace gea::windows::Shell32

// --- Dwmapi ---------------------------------------------------------------------------------

namespace gea::windows::Dwmapi {

double DwmSetWindowAttribute(double hwnd, double attribute, double value)
{
	// Colour attributes take a COLORREF, booleans a BOOL, enumerations an int:
	// all are 32-bit, so one DWORD payload serves every attribute.
	DWORD payload = static_cast<DWORD>(std::llround(value));
	return static_cast<double>(::DwmSetWindowAttribute(hwndOf(hwnd), static_cast<DWORD>(attribute), &payload, sizeof(payload)));
}
double DwmGetColorizationColor()
{
	DWORD color = 0;
	BOOL opaque = FALSE;
	::DwmGetColorizationColor(&color, &opaque);
	return static_cast<double>(color);
}
bool DwmIsCompositionEnabled()
{
	BOOL enabled = FALSE;
	::DwmIsCompositionEnabled(&enabled);
	return enabled != FALSE;
}
double DwmFlush() { return static_cast<double>(::DwmFlush()); }

}  // namespace gea::windows::Dwmapi
