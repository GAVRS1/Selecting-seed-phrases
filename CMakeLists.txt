cmake_minimum_required(VERSION 3.16)
project(wallet_recovery LANGUAGES CXX)

# Windows quality-of-life: if user installed dependencies with vcpkg and set
# VCPKG_ROOT in environment, help CMake locate OpenSSL even without explicitly
# passing CMAKE_TOOLCHAIN_FILE.
if(WIN32 AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(EXISTS "${CMAKE_SOURCE_DIR}/vcpkg/installed/x64-windows")
        list(APPEND CMAKE_PREFIX_PATH
            "${CMAKE_SOURCE_DIR}/vcpkg/installed/x64-windows"
            "${CMAKE_SOURCE_DIR}/vcpkg/installed/x64-windows-static"
        )
    endif()

    if(DEFINED ENV{VCPKG_ROOT})
        list(APPEND CMAKE_PREFIX_PATH
            "$ENV{VCPKG_ROOT}/installed/x64-windows"
            "$ENV{VCPKG_ROOT}/installed/x64-windows-static"
        )
    endif()

    if(DEFINED ENV{OPENSSL_ROOT_DIR})
        list(APPEND CMAKE_PREFIX_PATH "$ENV{OPENSSL_ROOT_DIR}")
    endif()
endif()

find_package(OpenSSL QUIET)
if(NOT OpenSSL_FOUND)
    message(FATAL_ERROR
        "OpenSSL was not found. On Windows, run these steps then re-configure:
"
        "  1) setx VCPKG_ROOT C:/path/to/vcpkg
"
        "  2) %VCPKG_ROOT%/vcpkg install openssl:x64-windows
"
        "  3) cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
"
        "Alternative: cmake -S . -B build -DOPENSSL_ROOT_DIR=C:/path/to/openssl"
    )
endif()

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

add_executable(test_cli_args tests/test_cli_args.cpp)
target_link_libraries(test_cli_args PRIVATE recovery_core)
add_test(NAME test_cli_args COMMAND test_cli_args)
