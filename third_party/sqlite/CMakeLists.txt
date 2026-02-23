add_library(proteus_sqlite STATIC sqlite3.c)

target_include_directories(proteus_sqlite
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
)

target_compile_definitions(proteus_sqlite
    PUBLIC
        SQLITE_THREADSAFE=1
        SQLITE_ENABLE_FTS5
        SQLITE_ENABLE_FTS4
        SQLITE_ENABLE_JSON1
        SQLITE_ENABLE_RTREE
        SQLITE_OMIT_LOAD_EXTENSION
)

if (WIN32)
    target_link_libraries(proteus_sqlite PUBLIC winsqlite3)
else()
    target_link_libraries(proteus_sqlite PUBLIC sqlite3)
endif()
