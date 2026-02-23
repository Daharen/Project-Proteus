#pragma once

#include "proteus/persistence/sqlite_db.hpp"

#include <string>

namespace proteus::persistence {

inline constexpr int kSchemaVersion = 6;

void ensure_schema(SqliteDb& db);
void open_and_migrate(SqliteDb& db, const std::string& db_path);

}  // namespace proteus::persistence
