cmake_minimum_required(VERSION 3.16)
project(wallet_recovery LANGUAGES CXX)

find_package(OpenSSL REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(recovery_core
    src/bip39/wordlist.cpp
    src/bip39/mnemonic_validator.cpp
    src/bip39/mnemonic_generator.cpp
    src/chains/bitcoin_module.cpp
    src/chains/ethereum_module.cpp
    src/chains/solana_module.cpp
    src/chains/common.cpp
    src/engine/matcher.cpp
    src/engine/pipeline.cpp
    src/cli/args.cpp
)

target_include_directories(recovery_core PUBLIC include)
target_link_libraries(recovery_core PUBLIC OpenSSL::Crypto)

add_executable(recovery_tool src/main.cpp)
target_link_libraries(recovery_tool PRIVATE recovery_core)

enable_testing()

add_executable(test_bip39 tests/test_bip39.cpp)
target_link_libraries(test_bip39 PRIVATE recovery_core)
add_test(NAME test_bip39 COMMAND test_bip39)

add_executable(test_pipeline tests/test_pipeline.cpp)
target_link_libraries(test_pipeline PRIVATE recovery_core)
add_test(NAME test_pipeline COMMAND test_pipeline)

add_executable(test_derivation tests/test_derivation.cpp)
target_link_libraries(test_derivation PRIVATE recovery_core)
add_test(NAME test_derivation COMMAND test_derivation)
