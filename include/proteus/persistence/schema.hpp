#pragma once

#include "proteus/persistence/sqlite_db.hpp"

namespace proteus::persistence {

inline constexpr int kSchemaVersion = 1;

void ensure_schema(SqliteDb& db);

}  // namespace proteus::persistence
