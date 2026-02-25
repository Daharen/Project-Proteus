cmake_minimum_required(VERSION 3.20)
project(ProjectProteus LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenSSL REQUIRED)

option(PROTEUS_ENABLE_OPENAI "Enable OpenAI responses client" ON)

add_subdirectory(third_party/sqlite)

add_library(proteus_core
    src/simulation/entity.cpp
    src/inference/belief_state.cpp
    src/inference/question_selector.cpp
    src/inference/novelty_hooks.cpp
    src/inference/identity.cpp
    src/content/in_memory_graph.cpp
    src/persistence/sqlite_db.cpp
    src/persistence/schema.cpp
    src/math/deterministic_math.cpp
    src/analytics/behavior_aggregation.cpp
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
    src/llm/llm_cache_client.cpp
    src/bootstrap/import_novel_query_artifact.cpp
    src/proteus/responders/response_envelope.cpp
    src/proteus/responders/response_validation.cpp
    src/proteus/responders/responder_manager.cpp
    src/proteus/responders/deterministic_stub_responder.cpp
)

target_include_directories(proteus_core
    PUBLIC
        third_party/sqlite
        include
        src
)

target_link_libraries(proteus_core PUBLIC proteus_sqlite OpenSSL::Crypto)

if(PROTEUS_ENABLE_OPENAI)
    if(NOT TARGET OpenSSL::SSL)
        message(FATAL_ERROR "PROTEUS_ENABLE_OPENAI=ON requires OpenSSL (SSL and Crypto). Install OpenSSL (for example via vcpkg) and reconfigure.")
    endif()

    target_sources(proteus_core PRIVATE src/llm/openai/openai_responses_client.cpp)
    target_compile_definitions(proteus_core
        PUBLIC
            PROTEUS_ENABLE_OPENAI=1
            CPPHTTPLIB_OPENSSL_SUPPORT
            HTTPLIB_OPENSSL_SUPPORT
    )
    target_link_libraries(proteus_core PUBLIC OpenSSL::SSL OpenSSL::Crypto)
endif()

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
    tests/test_behavior_aggregation_determinism.cpp
    tests/test_llm_bootstrap.cpp
    tests/responders/test_response_envelope.cpp
    tests/responders/test_response_validation.cpp
    tests/integration/test_responder_rerun_stability.cpp
)
target_include_directories(proteus_tests PRIVATE tests/third_party)
target_link_libraries(proteus_tests PRIVATE proteus_core)
add_test(NAME proteus_tests COMMAND proteus_tests)
