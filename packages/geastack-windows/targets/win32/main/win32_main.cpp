// SPDX-License-Identifier: Apache-2.0
// Entry point, main window and frame loop of the Win32 target.
//
// Each frame: Application::frame(now) lets the app update state, the CSS
// animation clock advances, the mounted root is sized to the content area
// (the target convention that makes the viewport track the window), the flex
// pass runs (Tree::computeLayout) and the renderer syncs the widget tree.
// Apps whose root is a <glass-split> go through the split shell instead;
// Windows-native apps drive their own control tree through the layout hook.

#include "win32_main.h"

#include <timeapi.h>

#include "app.h"
#include "css/declarative.h"
#include "css/engine.h"
#include "display.h"
#include "host/storage.h"
#include "platform/file_cache.h"
#include "services/storage_service.h"
#include "ui/document.h"
#include "ui/node.h"
#include "ui/node_model.h"
#include "ui/style.h"
#include "ui/tree_internal.h"
#include "win32_font_registry.h"
#include "win32_native_shell.h"
#include "win32_press_bridge.h"
#include "win32_renderer.h"

#include <dwmapi.h>
#include <gdiplus.h>
#include <shellscalingapi.h>
#include <windowsx.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shcore.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ole32.lib")

extern "C" int gea_embedded_now_ms(void);

namespace gea::win32 {
void installWifiDriver();
void installAppLauncherPlatform(const char *currentAppId);
std::string &storageAppId();
// Optional per-app storage bridge for the compiler runtime's localStorage
// table (compiled into the generated unit when present; weak so the smoke
// build links without it).
}
extern "C" void gea_win32_runtime_storage_load(void) __attribute__((weak));
extern "C" void gea_win32_runtime_storage_flush(void) __attribute__((weak));

#ifndef GEA_WINDOWS_APP_ID
#define GEA_WINDOWS_APP_ID "app"
#endif
#ifndef GEA_WINDOWS_APP_NAME
#define GEA_WINDOWS_APP_NAME "gea"
#endif

