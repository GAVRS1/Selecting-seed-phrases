@echo off
setlocal

REM Run from repository root.
cd /d "%~dp0"

set "CMAKE_EXTRA_ARGS="
set "VCPKG_EFFECTIVE_ROOT="
set "PYTHON_BIN=python"

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

where py >nul 2>&1
if not errorlevel 1 (
  set "PYTHON_BIN=py -3"
)

echo [1/3] Configuring CMake...
cmake -S . -B build %CMAKE_EXTRA_ARGS%
if errorlevel 1 goto :error

echo [2/3] Building project...
cmake --build build --config Release
if errorlevel 1 goto :error

echo [3/3] Opening 25 BTC + 25 ETH consoles ^(C++ derive only^) ...
if not exist ".env" (
  echo Missing .env file. Copy .env.example to .env and set RECOVERY_POSTGRES_CONN.
  goto :error
)

set "TEMPLATE=*,*,*,*,*,*,*,*,*,*,*,*"
set "MAX_CANDIDATES=0"
set "THREADS=8"
set "SCAN_LIMIT=20"

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

for /l %%i in (1,1,25) do (
  start "BTC recovery %%i" cmd /k ""%RECOVERY_EXE%" --template "%TEMPLATE%" --chains "btc" --bip39-passphrase "" --paths-btc "m/84'/0'/0'/0/{i}" --scan-limit %SCAN_LIMIT% --max-candidates %MAX_CANDIDATES% --threads %THREADS% --env-file ".env""
)

for /l %%i in (1,1,25) do (
  start "ETH recovery %%i" cmd /k ""%RECOVERY_EXE%" --template "%TEMPLATE%" --chains "eth" --bip39-passphrase "" --paths-eth "m/44'/60'/0'/0/{i}" --scan-limit %SCAN_LIMIT% --max-candidates %MAX_CANDIDATES% --threads %THREADS% --env-file ".env""
)

start "Python balance checker" cmd /k "%PYTHON_BIN% scripts\check_wallet_balances.py --env-file .env --output recovered_wallets.txt --delay-seconds 0.2"

echo.
echo Started 50 consoles total: 25 BTC and 25 ETH + balance checker.
goto :end

:error
echo.
echo Failed with error code %errorlevel%.

:end
pause
endlocal
