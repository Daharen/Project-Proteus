#include "proteus/persistence/sqlite_db.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <utility>
#include <vector>

namespace proteus::persistence {

namespace {

[[noreturn]] void throw_sqlite_error(sqlite3* db, const std::string& prefix) {
    throw std::runtime_error(prefix + ": " + sqlite3_errmsg(db));
}

void check_rc(sqlite3* db, int rc, const std::string& context) {
    if (rc != SQLITE_OK) {
        throw_sqlite_error(db, context);
    }
}

void close_db_handle(sqlite3*& db, const char* context) noexcept {
    if (db == nullptr) {
        return;
    }

    sqlite3* handle = db;
    db = nullptr;
    const int rc = sqlite3_close_v2(handle);
    if (rc != SQLITE_OK) {
#if !defined(NDEBUG)
        std::fprintf(stderr, "%s: sqlite3_close_v2 failed with rc=%d (%s)\n", context, rc, sqlite3_errstr(rc));
#endif
    }
}

}  // namespace

SqliteStatement::SqliteStatement(sqlite3_stmt* stmt) : stmt_(stmt) {}

SqliteStatement::~SqliteStatement() {
    if (stmt_ != nullptr) {
        sqlite3_finalize(stmt_);
    }
}

SqliteStatement::SqliteStatement(SqliteStatement&& other) noexcept : stmt_(std::exchange(other.stmt_, nullptr)) {}

SqliteStatement& SqliteStatement::operator=(SqliteStatement&& other) noexcept {
    if (this != &other) {
        if (stmt_ != nullptr) {
            sqlite3_finalize(stmt_);
        }
        stmt_ = std::exchange(other.stmt_, nullptr);
    }
    return *this;
}

void SqliteStatement::bind_text(int index, const std::string& value) {
    const int rc = sqlite3_bind_text(stmt_, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
        throw_sqlite_error(sqlite3_db_handle(stmt_), "bind_text failed");
    }
}

void SqliteStatement::bind_int64(int index, std::int64_t value) {
    const int rc = sqlite3_bind_int64(stmt_, index, static_cast<sqlite3_int64>(value));
    if (rc != SQLITE_OK) {
        throw_sqlite_error(sqlite3_db_handle(stmt_), "bind_int64 failed");
    }
}

void SqliteStatement::bind_double(int index, double value) {
    const int rc = sqlite3_bind_double(stmt_, index, value);
    if (rc != SQLITE_OK) {
        throw_sqlite_error(sqlite3_db_handle(stmt_), "bind_double failed");
    }
}

void SqliteStatement::bind_blob(int index, const std::vector<std::uint8_t>& value) {
    const int rc = sqlite3_bind_blob(
        stmt_,
        index,
        value.empty() ? nullptr : value.data(),
        static_cast<int>(value.size()),
        SQLITE_TRANSIENT
    );
    if (rc != SQLITE_OK) {
        throw_sqlite_error(sqlite3_db_handle(stmt_), "bind_blob failed");
    }
}

void SqliteStatement::bind_null(int index) {
    const int rc = sqlite3_bind_null(stmt_, index);
    if (rc != SQLITE_OK) {
        throw_sqlite_error(sqlite3_db_handle(stmt_), "bind_null failed");
    }
}

bool SqliteStatement::step() {
    const int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) {
        return true;
    }
    if (rc == SQLITE_DONE) {
        return false;
    }
    throw_sqlite_error(sqlite3_db_handle(stmt_), "step failed");
}

void SqliteStatement::reset() {
    const int clear_rc = sqlite3_clear_bindings(stmt_);
    if (clear_rc != SQLITE_OK) {
        throw_sqlite_error(sqlite3_db_handle(stmt_), "clear_bindings failed");
    }
    const int reset_rc = sqlite3_reset(stmt_);
    if (reset_rc != SQLITE_OK) {
        throw_sqlite_error(sqlite3_db_handle(stmt_), "reset failed");
    }
}

std::string SqliteStatement::column_text(int column) const {
    const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt_, column));
    return text == nullptr ? std::string{} : std::string{text};
}

std::int64_t SqliteStatement::column_int64(int column) const {
    return sqlite3_column_int64(stmt_, column);
}

double SqliteStatement::column_double(int column) const {
    return sqlite3_column_double(stmt_, column);
}

std::vector<std::uint8_t> SqliteStatement::column_blob(int column) const {
    const auto* blob = static_cast<const std::uint8_t*>(sqlite3_column_blob(stmt_, column));
    const int blob_size = sqlite3_column_bytes(stmt_, column);
    if (blob == nullptr || blob_size <= 0) {
        return {};
    }

    std::vector<std::uint8_t> out(static_cast<std::size_t>(blob_size));
    std::memcpy(out.data(), blob, static_cast<std::size_t>(blob_size));
    return out;
}

bool SqliteStatement::column_is_null(int column) const {
    return sqlite3_column_type(stmt_, column) == SQLITE_NULL;
}

sqlite3_stmt* SqliteStatement::native_handle() const {
    return stmt_;
}

SqliteDb::~SqliteDb() {
    close_db_handle(db_, "SqliteDb::~SqliteDb");
}

SqliteDb::SqliteDb(SqliteDb&& other) noexcept : db_(std::exchange(other.db_, nullptr)) {}

SqliteDb& SqliteDb::operator=(SqliteDb&& other) noexcept {
    if (this != &other) {
        close_db_handle(db_, "SqliteDb::operator=");
        db_ = std::exchange(other.db_, nullptr);
    }
    return *this;
}

void SqliteDb::open(const std::string& path) {
    close_db_handle(db_, "SqliteDb::open");

    std::filesystem::path fs_path(path);
    if (fs_path.has_parent_path()) {
        std::filesystem::create_directories(fs_path.parent_path());
    }

    const int rc = sqlite3_open(path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        throw_sqlite_error(db_, "sqlite open failed");
    }
    exec("PRAGMA foreign_keys = ON;");
}

void SqliteDb::exec(const std::string& sql) {
    char* err_msg = nullptr;
    const int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::string message = err_msg == nullptr ? std::string{"sqlite exec failed"} : std::string{err_msg};
        sqlite3_free(err_msg);
        throw std::runtime_error(message);
    }
}

SqliteStatement SqliteDb::prepare(const std::string& sql) const {
    sqlite3_stmt* stmt = nullptr;
    const int rc = sqlite3_prepare_v2(db_, sql.c_str(), static_cast<int>(sql.size()), &stmt, nullptr);
    check_rc(db_, rc, "prepare failed");
    return SqliteStatement(stmt);
}

sqlite3* SqliteDb::native_handle() const {
    return db_;
}

SqliteTransaction::SqliteTransaction(SqliteDb& db) : db_(db) {
    db_.exec("BEGIN IMMEDIATE TRANSACTION;");
}

SqliteTransaction::~SqliteTransaction() {
    if (!completed_) {
        db_.exec("ROLLBACK;");
    }
}

void SqliteTransaction::commit() {
    if (!completed_) {
        db_.exec("COMMIT;");
        completed_ = true;
    }
}

void SqliteTransaction::rollback() {
    if (!completed_) {
        db_.exec("ROLLBACK;");
        completed_ = true;
    }
}

}  // namespace proteus::persistence
