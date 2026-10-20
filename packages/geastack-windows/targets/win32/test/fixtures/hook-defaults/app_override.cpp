// An app's own definition of a hook.
extern "C" bool gea_embedded_asset_lookup(const char *, const unsigned char **, unsigned long *length) { *length = 42; return true; }
extern "C" bool gea_embedded_assets_prepare() { return true; }
