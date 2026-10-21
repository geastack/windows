// SPDX-License-Identifier: Apache-2.0
// Defaults for the optional hooks a generated program may define.
//
// The framework calls these hooks when the program provides them. On COFF a
// weak reference cannot meet a weak definition (each carries its own default,
// which lld-link and link.exe report as a duplicate), so this target compiles
// the references as ordinary ones (GEA_LINKED_HOOK_DEFAULTS) and supplies each
// default through /alternatename, the linker's documented fallback: it applies
// only while the symbol has no definition. A program's definition (weak or
// not) or an app's replaces the default; two strong definitions remain a
// duplicate-symbol error.
extern "C" void gea_win32_default_cycle_collection_defer() {}

extern "C" bool gea_win32_default_embedded_asset_lookup(const char *, const unsigned char **, unsigned long *) { return false; }

extern "C" bool gea_win32_default_embedded_assets_prepare() { return true; }

#pragma comment(linker, "/alternatename:gea_cycle_collection_defer_begin=gea_win32_default_cycle_collection_defer")
#pragma comment(linker, "/alternatename:gea_cycle_collection_defer_end=gea_win32_default_cycle_collection_defer")
#pragma comment(linker, "/alternatename:gea_embedded_asset_lookup=gea_win32_default_embedded_asset_lookup")
#pragma comment(linker, "/alternatename:gea_embedded_assets_prepare=gea_win32_default_embedded_assets_prepare")
