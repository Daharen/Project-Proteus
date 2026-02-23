#include "sqlite3.h"

/*
 * NOTE: Offline environment fallback shim.
 * The full SQLite amalgamation source was not available in this container.
 * We keep this translation unit so `proteus_sqlite` remains a concrete target,
 * while actual SQLite symbols are resolved from the platform sqlite library.
 */
int proteus_sqlite_vendor_shim_symbol(void) {
    return sqlite3_libversion_number();
}