namespace {

using gea::win32::Color;

const wchar_t *const kMainClass = L"GeaMainWindow";

struct WindowConfig {
	std::wstring title = L"" GEA_WINDOWS_APP_NAME;
	int width = 410;
	int height = 502;
	int minWidth = 200;
	int minHeight = 200;
	std::string appearance;   // "dark" | "light" | ""
	std::string backdrop;     // "mica" | "acrylic" | "tabbed" | ""
	bool titleBarTransparent = false;
	Color background;
};

HWND g_window = nullptr;
gea::win32::Widget *g_contentHost = nullptr;
std::unique_ptr<gea::win32::GlassSplitShell> g_shell;
HWND g_nativeRoot = nullptr;
gea::win32::Toolbar *g_nativeToolbar = nullptr;
std::function<void(const RECT &)> g_nativeLayout;
double g_scale = 1.0;
bool g_frameRequested = false;
bool g_running = true;
WindowConfig g_config;
int g_frameIntervalUs = 16667;
bool g_booted = false;

// --- tiny JSON reader for window.json ------------------------------------------

struct JsonValue {
	enum Kind { Null, Bool, Number, String, Object } kind = Null;
	bool boolean = false;
	double number = 0;
	std::string text;
	std::map<std::string, JsonValue> object;
};

struct JsonReader {
	const std::string &source;
	size_t position = 0;
	explicit JsonReader(const std::string &text) : source(text) {}
	void skipWhitespace()
	{
		while (position < source.size() && std::isspace(static_cast<unsigned char>(source[position]))) position++;
	}
	bool consume(char expected)
	{
		skipWhitespace();
		if (position < source.size() && source[position] == expected) {
			position++;
			return true;
		}
		return false;
	}
	std::string readString()
	{
		std::string out;
		if (!consume('"')) return out;
		while (position < source.size() && source[position] != '"') {
			char ch = source[position++];
			if (ch == '\\' && position < source.size()) {
				const char escaped = source[position++];
				switch (escaped) {
				case 'n': out.push_back('\n'); break;
				case 't': out.push_back('\t'); break;
				case 'u':
					if (position + 4 <= source.size()) {
						const unsigned code = static_cast<unsigned>(std::stoul(source.substr(position, 4), nullptr, 16));
						position += 4;
						if (code < 0x80) out.push_back(static_cast<char>(code));
						else if (code < 0x800) {
							out.push_back(static_cast<char>(0xC0 | (code >> 6)));
							out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
						} else {
							out.push_back(static_cast<char>(0xE0 | (code >> 12)));
							out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
							out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
						}
					}
					break;
				default: out.push_back(escaped);
				}
			} else {
				out.push_back(ch);
			}
		}
		position++;
		return out;
	}
	JsonValue readValue()
	{
		JsonValue value;
		skipWhitespace();
		if (position >= source.size()) return value;
		const char ch = source[position];
		if (ch == '{') {
			position++;
			value.kind = JsonValue::Object;
			for (;;) {
				skipWhitespace();
				if (consume('}')) break;
				const std::string key = readString();
				consume(':');
				value.object[key] = readValue();
				skipWhitespace();
				if (!consume(',')) {
					consume('}');
					break;
				}
			}
			return value;
		}
		if (ch == '[') {
			// Arrays are not part of the window schema; skip them.
			int depth = 0;
			do {
				if (source[position] == '[') depth++;
				if (source[position] == ']') depth--;
				position++;
			} while (depth > 0 && position < source.size());
			return value;
		}
		if (ch == '"') {
			value.kind = JsonValue::String;
			value.text = readString();
			return value;
		}
		if (source.compare(position, 4, "true") == 0) {
			value.kind = JsonValue::Bool;
			value.boolean = true;
			position += 4;
			return value;
		}
		if (source.compare(position, 5, "false") == 0) {
			value.kind = JsonValue::Bool;
			position += 5;
			return value;
		}
		if (source.compare(position, 4, "null") == 0) {
			position += 4;
			return value;
		}
		char *end = nullptr;
		value.number = std::strtod(source.c_str() + position, &end);
		value.kind = JsonValue::Number;
		position = static_cast<size_t>(end - source.c_str());
		return value;
	}
};

Color parseHexColor(const std::string &text)
{
	std::string hex = text;
	if (!hex.empty() && hex[0] == '#') hex.erase(0, 1);
	if (hex.size() == 3) hex = {hex[0], hex[0], hex[1], hex[1], hex[2], hex[2]};
	if (hex.size() != 6 && hex.size() != 8) return Color{};
	const unsigned long value = std::strtoul(hex.c_str(), nullptr, 16);
	if (hex.size() == 8) return Color::rgb((value >> 24) & 0xFF, (value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF);
	return Color::rgb((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF);
}

std::wstring executableDirectory()
{
	wchar_t module[MAX_PATH]{};
	GetModuleFileNameW(nullptr, module, MAX_PATH);
	std::wstring path = module;
	const size_t slash = path.find_last_of(L"\\/");
	return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

std::wstring resourcesDirectory()
{
	return executableDirectory() + L"\\Resources";
}

bool readTextFile(const std::wstring &path, std::string &out)
{
	HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	if (file == INVALID_HANDLE_VALUE) return false;
	LARGE_INTEGER size{};
	GetFileSizeEx(file, &size);
	out.resize(static_cast<size_t>(size.QuadPart));
	DWORD read = 0;
	const bool ok = size.QuadPart == 0 || (ReadFile(file, out.data(), static_cast<DWORD>(out.size()), &read, nullptr) && read == out.size());
	CloseHandle(file);
	return ok;
}

bool systemPrefersDark()
{
	DWORD value = 1;
	DWORD size = sizeof(value);
	if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", RRF_RT_REG_DWORD,
	                 nullptr, &value, &size) == ERROR_SUCCESS) {
		return value == 0;
	}
	return false;
}

void loadWindowConfig()
{
	// A per-app window.json (from the app's windows.json, or its macos.json
	// when it has no Windows-specific one) configures size and appearance.
	std::string text;
	if (!readTextFile(resourcesDirectory() + L"\\window.json", text)) return;
	JsonReader reader(text);
	JsonValue root = reader.readValue();
	const JsonValue *window = &root;
	auto it = root.object.find("window");
	if (it != root.object.end() && it->second.kind == JsonValue::Object) window = &it->second;
	auto number = [&](const char *key, int fallback) {
		auto found = window->object.find(key);
		return found != window->object.end() && found->second.kind == JsonValue::Number ? static_cast<int>(found->second.number) : fallback;
	};
	auto string = [&](const char *key) {
		auto found = window->object.find(key);
		return found != window->object.end() && found->second.kind == JsonValue::String ? found->second.text : std::string();
	};
	auto boolean = [&](const char *key, bool fallback) {
		auto found = window->object.find(key);
		return found != window->object.end() && found->second.kind == JsonValue::Bool ? found->second.boolean : fallback;
	};
	g_config.width = number("width", g_config.width);
	g_config.height = number("height", g_config.height);
	g_config.minWidth = number("minWidth", g_config.minWidth);
	g_config.minHeight = number("minHeight", g_config.minHeight);
	const std::string title = string("title");
	if (!title.empty()) g_config.title = gea::win32::toWide(title);
	g_config.appearance = string("appearance");
	g_config.backdrop = string("backdrop");
	g_config.titleBarTransparent = boolean("titleBarTransparent", false);
	const std::string background = string("backgroundColor");
	if (!background.empty()) g_config.background = parseHexColor(background);
}

// --- dark mode -----------------------------------------------------------------

using SetPreferredAppModeFn = int(WINAPI *)(int);

void applyAppearance(bool dark)
{
	gea::win32::setDarkModeEnabled(dark);
	// Dark scrollbars/menus for the common controls (uxtheme ordinal 135,
	// AllowDark = 1, Default = 0); ignored where the OS lacks it.
	if (HMODULE uxtheme = LoadLibraryW(L"uxtheme.dll")) {
		if (auto setPreferredAppMode = reinterpret_cast<SetPreferredAppModeFn>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(135)))) {
			setPreferredAppMode(dark ? 1 : 0);
		}
	}
	if (g_window) {
		BOOL immersive = dark ? TRUE : FALSE;
		DwmSetWindowAttribute(g_window, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &immersive, sizeof(immersive));
	}
}

