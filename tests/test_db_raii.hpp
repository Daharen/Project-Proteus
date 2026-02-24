#pragma once

#include "proteus/persistence/schema.hpp"
#include "proteus/persistence/sqlite_db.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <sstream>
#include <string>

namespace proteus::tests {

class TestSqliteDbFile {
public:
    explicit TestSqliteDbFile(const std::string& test_name, bool auto_migrate = true) {
        const std::uint64_t n = counter_.fetch_add(1, std::memory_order_relaxed);
        const std::string key = test_name + ":" + std::to_string(n);
        const auto suffix = static_cast<unsigned long long>(std::hash<std::string>{}(key) & 0xFFFFFFFFULL);

        std::ostringstream name;
        name << "proteus_test_" << std::hex << suffix << ".db";
        path_ = std::filesystem::temp_directory_path() / name.str();

        std::error_code ec;
        std::filesystem::remove(path_, ec);
        db_.open(path_.string());
        if (auto_migrate) {
            migrate();
        }
    }

    ~TestSqliteDbFile() noexcept {
        db_.Close();
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    TestSqliteDbFile(const TestSqliteDbFile&) = delete;
    TestSqliteDbFile& operator=(const TestSqliteDbFile&) = delete;

    void migrate() {
        persistence::ensure_schema(db_);
    }

    persistence::SqliteDb& db() { return db_; }
    const std::filesystem::path& path() const { return path_; }

private:
    inline static std::atomic<std::uint64_t> counter_{0};
    std::filesystem::path path_;
    persistence::SqliteDb db_;
};

}  // namespace proteus::tests
