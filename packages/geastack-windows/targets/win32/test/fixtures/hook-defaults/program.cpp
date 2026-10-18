// Stands in for a generated program: defines the hooks weakly, as the compiler emits them.
#include <cstdio>
extern "C" __attribute__((weak)) void gea_cycle_collection_defer_begin() { std::printf("program defer\n"); }
extern "C" __attribute__((weak)) bool gea_embedded_asset_lookup(const char *, const unsigned char **, unsigned long *length) { *length = 7; return true; }
