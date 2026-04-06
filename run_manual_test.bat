@echo off
setlocal

REM Run from repository root.
cd /d "%~dp0"

set "CMAKE_EXTRA_ARGS="
set "VCPKG_EFFECTIVE_ROOT="

REM Prefer local ./vcpkg inside this repo if it exists.
if exist "%cd%\vcpkg\scripts\buildsystems\vcpkg.cmake" (
  set "VCPKG_EFFECTIVE_ROOT=%cd%\vcpkg"
) else if defined VCPKG_ROOT (
  if exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
    set "VCPKG_EFFECTIVE_ROOT=%VCPKG_ROOT%"
  )
)

if defined VCPKG_EFFECTIVE_ROOT (
  set "CMAKE_EXTRA_ARGS=%CMAKE_EXTRA_ARGS% -DCMAKE_TOOLCHAIN_FILE=%VCPKG_EFFECTIVE_ROOT%/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows"
)

if defined OPENSSL_ROOT_DIR (
  set "CMAKE_EXTRA_ARGS=%CMAKE_EXTRA_ARGS% -DOPENSSL_ROOT_DIR=%OPENSSL_ROOT_DIR%"
)

echo [1/3] Configuring CMake...
cmake -S . -B build %CMAKE_EXTRA_ARGS%
if errorlevel 1 (
  echo.
  echo OpenSSL not found.
  echo.
  echo Option 1 ^(recommended^):
  echo   1^) Install vcpkg and package: openssl:x64-windows
  echo   2^) put vcpkg near this repo in .\vcpkg OR run: setx VCPKG_ROOT C:\path\to\vcpkg
  echo   3^) Install package: %%VCPKG_ROOT%%\vcpkg install openssl:x64-windows
  echo   4^) Re-open terminal and run this script again
  echo.
  echo Option 2:
  echo   setx OPENSSL_ROOT_DIR C:\path\to\openssl
  echo   Re-open terminal and run this script again
  goto :error
)

echo [2/3] Building project...
cmake --build build --config Release
if errorlevel 1 goto :error

echo [3/3] Running manual wallet check...
if not exist recovered_wallets.txt (
  type nul > recovered_wallets.txt
)

if not exist data (
  mkdir data
)

if not exist data\manual_wallets.txt (
  > data\manual_wallets.txt echo # Format: chain,address
  >>data\manual_wallets.txt echo # btc,1BoatSLRHtKNngkdXEeobR76b53LETtpyT
  >>data\manual_wallets.txt echo # eth,0xde0B295669a9FD93d5F28D9Ec85E40f4cb697BAe
  >>data\manual_wallets.txt echo # sol,Vote111111111111111111111111111111111111111
  echo Created data\manual_wallets.txt example file.
)

set "RECOVERY_EXE=build\recovery_tool.exe"
if not exist "%RECOVERY_EXE%" (
  if exist "build\Release\recovery_tool.exe" (
    set "RECOVERY_EXE=build\Release\recovery_tool.exe"
  ) else if exist "build\Debug\recovery_tool.exe" (
    set "RECOVERY_EXE=build\Debug\recovery_tool.exe"
  )
)

if not exist "%RECOVERY_EXE%" (
  echo Could not find recovery_tool.exe in build\ ^(single-config^) or build\Release\build\Debug ^(multi-config^).
  goto :error
)

"%RECOVERY_EXE%" --manual-wallets "data\manual_wallets.txt" --chains "btc,eth,sol" --recovered-wallets "recovered_wallets.txt"
if errorlevel 1 goto :error

echo.
echo Manual wallet check finished.
goto :end

:error
echo.
echo Failed with error code %errorlevel%.

:end
pause
endlocal
