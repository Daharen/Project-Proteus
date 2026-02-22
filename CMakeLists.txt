cmake_minimum_required(VERSION 3.20)
project(ProjectProteus LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(proteus_core
    src/simulation/entity.cpp
    src/inference/belief_state.cpp
    src/inference/question_selector.cpp
    src/inference/novelty_hooks.cpp
    src/content/in_memory_graph.cpp
)

target_include_directories(proteus_core PUBLIC include)

add_executable(proteus_cli src/main.cpp)
target_link_libraries(proteus_cli PRIVATE proteus_core)

add_executable(proteus_20q_demo src/cli/run_20q_demo.cpp)
target_link_libraries(proteus_20q_demo PRIVATE proteus_core)

enable_testing()

add_executable(proteus_tests
    tests/gtest_main.cpp
    tests/test_belief_state.cpp
)
target_include_directories(proteus_tests PRIVATE tests/third_party)
target_link_libraries(proteus_tests PRIVATE proteus_core)
add_test(NAME proteus_tests COMMAND proteus_tests)