void applyWindowChrome()
{
	if (!g_window) return;
	const Color background = gea::win32::windowBackground();
	if (g_config.titleBarTransparent) {
		// Windows 11: colour the caption like the content so the title bar
		// reads as part of the window, the way a transparent AppKit title bar does.
		COLORREF caption = RGB(background.r, background.g, background.b);
		DwmSetWindowAttribute(g_window, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof(caption));
		COLORREF text = gea::win32::darkModeEnabled() ? RGB(235, 235, 240) : RGB(30, 30, 30);
		DwmSetWindowAttribute(g_window, 36 /* DWMWA_TEXT_COLOR */, &text, sizeof(text));
	}
	int backdrop = 0;
	if (g_config.backdrop == "mica") backdrop = 2;
	else if (g_config.backdrop == "acrylic") backdrop = 3;
	else if (g_config.backdrop == "tabbed") backdrop = 4;
	if (backdrop) DwmSetWindowAttribute(g_window, 38 /* DWMWA_SYSTEMBACKDROP_TYPE */, &backdrop, sizeof(backdrop));
}

// --- frame pacing ----------------------------------------------------------------

void updateFrameInterval()
{
	HMONITOR monitor = MonitorFromWindow(g_window, MONITOR_DEFAULTTONEAREST);
	MONITORINFOEXW info{};
	info.cbSize = sizeof(info);
	int hz = 60;
	if (monitor && GetMonitorInfoW(monitor, &info)) {
		DEVMODEW mode{};
		mode.dmSize = sizeof(mode);
		if (EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1) hz = static_cast<int>(mode.dmDisplayFrequency);
	}
	// GEA_WINDOWS_REFRESH_HZ overrides the monitor's rate (a remote-desktop
	// display reports 32 Hz, which is then the frame ceiling by design).
	if (const char *forced = std::getenv("GEA_WINDOWS_REFRESH_HZ")) {
		const int value = std::atoi(forced);
		if (value > 0) hz = value;
	}
	g_frameIntervalUs = std::max(1000, 1000000 / std::clamp(hz, 24, 240));
}

void updateScale()
{
	const UINT dpi = g_window ? GetDpiForWindow(g_window) : 96;
	g_scale = dpi > 0 ? dpi / 96.0 : 1.0;
	gea::win32::Renderer::instance().setScale(g_scale);
}

