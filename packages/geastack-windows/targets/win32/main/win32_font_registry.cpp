// SPDX-License-Identifier: Apache-2.0
// Fonts for the Win32 target: family registry (the framework's weak
// lookupFontFamily hooks), HFONT cache, private TTF registration, the runtime
// TTF byte registry the canvas rasterizer reads, and the host text-measurement
// hook the layout engine calls for every text node.

#include "win32_font_registry.h"

#include "graphics/font.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace gea::win32 {

namespace {

struct FamilyRegistry {
	std::mutex lock;
	std::vector<std::string> names;
	std::unordered_map<std::string, int> byName;
};

FamilyRegistry &familyRegistry()
{
	static FamilyRegistry registry;
	return registry;
}

std::string lowercased(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return value;
}

double g_scale = 1.0;

// Registered TTF bytes by lowercased family, kept alive for the runtime
// rasterizer (stbtt keeps pointers into them).
struct TtfRegistry {
	std::mutex lock;
	std::unordered_map<std::string, std::vector<std::uint8_t>> byFamily;
};

TtfRegistry &ttfRegistry()
{
	static TtfRegistry registry;
	return registry;
}

struct FontKey {
	std::wstring face;
	int height;
	int weight;
	bool operator<(const FontKey &other) const
	{
		if (height != other.height) return height < other.height;
		if (weight != other.weight) return weight < other.weight;
		return face < other.face;
	}
};

std::map<FontKey, HFONT> &fontCache()
{
	static std::map<FontKey, HFONT> cache;
	return cache;
}

HFONT createFont(const std::wstring &face, int heightPx, int weight)
{
	FontKey key{face, heightPx, weight};
	auto &cache = fontCache();
	auto it = cache.find(key);
	if (it != cache.end()) return it->second;
	LOGFONTW logical{};
	logical.lfHeight = -std::max(1, heightPx);
	logical.lfWeight = weight > 0 ? weight : FW_NORMAL;
	logical.lfCharSet = DEFAULT_CHARSET;
	logical.lfOutPrecision = OUT_TT_PRECIS;
	logical.lfQuality = CLEARTYPE_QUALITY;
	logical.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
	wcsncpy_s(logical.lfFaceName, face.c_str(), LF_FACESIZE - 1);
	HFONT font = CreateFontIndirectW(&logical);
	cache.emplace(key, font);
	return font;
}

int devicePixels(int layoutPx)
{
	return std::max(1, static_cast<int>(std::lround(layoutPx * g_scale)));
}

}  // namespace

// CSS family keywords and Apple's system-font spellings resolve to the
// Windows UI font; a family GDI does not have falls back through its own
// substitution, so an unknown name still renders.
std::wstring faceForFamily(const std::string &family)
{
	const std::string lower = lowercased(family);
	if (lower.empty() || lower == "-apple-system" || lower == "system-ui" || lower == "blinkmacsystemfont" || lower == "san francisco" ||
	    lower == "sf pro" || lower == "sf pro text" || lower == "sf pro display" || lower == "helvetica neue" || lower == "helvetica" ||
	    lower == "arial" || lower == "sans-serif" || lower == "ui-sans-serif" || lower.rfind(".applesystemuifont", 0) == 0 ||
	    lower.rfind(".sf", 0) == 0) {
		return L"Segoe UI";
	}
	if (lower == "monospace" || lower == "ui-monospace" || lower == "sf mono" || lower == "menlo" || lower == "monaco" ||
	    lower == "courier new" || lower == "courier") {
		return L"Cascadia Mono";
	}
	if (lower == "serif" || lower == "ui-serif" || lower == "times" || lower == "times new roman" || lower == "georgia") return L"Georgia";
	return toWide(family);
}

void setFontScale(double scale)
{
	if (scale <= 0) scale = 1.0;
	if (std::fabs(scale - g_scale) < 1e-6) return;
	g_scale = scale;
	resetFontCaches();
}

