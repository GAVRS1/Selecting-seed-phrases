@echo off
setlocal EnableExtensions EnableDelayedExpansion

REM Run from repository root.
cd /d "%~dp0"

if not exist ".env" (
  echo Missing .env file. Copy .env.example to .env and set RECOVERY_POSTGRES_CONN.
  call :set_error 1
  goto :error
)

set "CMAKE_EXTRA_ARGS="
set "VCPKG_EFFECTIVE_ROOT="
set "PYTHON_BIN=python"
set "PSQL_BIN=psql"
set "MANUAL_EVM_TABLE=manual_wallets_evm"
set "MANUAL_SOL_TABLE=manual_wallets_sol"
set "RECOVERY_POSTGRES_CONN="

for /f "usebackq tokens=1,* delims==" %%A in (".env") do (
  if /I "%%~A"=="RECOVERY_POSTGRES_CONN" set "RECOVERY_POSTGRES_CONN=%%~B"
  if /I "%%~A"=="POSTGRES_CONN" if not defined RECOVERY_POSTGRES_CONN set "RECOVERY_POSTGRES_CONN=%%~B"
)

if exist "vcpkg.json" (
  powershell -NoProfile -ExecutionPolicy Bypass -Command "try { Get-Content -Raw -Path '.\vcpkg.json' | ConvertFrom-Json | Out-Null; exit 0 } catch { exit 1 }"
  if errorlevel 1 (
    echo.
    echo Detected invalid vcpkg.json in repository root.
    echo It looks like the file is not valid JSON ^(for example, a pasted PowerShell command^).
    echo.
    echo Repair with:
    echo   powershell -NoProfile -ExecutionPolicy Bypass -Command "$json = @{ name='selecting-seed-phrases'; 'version-string'='0.1.0'; dependencies=@('openssl') } ^| ConvertTo-Json -Depth 5; [System.IO.File]::WriteAllText('.\vcpkg.json', $json, (New-Object System.Text.UTF8Encoding($false)))"
    echo.
    goto :error
  )
)

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

where py >nul 2>&1
if not errorlevel 1 (
  set "PYTHON_BIN=py -3"
)

where psql >nul 2>&1
if not errorlevel 1 (
  set "PSQL_BIN=psql"
)

echo [1/4] Configuring CMake...
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

echo [2/4] Building project...
cmake --build build --config Release
if errorlevel 1 goto :error

echo [3/4] Preparing manual wallets files and temp DB tables...
if not exist recovered_wallets.txt (
  type nul > recovered_wallets.txt
)

if not exist data (
  mkdir data
)

if not exist data\manual_wallets_btc.txt (
  > data\manual_wallets_btc.txt echo # Format: chain,address
  >>data\manual_wallets_btc.txt echo # btc,1BoatSLRHtKNngkdXEeobR76b53LETtpyT
  echo Created data\manual_wallets_btc.txt example file.
)

if not exist data\manual_wallets_eth.txt (
  > data\manual_wallets_eth.txt echo # Format: chain,address
  >>data\manual_wallets_eth.txt echo # eth,0xde0B295669a9FD93d5F28D9Ec85E40f4cb697BAe
  echo Created data\manual_wallets_eth.txt example file.
)

if not exist data\manual_wallets_sol.txt (
  > data\manual_wallets_sol.txt echo # Format: chain,address
  >>data\manual_wallets_sol.txt echo # sol,Vote111111111111111111111111111111111111111
  echo Created data\manual_wallets_sol.txt example file.
)

if "%RECOVERY_POSTGRES_CONN%"=="" (
  echo.
  echo RECOVERY_POSTGRES_CONN is not set in .env/environment.
  echo Required to pass ETH/SOL wallets into evm-checker and solana-checker.
  call :set_error 1
  goto :error
)

"%PSQL_BIN%" "%RECOVERY_POSTGRES_CONN%" -v ON_ERROR_STOP=1 -c "CREATE TABLE IF NOT EXISTS %MANUAL_EVM_TABLE% (id BIGSERIAL PRIMARY KEY, address TEXT NOT NULL, mnemonic TEXT, blockchain TEXT DEFAULT 'eth', created_at TIMESTAMPTZ DEFAULT NOW()); TRUNCATE TABLE %MANUAL_EVM_TABLE%;"
if errorlevel 1 goto :error
for /f "usebackq tokens=1,2 delims=," %%A in ("data\manual_wallets_eth.txt") do (
  set "CHAIN=%%~A"
  set "ADDR=%%~B"
  if defined CHAIN if defined ADDR (
    if /I not "!CHAIN:~0,1!"=="#" (
      "%PSQL_BIN%" "%RECOVERY_POSTGRES_CONN%" -v ON_ERROR_STOP=1 -c "INSERT INTO %MANUAL_EVM_TABLE% (address, blockchain) VALUES (trim('!ADDR!'), lower(trim('!CHAIN!')));"
      if errorlevel 1 goto :error
    )
  )
)

"%PSQL_BIN%" "%RECOVERY_POSTGRES_CONN%" -v ON_ERROR_STOP=1 -c "CREATE TABLE IF NOT EXISTS %MANUAL_SOL_TABLE% (id BIGSERIAL PRIMARY KEY, address TEXT NOT NULL, mnemonic TEXT, blockchain TEXT DEFAULT 'sol', created_at TIMESTAMPTZ DEFAULT NOW()); TRUNCATE TABLE %MANUAL_SOL_TABLE%;"
if errorlevel 1 goto :error
for /f "usebackq tokens=1,2 delims=," %%A in ("data\manual_wallets_sol.txt") do (
  set "CHAIN=%%~A"
  set "ADDR=%%~B"
  if defined CHAIN if defined ADDR (
    if /I not "!CHAIN:~0,1!"=="#" (
      "%PSQL_BIN%" "%RECOVERY_POSTGRES_CONN%" -v ON_ERROR_STOP=1 -c "INSERT INTO %MANUAL_SOL_TABLE% (address, blockchain) VALUES (trim('!ADDR!'), lower(trim('!CHAIN!')));"
      if errorlevel 1 goto :error
    )
  )
)

echo [4/4] Starting manual checker consoles ^(BTC from file, ETH/SOL from temp DB tables^)...
start "Manual BTC checker" cmd /k "%PYTHON_BIN% scripts\check_wallet_balances.py --manual-wallets data\manual_wallets_btc.txt --output recovered_wallets.txt --delay-seconds 0.2"
start "Manual ETH checker" cmd /k "set POSTGRES_CONN=%RECOVERY_POSTGRES_CONN%&& set POSTGRES_SOURCE_TABLE=%MANUAL_EVM_TABLE%&& npm --prefix scripts\evm-checker start -- all"
start "Manual SOL checker" cmd /k "set POSTGRES_CONN=%RECOVERY_POSTGRES_CONN%&& set POSTGRES_SOURCE_TABLE=%MANUAL_SOL_TABLE%&& npm --prefix scripts\solana-checker start -- all"

echo.
echo Started 3 manual checker consoles:
echo   - BTC from data\manual_wallets_btc.txt
echo   - ETH via table %MANUAL_EVM_TABLE% from data\manual_wallets_eth.txt
echo   - SOL via table %MANUAL_SOL_TABLE% from data\manual_wallets_sol.txt
goto :end

:error
echo.
echo Failed with error code %errorlevel%.
exit /b %errorlevel%

:end
pause
endlocal
exit /b 0

:set_error
exit /b %~1