RECT clientRect()
{
	RECT rect{};
	if (g_window) GetClientRect(g_window, &rect);
	return rect;
}

// --- diagnostics ------------------------------------------------------------------

void logTree(const char *prefix)
{
	using gea::embedded::ui::NodeType;
	auto &tree = gea::embedded::ui::Tree::instance();
	std::fprintf(stderr, "%s mountedRoot=%d nodeCount=%d\n", prefix, tree.mountedRoot(), tree.nodeCount());
	for (int i = 0; i < tree.nodeCount(); i++) {
		const auto &node = tree.node(i);
		const char *tag = tree.tagName(i);
		std::fprintf(stderr, "%s   node[%d] type=%d tag=%s frame=(%d,%d,%dx%d) bg=%d/%04x/%u z=%d radius=%d opacity=%u display=%d%s%s\n", prefix, i,
		             static_cast<int>(node.type), tag ? tag : "", node.layout.x, node.layout.y, node.layout.width, node.layout.height,
		             static_cast<int>(node.style.has_bg), static_cast<unsigned>(node.style.bg_color), static_cast<unsigned>(node.style.bg_alpha),
		             static_cast<int>(node.style.z_index), static_cast<int>(node.style.border_radius[0]), static_cast<unsigned>(node.style.opacity),
		             static_cast<int>(node.style.display), node.type == NodeType::Text ? " text=" : "", node.type == NodeType::Text ? node.text.c_str() : "");
	}
}

bool saveWindowScreenshot(const std::wstring &path)
{
	RECT rect = clientRect();
	const int width = rect.right - rect.left;
	const int height = rect.bottom - rect.top;
	if (width <= 0 || height <= 0) return false;
	HDC screen = GetDC(g_window);
	HDC memory = CreateCompatibleDC(screen);
	HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
	HGDIOBJ previous = SelectObject(memory, bitmap);
	// Copy what is on screen: PrintWindow (even with PW_RENDERFULLCONTENT)
	// hands back the class background for child windows that moved or were
	// painted this frame, so a capture of an animating scene came out as grey
	// boxes. The screen copy is what the user sees; PrintWindow is the
	// fallback for a hidden or minimized window.
	const bool onScreen = IsWindowVisible(g_window) && !IsIconic(g_window);
	if (!onScreen || !BitBlt(memory, 0, 0, width, height, screen, 0, 0, SRCCOPY)) PrintWindow(g_window, memory, PW_CLIENTONLY | 0x00000002);
	SelectObject(memory, previous);
	DeleteDC(memory);
	ReleaseDC(g_window, screen);
	CLSID png{};
	UINT count = 0;
	UINT size = 0;
	Gdiplus::GetImageEncodersSize(&count, &size);
	std::vector<unsigned char> buffer(size);
	auto *codecs = reinterpret_cast<Gdiplus::ImageCodecInfo *>(buffer.data());
	Gdiplus::GetImageEncoders(count, size, codecs);
	bool found = false;
	for (UINT i = 0; i < count; i++) {
		if (wcscmp(codecs[i].MimeType, L"image/png") == 0) {
			png = codecs[i].Clsid;
			found = true;
			break;
		}
	}
	bool ok = false;
	if (found) {
		Gdiplus::Bitmap image(bitmap, nullptr);
		ok = image.Save(path.c_str(), &png, nullptr) == Gdiplus::Ok;
	}
	DeleteObject(bitmap);
	return ok;
}

// --- the frame ---------------------------------------------------------------------

RECT computeContentArea()
{
	RECT area = clientRect();
	if (g_nativeToolbar) area.top += static_cast<int>(std::lround(g_nativeToolbar->layoutHeight() * g_scale));
	return area;
}

void layoutNativeChrome()
{
	if (g_nativeToolbar) {
		RECT strip = clientRect();
		strip.bottom = strip.top + static_cast<int>(std::lround(g_nativeToolbar->layoutHeight() * g_scale));
		g_nativeToolbar->layout(strip, g_scale);
	}
	if (g_nativeRoot) {
		const RECT area = computeContentArea();
		SetWindowPos(g_nativeRoot, nullptr, area.left, area.top, area.right - area.left, area.bottom - area.top, SWP_NOZORDER | SWP_NOACTIVATE);
	}
}

