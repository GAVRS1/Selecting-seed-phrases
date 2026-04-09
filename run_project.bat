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
  echo CMake configure failed.
  goto :error
)

echo [2/3] Building project...
cmake --build build --config Release
if errorlevel 1 goto :error

echo [3/3] Running recovery + separate Python balance checker...
if not exist ".env" (
  echo Missing .env file. Copy .env.example to .env and set RECOVERY_POSTGRES_CONN.
  goto :error
)

set "TEMPLATE=*,*,*,*,*,*,*,*,*,*,*,*"
set "MAX_CANDIDATES=0"
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

echo Opening separate consoles for BTC / ETH / SOL / TON ^(C++ derive only, no balance check^) ...
start "BTC recovery" cmd /k ""%RECOVERY_EXE%" --template "%TEMPLATE%" --chains "btc" --bip39-passphrase "" --paths-btc "m/84'/0'/0'/0/{i}" --scan-limit 20 --max-candidates %MAX_CANDIDATES% --threads 8 --env-file ".env""
start "ETH recovery" cmd /k ""%RECOVERY_EXE%" --template "%TEMPLATE%" --chains "eth" --bip39-passphrase "" --paths-eth "m/44'/60'/0'/0/{i}" --scan-limit 20 --max-candidates %MAX_CANDIDATES% --threads 8 --env-file ".env""
start "SOL recovery" cmd /k ""%RECOVERY_EXE%" --template "%TEMPLATE%" --chains "sol" --bip39-passphrase "" --paths-sol "m/44'/501'/{i}'/0'" --scan-limit 20 --max-candidates %MAX_CANDIDATES% --threads 8 --env-file ".env""
start "TON recovery" cmd /k ""%RECOVERY_EXE%" --template "%TEMPLATE%" --chains "ton" --bip39-passphrase "" --paths-ton "m/44'/607'/0'/{i}'" --scan-limit 20 --max-candidates %MAX_CANDIDATES% --threads 8 --env-file ".env""

start "Python balance checker" cmd /k "%PYTHON_BIN% scripts\check_wallet_balances.py --env-file .env --output recovered_wallets.txt --delay-seconds 0.2"

echo.
echo Consoles started.
goto :end

:error
echo.
echo Failed with error code %errorlevel%.

:end
pause
endlocal
