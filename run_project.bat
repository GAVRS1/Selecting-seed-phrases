@echo off
setlocal

REM Run from repository root.
cd /d "%~dp0"

set "CMAKE_EXTRA_ARGS="
set "VCPKG_EFFECTIVE_ROOT="

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

echo [1/3] Configuring CMake...
cmake -S . -B build %CMAKE_EXTRA_ARGS%
if errorlevel 1 (
  echo.
  echo CMake configure failed.
  echo Check messages above. Common reasons:
  echo   - missing OpenSSL dependency
  echo   - broken/missing source files in the repository
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

echo [3/3] Running recovery_tool...
if not exist recovered_wallets.txt (
  type nul > recovered_wallets.txt
)

REM By default search all 12 positions from the active wordlist.
REM If you know some positions exactly, replace corresponding '*' with known words.
set "TEMPLATE=*,*,*,*,*,*,*,*,*,*,*,*"
REM 0 = unlimited search. Use a positive number to stop automatically after N valid candidates.
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

echo Opening separate consoles for BTC / ETH / SOL...
start "BTC recovery" cmd /k ""%RECOVERY_EXE%" --template "%TEMPLATE%" --chains "btc" --recovered-wallets "recovered_wallets.txt" --bip39-passphrase "" --paths-btc "m/84'/0'/0'/0/{i}" --scan-limit 20 --max-candidates %MAX_CANDIDATES% --threads 8"
start "ETH recovery" cmd /k ""%RECOVERY_EXE%" --template "%TEMPLATE%" --chains "eth" --recovered-wallets "recovered_wallets.txt" --bip39-passphrase "" --paths-eth "m/44'/60'/0'/0/{i}" --scan-limit 20 --max-candidates %MAX_CANDIDATES% --threads 8"
start "SOL recovery" cmd /k ""%RECOVERY_EXE%" --template "%TEMPLATE%" --chains "sol" --recovered-wallets "recovered_wallets.txt" --bip39-passphrase "" --paths-sol "m/44'/501'/{i}'/0'" --scan-limit 20 --max-candidates %MAX_CANDIDATES% --threads 8"

echo.
echo Consoles started.
goto :end

:error
echo.
echo Failed with error code %errorlevel%.

:end
pause
endlocal