void tick()
{
	static const bool tickDebug = std::getenv("GEA_WINDOWS_TICK_DEBUG") != nullptr;
	if (tickDebug) {
		static int count = 0;
		count++;
		if (count <= 10 || count % 60 == 0) std::fprintf(stderr, "[gea-windows] tick %d now=%d\n", count, gea_embedded_now_ms());
	}
	gea::framework::app::Application::frame(gea_embedded_now_ms());
	auto &tree = gea::embedded::ui::Tree::instance();
	const int root = tree.mountedRoot();

	// The CSS animation clock: scan once per mounted app, then tick.
	{
		static bool scanned = false;
		if (!scanned) {
			scanned = true;
			gea::css::DeclarativeAnimations::scanAndStart(gea_embedded_now_ms());
			gea::embedded::ui::StyleSheet::instance().startCssAnimations(gea_embedded_now_ms());
		}
	}
	gea::css::AnimationEngine::instance().tick(static_cast<std::uint32_t>(gea_embedded_now_ms()));

	static int frameCount = 0;
	++frameCount;

	if (const char *inputEnv = std::getenv("GEA_WINDOWS_SYNTH_INPUT")) {
		if (frameCount == 4) {
			const int node = gea_win32_fire_input_for_node(-1, inputEnv);
			std::fprintf(stderr, "[gea-synth-input] node=%d text=%s\n", node, inputEnv);
		}
	}
	if (const char *clickEnv = std::getenv("GEA_WINDOWS_SYNTH_CLICK")) {
		if (frameCount == 3) gea_win32_fire_press_for_node(std::atoi(clickEnv));
	}

	if (g_nativeRoot || g_nativeLayout) {
		layoutNativeChrome();
		if (g_nativeLayout) g_nativeLayout(computeContentArea());
	} else if (root >= 0) {
		if (g_shell) {
			g_shell->layoutAndSync(clientRect(), g_scale);
		} else {
			const RECT area = computeContentArea();
			if (g_contentHost) gea::win32::setWidgetFrame(g_contentHost, area.left, area.top, area.right - area.left, area.bottom - area.top);
			const int w = static_cast<int>(std::lround((area.right - area.left) / g_scale));
			const int h = static_cast<int>(std::lround((area.bottom - area.top) / g_scale));
			if (w > 0 && h > 0) {
				gea::embedded::ui::NodeHandle(root).style().width(w);
				gea::embedded::ui::NodeHandle(root).style().height(h);
				tree.computeLayout(root, w, h);
				gea::win32::Renderer::instance().sync(g_contentHost ? g_contentHost->hwnd : g_window, root);
			}
		}
	}

	// Persist localStorage writes made this frame (both views of it).
	gea::host::Storage.flushPending();
	if (gea_win32_runtime_storage_flush) gea_win32_runtime_storage_flush();

	if (std::getenv("GEA_WINDOWS_LAYOUT_DUMP") && (frameCount <= 3 || frameCount % 120 == 0)) logTree("[gea-layout]");
	if (const char *shot = std::getenv("GEA_WINDOWS_SCREENSHOT")) {
		const int after = std::getenv("GEA_WINDOWS_SCREENSHOT_FRAME") ? std::atoi(std::getenv("GEA_WINDOWS_SCREENSHOT_FRAME")) : 12;
		if (frameCount == after) {
			// Let the compositor present everything queued before capturing.
			DwmFlush();
			UpdateWindow(g_window);
			const bool ok = saveWindowScreenshot(gea::win32::toWide(shot));
			std::fprintf(stderr, "[gea-screenshot] %s -> %s\n", shot, ok ? "written" : "FAILED");
			if (!std::getenv("GEA_WINDOWS_SCREENSHOT_STAY")) {
				g_running = false;
				PostQuitMessage(0);
			}
		}
	}
	if (std::getenv("GEA_WINDOWS_VERIFY_ONCE") && root >= 0) {
		static bool verified = false;
		if (!verified && frameCount >= 2) {
			verified = true;
			using gea::embedded::ui::NodeType;
			logTree("[gea-verify]");
			int buttonNode = -1;
			for (int i = 0; i < tree.nodeCount(); i++) {
				if (tree.node(i).type == NodeType::Button) buttonNode = i;
			}
			if (buttonNode >= 0) {
				std::fprintf(stderr, "[gea-verify] firing press for buttonNode=%d twice\n", buttonNode);
				gea_win32_fire_press_for_node(buttonNode);
				gea_win32_fire_press_for_node(buttonNode);
				for (int i = 0; i < tree.nodeCount(); i++) {
					const auto &node = tree.node(i);
					if (node.type == NodeType::Text && !node.text.empty()) std::fprintf(stderr, "[gea-verify]   text node[%d] = \"%s\"\n", i, node.text.c_str());
				}
			}
			g_running = false;
			PostQuitMessage(0);
		}
	}
	if (const char *exitAfter = std::getenv("GEA_WINDOWS_EXIT_AFTER_MS")) {
		if (gea_embedded_now_ms() >= std::atoi(exitAfter)) {
			g_running = false;
			PostQuitMessage(0);
		}
	}
}

