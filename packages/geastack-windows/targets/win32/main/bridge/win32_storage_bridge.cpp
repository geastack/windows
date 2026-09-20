// SPDX-License-Identifier: Apache-2.0
// Persistence bridge for the compiler runtime's `localStorage`.
//
// geatsc-compiled programs lower `localStorage.getItem/setItem/removeItem` to
// `gea::host::storage::*` in the compiler's own runtime header, which keeps
// the entries in an in-memory `std::map`. Nothing in the generated runtime
// persists it, so without this bridge every localStorage write would die with
// the process. The bridge syncs that map with the target's file-backed
// StorageService: load() before Application::init (a store's init() reads
// localStorage during mount), flush() once per frame (serialize, compare with
// the last persisted blob, write only on change). The blob framing matches
// gea::host::StorageFacade (4-byte host-order length-prefixed key/value
// chunks), so both views of localStorage stay interchangeable.
//
// This unit compiles against the generated program's runtime prelude (the
// build passes GEA_WINDOWS_RUNTIME_PRELUDE), so unlike the other target files
// it is rebuilt per app; the build only adds it when a program was generated.

#ifdef GEA_WINDOWS_RUNTIME_PRELUDE
#include GEA_WINDOWS_RUNTIME_PRELUDE
#else
#include "gea_runtime.h"
#endif

#include "services/storage_service.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace {

std::string g_last_blob;

std::string serializeEntries()
{
	std::string out;
	for (const auto &entry : gea::host::storage::table()) {
		for (const std::string *part : {&entry.first, &entry.second}) {
			const std::uint32_t length = static_cast<std::uint32_t>(part->size());
			char header[sizeof(length)];
			std::memcpy(header, &length, sizeof(length));
			out.append(header, sizeof(header));
			out.append(*part);
		}
	}
	return out;
}

}  // namespace

extern "C" void gea_win32_runtime_storage_load(void)
{
	std::string blob;
	gea::framework::services::StorageService::loadKv(blob);
	auto &table = gea::host::storage::table();
	table.clear();
	std::size_t position = 0;
	auto readChunk = [&](std::string &out) {
		if (position + sizeof(std::uint32_t) > blob.size()) return false;
		std::uint32_t length = 0;
		std::memcpy(&length, blob.data() + position, sizeof(length));
		position += sizeof(length);
		if (position + length > blob.size()) return false;
		out.assign(blob.data() + position, length);
		position += length;
		return true;
	};
	std::string key;
	std::string value;
	while (readChunk(key) && readChunk(value)) table[key] = value;
	g_last_blob = blob;
}

extern "C" void gea_win32_runtime_storage_flush(void)
{
	std::string blob = serializeEntries();
	if (blob == g_last_blob) return;
	gea::framework::services::StorageService::saveKv(blob);
	g_last_blob.swap(blob);
}
