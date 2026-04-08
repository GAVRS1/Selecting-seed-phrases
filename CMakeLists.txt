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
    src/chains/ton_module.cpp
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

function(add_recovery_test target_name source_file)
    if(EXISTS "${CMAKE_SOURCE_DIR}/${source_file}")
        add_executable("${target_name}" "${source_file}")
        target_link_libraries("${target_name}" PRIVATE recovery_core)
        add_test(NAME "${target_name}" COMMAND "${target_name}")
    else()
        message(WARNING "Skipping ${target_name}: missing source file ${source_file}")
    endif()
endfunction()

add_recovery_test(test_bip39 tests/test_bip39.cpp)
add_recovery_test(test_pipeline tests/test_pipeline.cpp)
add_recovery_test(test_derivation tests/test_derivation.cpp)
add_recovery_test(test_cli_args tests/test_cli_args.cpp)
add_recovery_test(test_mnemonic_generator tests/test_mnemonic_generator.cpp)