// --- boot ------------------------------------------------------------------------

void boot()
{
	if (g_booted) return;
	g_booted = true;
	gea::win32::installWifiDriver();
	gea::win32::registerFontDirectory(resourcesDirectory() + L"\\Fonts");
	gea::win32::installAppLauncherPlatform(GEA_WINDOWS_APP_ID);
	gea::win32::storageAppId() = GEA_WINDOWS_APP_ID;
	// Windows storage needs no mount operation.
	gea::platform::storage::setMountProvider([]() -> bool { return true; });
	gea::framework::services::StorageService::init();
	// Restore persisted localStorage BEFORE Application::init: a store's
	// init() reads it during mount.
	gea::host::Storage.load();
	if (gea_win32_runtime_storage_load) gea_win32_runtime_storage_load();

	const RECT area = computeContentArea();
	const int w = std::max(1, static_cast<int>(std::lround((area.right - area.left) / g_scale)));
	const int h = std::max(1, static_cast<int>(std::lround((area.bottom - area.top) / g_scale)));
	gea::framework::app::Application::init(w, h);

	// A <glass-split> root swaps the flat content host for the split shell.
	if (!g_nativeRoot && !g_nativeLayout) {
		const int mountedRoot = gea::embedded::ui::Tree::instance().mountedRoot();
		if (mountedRoot >= 0) {
			g_shell = gea::win32::GlassSplitShell::create(mountedRoot, g_window);
			if (g_shell && g_contentHost) ShowWindow(g_contentHost->hwnd, SW_HIDE);
		}
	}
	tick();
}

LRESULT CALLBACK mainWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message) {
	case WM_ERASEBKGND: {
		RECT rect{};
		GetClientRect(hwnd, &rect);
		gea::win32::fillRect(reinterpret_cast<HDC>(wParam), rect, gea::win32::windowBackground());
		return 1;
	}
	case WM_PAINT: {
		PAINTSTRUCT paint{};
		HDC hdc = BeginPaint(hwnd, &paint);
		gea::win32::fillRect(hdc, paint.rcPaint, gea::win32::windowBackground());
		EndPaint(hwnd, &paint);
		return 0;
	}
	case WM_SIZE:
		if (g_booted && wParam != SIZE_MINIMIZED) {
			layoutNativeChrome();
			tick();
		}
		return 0;
	case WM_GETMINMAXINFO: {
		auto *info = reinterpret_cast<MINMAXINFO *>(lParam);
		RECT frame{0, 0, static_cast<int>(std::lround(g_config.minWidth * g_scale)), static_cast<int>(std::lround(g_config.minHeight * g_scale))};
		AdjustWindowRectExForDpi(&frame, static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE)), FALSE, 0, GetDpiForWindow(hwnd));
		info->ptMinTrackSize.x = frame.right - frame.left;
		info->ptMinTrackSize.y = frame.bottom - frame.top;
		return 0;
	}
	case WM_DPICHANGED: {
		updateScale();
		gea::win32::resetFontCaches();
		const RECT *suggested = reinterpret_cast<RECT *>(lParam);
		SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top,
		             SWP_NOZORDER | SWP_NOACTIVATE);
		updateFrameInterval();
		if (g_booted) tick();
		return 0;
	}
	case WM_DISPLAYCHANGE:
	case WM_MOVE:
		if (g_window) updateFrameInterval();
		break;
	case WM_SETTINGCHANGE:
		if (lParam && wcscmp(reinterpret_cast<const wchar_t *>(lParam), L"ImmersiveColorSet") == 0 && g_config.appearance.empty()) {
			applyAppearance(systemPrefersDark());
		}
		break;
	case WM_CLOSE:
		DestroyWindow(hwnd);
		return 0;
	case WM_DESTROY:
		g_running = false;
		PostQuitMessage(0);
		return 0;
	default:
		break;
	}
	return DefWindowProcW(hwnd, message, wParam, lParam);
}

}  // namespace