double fontScale()
{
	return g_scale;
}

int familyIdFor(const std::string &family)
{
	if (family.empty()) return -1;
	FamilyRegistry &registry = familyRegistry();
	std::scoped_lock guard(registry.lock);
	auto it = registry.byName.find(family);
	if (it != registry.byName.end()) return it->second;
	const int id = static_cast<int>(registry.names.size());
	registry.names.push_back(family);
	registry.byName.emplace(family, id);
	return id;
}

HFONT fontForFamily(const std::wstring &family, int sizePx, int weight)
{
	const int size = sizePx > 0 ? sizePx : 13;
	return createFont(family.empty() ? L"Segoe UI" : family, devicePixels(size), weight);
}

HFONT fontForId(int fontId, int sizePx, int weight)
{
	std::string family;
	{
		FamilyRegistry &registry = familyRegistry();
		std::scoped_lock guard(registry.lock);
		if (fontId >= 0 && static_cast<size_t>(fontId) < registry.names.size()) family = registry.names[static_cast<size_t>(fontId)];
	}
	return fontForFamily(faceForFamily(family), sizePx, weight);
}

int registerFontDirectory(const std::wstring &directory)
{
	WIN32_FIND_DATAW data{};
	HANDLE find = FindFirstFileW((directory + L"\\*").c_str(), &data);
	if (find == INVALID_HANDLE_VALUE) return 0;
	int count = 0;
	do {
		std::wstring name = data.cFileName;
		std::wstring lower = name;
		std::transform(lower.begin(), lower.end(), lower.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
		if (lower.size() < 4) continue;
		const std::wstring extension = lower.substr(lower.size() - 4);
		if (extension != L".ttf" && extension != L".otf") continue;
		const std::wstring path = directory + L"\\" + name;
		if (AddFontResourceExW(path.c_str(), FR_PRIVATE, nullptr) > 0) count++;
		// Keep the bytes for the runtime rasterizer, keyed by the file's family
		// name (the stem up to the first '-', which is how the shared examples
		// name their fonts: Inter-Regular.ttf -> inter).
		HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
		if (file != INVALID_HANDLE_VALUE) {
			LARGE_INTEGER size{};
			if (GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart < (64 << 20)) {
				std::vector<std::uint8_t> bytes(static_cast<size_t>(size.QuadPart));
				DWORD read = 0;
				if (ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) && read == bytes.size()) {
					std::string stem = fromWide(name.substr(0, name.size() - 4));
					const size_t dash = stem.find('-');
					if (dash != std::string::npos) stem = stem.substr(0, dash);
					TtfRegistry &registry = ttfRegistry();
					std::scoped_lock guard(registry.lock);
					registry.byFamily.emplace(lowercased(stem), std::move(bytes));
				}
			}
			CloseHandle(file);
		}
	} while (FindNextFileW(find, &data));
	FindClose(find);
	return count;
}

int fontLineHeight(HFONT font)
{
	HDC screen = GetDC(nullptr);
	HGDIOBJ previous = font ? SelectObject(screen, font) : nullptr;
	TEXTMETRICW metrics{};
	GetTextMetricsW(screen, &metrics);
	if (previous) SelectObject(screen, previous);
	ReleaseDC(nullptr, screen);
	return static_cast<int>(std::ceil((metrics.tmHeight + metrics.tmExternalLeading) / g_scale));
}

