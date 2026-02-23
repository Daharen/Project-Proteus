cmake_minimum_required(VERSION 3.20)
project(ProjectProteus LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SQLite3 REQUIRED)
find_package(OpenSSL REQUIRED)

add_library(proteus_core
    src/simulation/entity.cpp
    src/inference/belief_state.cpp
    src/inference/question_selector.cpp
    src/inference/novelty_hooks.cpp
    src/inference/identity.cpp
    src/content/in_memory_graph.cpp
    src/persistence/sqlite_db.cpp
    src/persistence/schema.cpp
    src/playable/canonicalize.cpp
    src/playable/prompt_cache.cpp
    src/playable/proposal_schema.cpp
    src/playable/feature_extractor.cpp
    src/playable/topology.cpp
    src/playable/governor.cpp
    src/playable/proposal_selector.cpp
    src/playable/retrieval_engine.cpp
    src/playable/http_server.cpp
    src/query/query_identity.cpp
)

target_include_directories(proteus_core PUBLIC include)
if(TARGET SQLite3::SQLite3)
    set(PROTEUS_SQLITE_TARGET SQLite3::SQLite3)
elseif(TARGET SQLite::SQLite3)
    set(PROTEUS_SQLITE_TARGET SQLite::SQLite3)
else()
    message(FATAL_ERROR "No compatible SQLite CMake target found (expected SQLite3::SQLite3 or SQLite::SQLite3)")
endif()

target_link_libraries(proteus_core PUBLIC ${PROTEUS_SQLITE_TARGET} OpenSSL::Crypto)

add_executable(proteus_cli src/main.cpp)
set_target_properties(proteus_cli PROPERTIES OUTPUT_NAME proteus)
target_link_libraries(proteus_cli PRIVATE proteus_core)

add_executable(proteus_20q_demo src/cli/run_20q_demo.cpp)
target_link_libraries(proteus_20q_demo PRIVATE proteus_core)

enable_testing()

add_executable(proteus_tests
    tests/gtest_main.cpp
    tests/test_belief_state.cpp
    tests/test_identity_inference.cpp
    tests/test_playable_retrieval.cpp
    tests/test_query_identity.cpp
)
target_include_directories(proteus_tests PRIVATE tests/third_party)
target_link_libraries(proteus_tests PRIVATE proteus_core)
add_test(NAME proteus_tests COMMAND proteus_tests)
