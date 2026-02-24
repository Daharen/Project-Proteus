#pragma once

#include "proteus/persistence/sqlite_db.hpp"

#include <string>

namespace proteus::persistence {

inline constexpr int kSchemaVersion = 9;

void ensure_schema(SqliteDb& db);
void open_and_migrate(SqliteDb& db, const std::string& db_path, bool verbose = false);

}  // namespace proteus::persistence
