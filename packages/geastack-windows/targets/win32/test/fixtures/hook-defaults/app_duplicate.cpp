// A second, different strong definition of the same hook: a genuine duplicate.
extern "C" bool gea_embedded_asset_lookup(const char *, const unsigned char **, unsigned long *length) { *length = 43; return true; }
