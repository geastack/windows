// Stands in for a framework unit: references the hooks as this target compiles them.
#include <cstdio>
extern "C" void gea_cycle_collection_defer_begin();
extern "C" bool gea_embedded_asset_lookup(const char *path, const unsigned char **data, unsigned long *length);
extern "C" bool gea_embedded_assets_prepare();
int main()
{
	gea_cycle_collection_defer_begin();
	std::printf("prepare %d\n", gea_embedded_assets_prepare() ? 1 : 0);
	const unsigned char *data = nullptr;
	unsigned long length = 0;
	const bool found = gea_embedded_asset_lookup("a.png", &data, &length);
	std::printf("lookup %d %lu\n", found ? 1 : 0, length);
	return 0;
}
