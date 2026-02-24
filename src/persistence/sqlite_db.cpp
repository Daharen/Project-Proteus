#include "proteus/persistence/sqlite_db.hpp"

#include <cstdio>
#include <cstring>
#include <algorithm>
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

SqliteStatement::SqliteStatement(sqlite3_stmt* stmt) : stmt_(std::make_shared<sqlite3_stmt*>(stmt)) {}

SqliteStatement::SqliteStatement(sqlite3_stmt* stmt, SqliteDb* owner)
    : stmt_(std::make_shared<sqlite3_stmt*>(stmt)), owner_(owner) {
    if (owner_ != nullptr) {
        owner_->register_statement_handle(stmt_);
    }
}

SqliteStatement::~SqliteStatement() {
    if (stmt_ != nullptr && *stmt_ != nullptr) {
        sqlite3_finalize(*stmt_);
        *stmt_ = nullptr;
    }
    if (owner_ != nullptr && stmt_ != nullptr) {
        owner_->unregister_statement_handle(stmt_);
    }
}

SqliteStatement::SqliteStatement(SqliteStatement&& other) noexcept
    : stmt_(std::move(other.stmt_)), owner_(std::exchange(other.owner_, nullptr)) {}

SqliteStatement& SqliteStatement::operator=(SqliteStatement&& other) noexcept {
    if (this != &other) {
        if (stmt_ != nullptr && *stmt_ != nullptr) {
            sqlite3_finalize(*stmt_);
            *stmt_ = nullptr;
        }
        if (owner_ != nullptr && stmt_ != nullptr) {
            owner_->unregister_statement_handle(stmt_);
        }
        stmt_ = std::move(other.stmt_);
        owner_ = std::exchange(other.owner_, nullptr);
    }
    return *this;
}

sqlite3_stmt* SqliteStatement::stmt_or_throw() const {
    if (stmt_ == nullptr || *stmt_ == nullptr) {
        throw std::runtime_error("sqlite statement is closed");
    }
    return *stmt_;
}

void SqliteStatement::bind_text(int index, const std::string& value) {
    const int rc = sqlite3_bind_text(stmt_or_throw(), index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
        throw_sqlite_error(sqlite3_db_handle(stmt_or_throw()), "bind_text failed");
    }
}

void SqliteStatement::bind_int64(int index, std::int64_t value) {
    const int rc = sqlite3_bind_int64(stmt_or_throw(), index, static_cast<sqlite3_int64>(value));
    if (rc != SQLITE_OK) {
        throw_sqlite_error(sqlite3_db_handle(stmt_or_throw()), "bind_int64 failed");
    }
}

void SqliteStatement::bind_double(int index, double value) {
    const int rc = sqlite3_bind_double(stmt_or_throw(), index, value);
    if (rc != SQLITE_OK) {
        throw_sqlite_error(sqlite3_db_handle(stmt_or_throw()), "bind_double failed");
    }
}

void SqliteStatement::bind_blob(int index, const std::vector<std::uint8_t>& value) {
    const int rc = sqlite3_bind_blob(
        stmt_or_throw(),
        index,
        value.empty() ? nullptr : value.data(),
        static_cast<int>(value.size()),
        SQLITE_TRANSIENT
    );
    if (rc != SQLITE_OK) {
        throw_sqlite_error(sqlite3_db_handle(stmt_or_throw()), "bind_blob failed");
    }
}

void SqliteStatement::bind_null(int index) {
    const int rc = sqlite3_bind_null(stmt_or_throw(), index);
    if (rc != SQLITE_OK) {
        throw_sqlite_error(sqlite3_db_handle(stmt_or_throw()), "bind_null failed");
    }
}

bool SqliteStatement::step() {
    const int rc = sqlite3_step(stmt_or_throw());
    if (rc == SQLITE_ROW) {
        return true;
    }
    if (rc == SQLITE_DONE) {
        return false;
    }
    throw_sqlite_error(sqlite3_db_handle(stmt_or_throw()), "step failed");
}

void SqliteStatement::reset() {
    const int clear_rc = sqlite3_clear_bindings(stmt_or_throw());
    if (clear_rc != SQLITE_OK) {
        throw_sqlite_error(sqlite3_db_handle(stmt_or_throw()), "clear_bindings failed");
    }
    const int reset_rc = sqlite3_reset(stmt_or_throw());
    if (reset_rc != SQLITE_OK) {
        throw_sqlite_error(sqlite3_db_handle(stmt_or_throw()), "reset failed");
    }
}

std::string SqliteStatement::column_text(int column) const {
    const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt_or_throw(), column));
    return text == nullptr ? std::string{} : std::string{text};
}

std::int64_t SqliteStatement::column_int64(int column) const {
    return sqlite3_column_int64(stmt_or_throw(), column);
}

double SqliteStatement::column_double(int column) const {
    return sqlite3_column_double(stmt_or_throw(), column);
}

std::vector<std::uint8_t> SqliteStatement::column_blob(int column) const {
    const auto* blob = static_cast<const std::uint8_t*>(sqlite3_column_blob(stmt_or_throw(), column));
    const int blob_size = sqlite3_column_bytes(stmt_or_throw(), column);
    if (blob == nullptr || blob_size <= 0) {
        return {};
    }

    std::vector<std::uint8_t> out(static_cast<std::size_t>(blob_size));
    std::memcpy(out.data(), blob, static_cast<std::size_t>(blob_size));
    return out;
}

bool SqliteStatement::column_is_null(int column) const {
    return sqlite3_column_type(stmt_or_throw(), column) == SQLITE_NULL;
}

sqlite3_stmt* SqliteStatement::native_handle() const {
    return stmt_ == nullptr ? nullptr : *stmt_;
}

SqliteDb::~SqliteDb() {
    Close();
}

SqliteDb::SqliteDb(SqliteDb&& other) noexcept : db_(std::exchange(other.db_, nullptr)) {}

SqliteDb& SqliteDb::operator=(SqliteDb&& other) noexcept {
    if (this != &other) {
        Close();
        db_ = std::exchange(other.db_, nullptr);
        active_statements_ = std::move(other.active_statements_);
    }
    return *this;
}

void SqliteDb::open(const std::string& path) {
    Close();

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

void SqliteDb::Close() {
    for (const auto& stmt_handle : active_statements_) {
        if (stmt_handle != nullptr && *stmt_handle != nullptr) {
            sqlite3_finalize(*stmt_handle);
            *stmt_handle = nullptr;
        }
    }
    active_statements_.clear();
    close_db_handle(db_, "SqliteDb::Close");
}

bool SqliteDb::is_open() const {
    return db_ != nullptr;
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
    return SqliteStatement(stmt, const_cast<SqliteDb*>(this));
}

sqlite3* SqliteDb::native_handle() const {
    return db_;
}

void SqliteDb::register_statement_handle(const std::shared_ptr<sqlite3_stmt*>& stmt) const {
    active_statements_.push_back(stmt);
}

void SqliteDb::unregister_statement_handle(const std::shared_ptr<sqlite3_stmt*>& stmt) const {
    active_statements_.erase(
        std::remove_if(
            active_statements_.begin(),
            active_statements_.end(),
            [&stmt](const std::shared_ptr<sqlite3_stmt*>& entry) { return entry == stmt; }
        ),
        active_statements_.end()
    );
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
