// SPDX-License-Identifier: Apache-2.0
// Real HTTP(S) for the framework's fetch facade, via WinHTTP, and the WiFi
// status driver apps gate remote fetches on.
//
// core's host/host/fetch.cpp has a desktop arm that calls two WEAK hooks --
// test_record_request(url, init) then test_canned_response(url) -- and returns
// whatever the latter produces. This file strong-overrides that seam with
// WinHTTP, the system HTTP stack (proxy settings, TLS, and redirects
// come from it). The seam is synchronous per call; callers
// are fetch workers, detached fetchAsync threads or the frame thread.

#include "host/fetch.h"
#include "wifi.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
// winsock2 before windows.h (the framework build defines WIN32_LEAN_AND_MEAN,
// but this order is what keeps the two socket headers from colliding).
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#include <iphlpapi.h>

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

namespace {

thread_local gea::host::FetchRequestInit t_pending_init;
thread_local bool t_has_pending_init = false;

std::wstring widen(const std::string &utf8)
{
	if (utf8.empty()) return std::wstring();
	const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
	std::wstring wide(static_cast<size_t>(std::max(needed, 0)), L'\0');
	if (needed > 0) MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), needed);
	return wide;
}

std::string narrow(const std::wstring &wide)
{
	if (wide.empty()) return std::string();
	const int needed = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
	std::string utf8(static_cast<size_t>(std::max(needed, 0)), '\0');
	if (needed > 0) WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), needed, nullptr, nullptr);
	return utf8;
}

// One session for every caller: WinHTTP pools connections per host across
// threads, so sequential tile fetches pay the handshake once.
HINTERNET sharedSession()
{
	static HINTERNET session = [] {
		HINTERNET created = WinHttpOpen(L"gea-windows/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
		if (created) {
			// WinHTTP decompression can abort successful body reads (E_ABORT).
			DWORD protocols = WINHTTP_PROTOCOL_FLAG_HTTP2;
			WinHttpSetOption(created, WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL, &protocols, sizeof(protocols));
		}
		return created;
	}();
	return session;
}

gea::host::FetchResponse sessionFetch(const std::string &url, const gea::host::FetchRequestInit &init)
{
	gea::host::FetchResponse response;
	HINTERNET session = sharedSession();
	if (!session) return response;
	const std::wstring wideUrl = widen(url);
	URL_COMPONENTS parts{};
	parts.dwStructSize = sizeof(parts);
	wchar_t host[256] = {0};
	wchar_t path[4096] = {0};
	parts.lpszHostName = host;
	parts.dwHostNameLength = 256;
	parts.lpszUrlPath = path;
	parts.dwUrlPathLength = 4096;
	if (!WinHttpCrackUrl(wideUrl.c_str(), static_cast<DWORD>(wideUrl.size()), 0, &parts)) {
		std::fprintf(stderr, "[win32 net] fetch rejected malformed url %s\n", url.c_str());
		return response;
	}
	const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
	HINTERNET connection = WinHttpConnect(session, host, parts.nPort, 0);
	if (!connection) return response;
	const std::string method = init.method.empty() ? "GET" : init.method;
	HINTERNET request = WinHttpOpenRequest(connection, widen(method).c_str(), path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
	                                       secure ? WINHTTP_FLAG_SECURE : 0);
	if (!request) {
		WinHttpCloseHandle(connection);
		return response;
	}
	if (init.timeout_ms > 0) WinHttpSetTimeouts(request, init.timeout_ms, init.timeout_ms, init.timeout_ms, init.timeout_ms);
	else WinHttpSetTimeouts(request, 30000, 30000, 30000, 30000);
	std::wstring headers;
	for (const auto &kv : init.headers) headers += widen(kv.first) + L": " + widen(kv.second) + L"\r\n";
	const bool hasBody = !init.body.empty() && method != "GET" && method != "HEAD";
	BOOL sent = WinHttpSendRequest(request, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
	                               headers.empty() ? 0 : static_cast<DWORD>(-1L), hasBody ? const_cast<char *>(init.body.data()) : WINHTTP_NO_REQUEST_DATA,
	                               hasBody ? static_cast<DWORD>(init.body.size()) : 0, hasBody ? static_cast<DWORD>(init.body.size()) : 0, 0);
	if (sent) sent = WinHttpReceiveResponse(request, nullptr);
	if (!sent) {
		std::fprintf(stderr, "[win32 net] fetch %s failed: winhttp error %lu\n", url.c_str(), GetLastError());
		WinHttpCloseHandle(request);
		WinHttpCloseHandle(connection);
		return response;
	}
	DWORD status = 0;
	DWORD statusSize = sizeof(status);
	WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
	                    WINHTTP_NO_HEADER_INDEX);
	response.status = static_cast<double>(status);
	response.ok = status >= 200 && status < 300;
	DWORD textSize = 0;
	WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_TEXT, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &textSize, WINHTTP_NO_HEADER_INDEX);
	if (textSize > 0) {
		std::wstring text(textSize / sizeof(wchar_t), L'\0');
		if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_TEXT, WINHTTP_HEADER_NAME_BY_INDEX, text.data(), &textSize, WINHTTP_NO_HEADER_INDEX)) {
			text.resize(textSize / sizeof(wchar_t));
			response.status_text = narrow(text);
		}
	}
	DWORD rawSize = 0;
	WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &rawSize, WINHTTP_NO_HEADER_INDEX);
	if (rawSize > 0) {
		std::wstring raw(rawSize / sizeof(wchar_t), L'\0');
		if (WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, raw.data(), &rawSize, WINHTTP_NO_HEADER_INDEX)) {
			raw.resize(rawSize / sizeof(wchar_t));
			size_t start = raw.find(L"\r\n");
			while (start != std::wstring::npos) {
				start += 2;
				const size_t end = raw.find(L"\r\n", start);
				const std::wstring line = raw.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
				const size_t colon = line.find(L':');
				if (colon != std::wstring::npos) {
					std::wstring name = line.substr(0, colon);
					std::wstring value = line.substr(colon + 1);
					while (!value.empty() && value.front() == L' ') value.erase(value.begin());
					response.headers[narrow(name)] = narrow(value);
				}
				start = end;
			}
		}
	}
	for (;;) {
		DWORD available = 0;
		if (!WinHttpQueryDataAvailable(request, &available) || available == 0) break;
		const size_t offset = response.body.size();
		response.body.resize(offset + available);
		DWORD read = 0;
		if (!WinHttpReadData(request, response.body.data() + offset, available, &read)) break;
		response.body.resize(offset + read);
		if (read == 0) break;
	}
	WinHttpCloseHandle(request);
	WinHttpCloseHandle(connection);
	return response;
}

}  // namespace

