#pragma once

#include "sqlite3.h"

#include <cstdint>
#include <string>
#include <vector>

namespace proteus::persistence {

class SqliteStatement {
public:
    explicit SqliteStatement(sqlite3_stmt* stmt = nullptr);
    ~SqliteStatement();

    SqliteStatement(const SqliteStatement&) = delete;
    SqliteStatement& operator=(const SqliteStatement&) = delete;

    SqliteStatement(SqliteStatement&& other) noexcept;
    SqliteStatement& operator=(SqliteStatement&& other) noexcept;

    void bind_text(int index, const std::string& value);
    void bind_int64(int index, std::int64_t value);
    void bind_double(int index, double value);
    void bind_blob(int index, const std::vector<std::uint8_t>& value);
    void bind_null(int index);

    bool step();
    void reset();

    std::string column_text(int column) const;
    std::int64_t column_int64(int column) const;
    double column_double(int column) const;
    std::vector<std::uint8_t> column_blob(int column) const;
    bool column_is_null(int column) const;

    sqlite3_stmt* native_handle() const;

private:
    sqlite3_stmt* stmt_ = nullptr;
};

class SqliteDb {
public:
    SqliteDb() = default;
    ~SqliteDb();

    SqliteDb(const SqliteDb&) = delete;
    SqliteDb& operator=(const SqliteDb&) = delete;

    SqliteDb(SqliteDb&& other) noexcept;
    SqliteDb& operator=(SqliteDb&& other) noexcept;

    void open(const std::string& path);
    void exec(const std::string& sql);
    SqliteStatement prepare(const std::string& sql) const;

    sqlite3* native_handle() const;

private:
    sqlite3* db_ = nullptr;
};

class SqliteTransaction {
public:
    explicit SqliteTransaction(SqliteDb& db);
    ~SqliteTransaction();

    SqliteTransaction(const SqliteTransaction&) = delete;
    SqliteTransaction& operator=(const SqliteTransaction&) = delete;

    void commit();
    void rollback();

private:
    SqliteDb& db_;
    bool completed_ = false;
};

}  // namespace proteus::persistence
