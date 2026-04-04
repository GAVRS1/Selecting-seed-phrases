@echo off
setlocal

REM Run from repository root.
cd /d "%~dp0"

if not exist build (
  echo [1/3] Configuring CMake...
  cmake -S . -B build
  if errorlevel 1 goto :error
)

echo [2/3] Building project...
cmake --build build --config Release
if errorlevel 1 goto :error

echo [3/3] Running recovery_tool...
if not exist recovered_wallets.txt (
  type nul > recovered_wallets.txt
)

set "TEMPLATE=abandon,ability,*,*,abandon,ability,abandon,ability,abandon,ability,abandon,ability"

build\recovery_tool.exe ^
  --template "%TEMPLATE%" ^
  --recovered-wallets "recovered_wallets.txt" ^
  --bip39-passphrase "" ^
  --paths-btc "m/84'/0'/0'/0/{i}" ^
  --paths-eth "m/44'/60'/0'/0/{i}" ^
  --paths-sol "m/44'/501'/{i}'/0'" ^
  --scan-limit 20 ^
  --max-candidates 100000 ^
  --threads 8
if errorlevel 1 goto :error

echo.
echo Done.
goto :end

:error
echo.
echo Failed with error code %errorlevel%.

:end
pause
endlocal