void measureText(const std::wstring &text, HFONT font, int maxWidth, bool singleLine, int *outWidth, int *outHeight)
{
	HDC screen = GetDC(nullptr);
	HGDIOBJ previous = font ? SelectObject(screen, font) : nullptr;
	RECT rect{0, 0, maxWidth > 0 ? static_cast<int>(std::lround(maxWidth * g_scale)) : 1 << 20, 0};
	UINT format = DT_CALCRECT | DT_NOPREFIX | DT_EXTERNALLEADING;
	if (singleLine) format |= DT_SINGLELINE;
	else format |= DT_WORDBREAK | DT_EDITCONTROL;
	if (text.empty()) {
		TEXTMETRICW metrics{};
		GetTextMetricsW(screen, &metrics);
		rect.right = 0;
		rect.bottom = metrics.tmHeight;
	} else {
		DrawTextW(screen, text.c_str(), static_cast<int>(text.size()), &rect, format);
	}
	if (previous) SelectObject(screen, previous);
	ReleaseDC(nullptr, screen);
	if (outWidth) *outWidth = static_cast<int>(std::ceil((rect.right - rect.left) / g_scale));
	if (outHeight) *outHeight = static_cast<int>(std::ceil((rect.bottom - rect.top) / g_scale));
}

void resetFontCaches()
{
	for (auto &entry : fontCache()) DeleteObject(entry.second);
	fontCache().clear();
}

namespace detail {
FamilyRegistry &families() { return familyRegistry(); }
TtfRegistry &ttfs() { return ttfRegistry(); }
std::string lower(const std::string &value) { return lowercased(value); }
}  // namespace detail

}  // namespace gea::win32

// Strong overrides of the framework's weak font-lookup hooks: text nodes
// render through GDI fonts, so no baked atlas is looked up; the runtime TTF
// hook feeds the canvas rasterizer from the fonts the build copied next to
// the executable.
namespace gea::framework::graphics::generated {

int lookupFontFamily(const char *family)
{
	if (!family || !family[0]) return -1;
	return gea::win32::familyIdFor(std::string(family));
}

const RasterizedFontData *lookupFontForFamily(int, int) { return nullptr; }
const RasterizedFontData *lookupFont(int) { return nullptr; }
void ensureLinked() {}

const std::uint8_t *lookupRuntimeTtfFontForFamily(int familyId, unsigned long *length)
{
	if (length) *length = 0;
	std::string family;
	{
		auto &registry = gea::win32::detail::families();
		std::scoped_lock guard(registry.lock);
		if (familyId < 0 || static_cast<size_t>(familyId) >= registry.names.size()) return nullptr;
		family = registry.names[static_cast<size_t>(familyId)];
	}
	auto &registry = gea::win32::detail::ttfs();
	std::scoped_lock guard(registry.lock);
	auto it = registry.byFamily.find(gea::win32::detail::lower(family));
	if (it == registry.byFamily.end()) return nullptr;
	if (length) *length = static_cast<unsigned long>(it->second.size());
	return it->second.data();
}

}  // namespace gea::framework::graphics::generated

// Host text measurement: the layout engine re-measures every text node on
// every layout pass, so results are memoized on their exact inputs.
extern "C" bool gea_host_measure_text(const char *text, int maxWidth, int fontId, int fontSize, int *outWidth, int *outHeight)
{
	if (!outWidth || !outHeight) return false;
	if (!text || !text[0]) {
		*outWidth = 0;
		*outHeight = 0;
		return true;
	}
	static std::mutex measureLock;
	static std::unordered_map<std::string, std::pair<int, int>> cache;
	std::string key;
	key.reserve(std::strlen(text) + 24);
	key.append(text).push_back('\x1f');
	key.append(std::to_string(maxWidth)).push_back('\x1f');
	key.append(std::to_string(fontId)).push_back('\x1f');
	key.append(std::to_string(fontSize));
	{
		std::scoped_lock guard(measureLock);
		auto it = cache.find(key);
		if (it != cache.end()) {
			*outWidth = it->second.first;
			*outHeight = it->second.second;
			return true;
		}
	}
	HFONT font = gea::win32::fontForId(fontId, fontSize > 0 ? fontSize : 13);
	gea::win32::measureText(gea::win32::toWide(text), font, maxWidth, false, outWidth, outHeight);
	{
		std::scoped_lock guard(measureLock);
		if (cache.size() > 8192) cache.clear();
		cache.emplace(std::move(key), std::make_pair(*outWidth, *outHeight));
	}
	return true;
}