namespace gea::framework::host {

void test_record_request(const std::string &, const gea::host::FetchRequestInit &init)
{
	t_pending_init = init;
	t_has_pending_init = true;
}

gea::host::FetchResponse test_canned_response(const std::string &url)
{
	gea::host::FetchRequestInit init;
	if (t_has_pending_init) {
		init = std::move(t_pending_init);
		t_pending_init = {};
		t_has_pending_init = false;
	}
	return sessionFetch(url, init);
}

}  // namespace gea::framework::host

namespace {

// "Connected" = an adapter with a default gateway is up. True for wired
// Ethernet too, which is the honest answer to "can I reach the network".
struct AdapterInfo {
	bool connected = false;
	std::string name;
	std::string ip;
	std::string mac;
};

AdapterInfo primaryAdapter()
{
	AdapterInfo info;
	ULONG size = 16 * 1024;
	std::vector<unsigned char> buffer(size);
	const ULONG flags = GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
	ULONG result = GetAdaptersAddresses(AF_INET, flags, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data()), &size);
	if (result == ERROR_BUFFER_OVERFLOW) {
		buffer.resize(size);
		result = GetAdaptersAddresses(AF_INET, flags, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data()), &size);
	}
	if (result != NO_ERROR) return info;
	for (auto *adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data()); adapter; adapter = adapter->Next) {
		if (adapter->OperStatus != IfOperStatusUp) continue;
		if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
		if (!adapter->FirstGatewayAddress) continue;
		info.connected = true;
		info.name = narrow(adapter->FriendlyName ? adapter->FriendlyName : L"");
		for (auto *unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next) {
			if (unicast->Address.lpSockaddr->sa_family != AF_INET) continue;
			char text[INET_ADDRSTRLEN] = {0};
			const auto *address = reinterpret_cast<sockaddr_in *>(unicast->Address.lpSockaddr);
			if (inet_ntop(AF_INET, &address->sin_addr, text, sizeof(text))) info.ip = text;
			break;
		}
		if (adapter->PhysicalAddressLength == 6) {
			char text[18];
			std::snprintf(text, sizeof(text), "%02x:%02x:%02x:%02x:%02x:%02x", adapter->PhysicalAddress[0], adapter->PhysicalAddress[1],
			              adapter->PhysicalAddress[2], adapter->PhysicalAddress[3], adapter->PhysicalAddress[4], adapter->PhysicalAddress[5]);
			info.mac = text;
		}
		break;
	}
	return info;
}

class Win32WifiDriver final : public gea::framework::network::WifiDriver {
public:
	bool init() override { return true; }
	bool enabled() const override { return true; }
	void setEnabled(bool) override {}
	bool connected() const override { return primaryAdapter().connected; }
	int rssi() override { return connected() ? -50 : 0; }
	std::string ssid() override { return primaryAdapter().name; }
	std::string ip() const override { return primaryAdapter().ip; }
	std::string mac() override { return primaryAdapter().mac; }
	void configure(const std::string &, const std::string &) override {}
	void scan() override {}
	bool scanning() const override { return false; }
	int scanCount() const override { return 0; }
	gea::framework::network::WifiNetwork networkAt(int) const override { return {}; }
	std::vector<gea::framework::network::WifiNetwork> scanResults() const override { return {}; }
};

}  // namespace

namespace gea::win32 {

void installWifiDriver()
{
	static Win32WifiDriver driver;
	gea::framework::network::WifiAdapter::setDriver(&driver);
}

}  // namespace gea::win32