namespace gea::win32 {

HWND mainWindow() { return g_window; }
double windowScale() { return g_scale; }

void resizeMainWindowClient(int width, int height)
{
	if (!g_window) return;
	RECT frame{0, 0, static_cast<int>(std::lround(width * g_scale)), static_cast<int>(std::lround(height * g_scale))};
	AdjustWindowRectExForDpi(&frame, static_cast<DWORD>(GetWindowLongPtrW(g_window, GWL_STYLE)), FALSE, 0, GetDpiForWindow(g_window));
	SetWindowPos(g_window, nullptr, 0, 0, frame.right - frame.left, frame.bottom - frame.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void requestFrame() { g_frameRequested = true; }

void quitApplication()
{
	g_running = false;
	PostQuitMessage(0);
}

void setMainWindowTitle(const std::wstring &title)
{
	if (g_window) SetWindowTextW(g_window, title.c_str());
}

void setMainWindowAppearance(const std::string &appearance)
{
	g_config.appearance = appearance == "system" ? "" : appearance;
	const bool dark = appearance == "dark" || (appearance != "light" && systemPrefersDark());
	applyAppearance(dark);
	applyWindowChrome();
	if (g_window) RedrawWindow(g_window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);
}

void setMainWindowBackground(Color color)
{
	setWindowBackground(color);
	if (g_contentHost) {
		WidgetStyle style = g_contentHost->style;
		style.background = color;
		applyWidgetStyle(g_contentHost, style);
	}
	applyWindowChrome();
	if (g_window) RedrawWindow(g_window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);
}

RECT contentArea() { return computeContentArea(); }

void installNativeRootWindow(HWND root)
{
	if (!root) return;
	g_nativeRoot = root;
	if (g_contentHost) ShowWindow(g_contentHost->hwnd, SW_HIDE);
	SetParent(root, g_window);
	layoutNativeChrome();
	ShowWindow(root, SW_SHOWNA);
	requestFrame();
}

void installNativeToolbar(Toolbar *toolbar)
{
	g_nativeToolbar = toolbar;
	layoutNativeChrome();
	requestFrame();
}

void setNativeLayoutCallback(std::function<void(const RECT &)> callback)
{
	g_nativeLayout = std::move(callback);
}

}  // namespace gea::win32

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	// A console-attached parent (the build's --run, a terminal) sees stderr and
	// stdout. A stream the parent redirected to a file or pipe (`2> log.txt`)
	// already has a usable handle and is left alone; only a stream with no
	// handle at all (the usual case for a windows-subsystem process) is
	// pointed at the console.
	if (AttachConsole(ATTACH_PARENT_PROCESS)) {
		auto redirected = [](DWORD which) {
			HANDLE handle = GetStdHandle(which);
			return handle && handle != INVALID_HANDLE_VALUE && GetFileType(handle) != FILE_TYPE_UNKNOWN;
		};
		FILE *stream = nullptr;
		if (!redirected(STD_ERROR_HANDLE)) freopen_s(&stream, "CONOUT$", "w", stderr);
		if (!redirected(STD_OUTPUT_HANDLE)) freopen_s(&stream, "CONOUT$", "w", stdout);
	}
	gea::win32::initializeWidgets(instance);
	loadWindowConfig();

	const bool dark = g_config.appearance == "dark" || (g_config.appearance != "light" && systemPrefersDark());
	applyAppearance(dark);
	Color background = g_config.background.set() ? g_config.background : (dark ? Color::rgb(32, 32, 32) : Color::rgb(243, 243, 243));
	gea::win32::setWindowBackground(background);

	WNDCLASSEXW cls{};
	cls.cbSize = sizeof(cls);
	cls.style = CS_HREDRAW | CS_VREDRAW;
	cls.lpfnWndProc = mainWindowProc;
	cls.hInstance = instance;
	cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	cls.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
	cls.hIconSm = cls.hIcon;
	cls.lpszClassName = kMainClass;
	RegisterClassExW(&cls);

	int width = g_config.width;
	int height = g_config.height;
	if (const char *e = std::getenv("GEA_WINDOWS_WIN_W")) width = std::atoi(e);
	if (const char *e = std::getenv("GEA_WINDOWS_WIN_H")) height = std::atoi(e);
	const UINT dpi = GetDpiForSystem();
	g_scale = dpi / 96.0;
	RECT frame{0, 0, static_cast<int>(std::lround(width * g_scale)), static_cast<int>(std::lround(height * g_scale))};
	const DWORD style = WS_OVERLAPPEDWINDOW;
	AdjustWindowRectExForDpi(&frame, style, FALSE, 0, dpi);
	const int screenW = GetSystemMetrics(SM_CXSCREEN);
	const int screenH = GetSystemMetrics(SM_CYSCREEN);
	const int windowW = frame.right - frame.left;
	const int windowH = frame.bottom - frame.top;
	g_window = CreateWindowExW(0, kMainClass, g_config.title.c_str(), style, std::max(0, (screenW - windowW) / 2), std::max(0, (screenH - windowH) / 2),
	                           windowW, windowH, nullptr, nullptr, instance, nullptr);
	if (!g_window) return 1;
	updateScale();
	applyAppearance(dark);
	applyWindowChrome();
	updateFrameInterval();

	g_contentHost = gea::win32::createWidget(gea::win32::WidgetKind::View, g_window);
	{
		gea::win32::WidgetStyle hostStyle = g_contentHost->style;
		hostStyle.background = background;
		gea::win32::applyWidgetStyle(g_contentHost, hostStyle);
		const RECT area = clientRect();
		gea::win32::setWidgetFrame(g_contentHost, 0, 0, area.right, area.bottom);
	}

	ShowWindow(g_window, SW_SHOW);
	UpdateWindow(g_window);
	boot();

	// Frame pacing: run a frame every refresh interval, waiting for input in
	// between so an idle window costs nothing between ticks. The wait goes
	// through a high-resolution waitable timer: a plain millisecond timeout is
	// quantised to the 15.6 ms scheduler tick, which on its own turns a 60 Hz
	// loop into 32 frames a second. timeBeginPeriod covers systems without
	// the high-resolution timer.
	HANDLE frameTimer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
	if (!frameTimer) {
		frameTimer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
		timeBeginPeriod(1);
	}
	auto nextFrame = std::chrono::steady_clock::now();
	MSG message{};
	while (g_running) {
		while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
			if (message.message == WM_QUIT) {
				g_running = false;
				break;
			}
			gea::win32::routeMouseWheel(message);
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}
		if (!g_running) break;
		const auto now = std::chrono::steady_clock::now();
		if (now >= nextFrame || g_frameRequested) {
			g_frameRequested = false;
			tick();
			nextFrame = std::max(nextFrame + std::chrono::microseconds(g_frameIntervalUs), now - std::chrono::microseconds(g_frameIntervalUs));
			if (nextFrame < now) nextFrame = now + std::chrono::microseconds(g_frameIntervalUs);
			continue;
		}
		const auto waitUs = std::chrono::duration_cast<std::chrono::microseconds>(nextFrame - now).count();
		if (frameTimer) {
			LARGE_INTEGER due{};
			due.QuadPart = -static_cast<LONGLONG>(std::max<long long>(1, waitUs)) * 10;  // relative, 100 ns units
			SetWaitableTimer(frameTimer, &due, 0, nullptr, nullptr, FALSE);
			MsgWaitForMultipleObjectsEx(1, &frameTimer, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
		} else {
			MsgWaitForMultipleObjectsEx(0, nullptr, static_cast<DWORD>(std::max<long long>(1, (waitUs + 999) / 1000)), QS_ALLINPUT, MWMO_INPUTAVAILABLE);
		}
	}
	if (frameTimer) CloseHandle(frameTimer);
	g_shell.reset();
	gea::win32::Renderer::instance().teardown();
	CoUninitialize();
	return 0;
}
