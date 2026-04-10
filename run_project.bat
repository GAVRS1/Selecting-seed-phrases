@echo off
setlocal EnableExtensions EnableDelayedExpansion

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

echo [3/3] Running recovery launchers...
if not exist ".env" (
  echo Missing .env file. Copy .env.example to .env and set RECOVERY_POSTGRES_CONN.
  goto :error
)

set "TEMPLATE=*,*,*,*,*,*,*,*,*,*,*,*"
set "TON_TEMPLATE=*,*,*,*,*,*,*,*,*,*,*,*"
set "MAX_CANDIDATES=0"
set "THREADS=8"
set "SCAN_LIMIT=20"
set "STARTED_CONSOLES=0"

REM Default launch controls (can be overridden in .env)
set "RECOVERY_ENABLE_BTC=true"
set "RECOVERY_ENABLE_ETH=true"
set "RECOVERY_ENABLE_SOL=true"
set "RECOVERY_ENABLE_TON=true"
set "RECOVERY_CONSOLES_BTC=1"
set "RECOVERY_CONSOLES_ETH=1"
set "RECOVERY_CONSOLES_SOL=1"
set "RECOVERY_CONSOLES_TON=1"
set "RECOVERY_RUN_BALANCE_CHECKER=true"

call :load_env_value RECOVERY_ENABLE_BTC
call :load_env_value RECOVERY_ENABLE_ETH
call :load_env_value RECOVERY_ENABLE_SOL
call :load_env_value RECOVERY_ENABLE_TON
call :load_env_value RECOVERY_CONSOLES_BTC
call :load_env_value RECOVERY_CONSOLES_ETH
call :load_env_value RECOVERY_CONSOLES_SOL
call :load_env_value RECOVERY_CONSOLES_TON
call :load_env_value RECOVERY_RUN_BALANCE_CHECKER

call :normalize_bool RECOVERY_ENABLE_BTC
call :normalize_bool RECOVERY_ENABLE_ETH
call :normalize_bool RECOVERY_ENABLE_SOL
call :normalize_bool RECOVERY_ENABLE_TON
call :normalize_bool RECOVERY_RUN_BALANCE_CHECKER

call :normalize_count RECOVERY_CONSOLES_BTC
if errorlevel 1 goto :error
call :normalize_count RECOVERY_CONSOLES_ETH
if errorlevel 1 goto :error
call :normalize_count RECOVERY_CONSOLES_SOL
if errorlevel 1 goto :error
call :normalize_count RECOVERY_CONSOLES_TON
if errorlevel 1 goto :error

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

echo Opening recovery consoles using .env settings...
call :launch_chain btc "m/84'/0'/0'/0/{i}" RECOVERY_ENABLE_BTC RECOVERY_CONSOLES_BTC
call :launch_chain eth "m/44'/60'/0'/0/{i}" RECOVERY_ENABLE_ETH RECOVERY_CONSOLES_ETH
call :launch_chain sol "m/44'/501'/{i}'/0'" RECOVERY_ENABLE_SOL RECOVERY_CONSOLES_SOL
call :launch_chain ton "m/44'/607'/0'/{i}'" RECOVERY_ENABLE_TON RECOVERY_CONSOLES_TON

if "%STARTED_CONSOLES%"=="0" (
  echo No recovery consoles started: all chains are disabled or have zero console count.
)

if /i "%RECOVERY_RUN_BALANCE_CHECKER%"=="true" (
  if exist "run_checker.bat" (
    call run_checker.bat
  ) else (
    echo run_checker.bat was not found, cannot start balance checker.
    goto :error
  )
) else (
  echo Python balance checker launch skipped: RECOVERY_RUN_BALANCE_CHECKER=%RECOVERY_RUN_BALANCE_CHECKER%.
)

echo.
echo Recovery consoles started: %STARTED_CONSOLES%.
goto :end

:error
echo.
echo Failed with error code %errorlevel%.

:end
pause
endlocal
goto :eof

:load_env_value
set "ENV_KEY=%~1"
for /f "usebackq tokens=1* delims==" %%A in (`findstr /b /c:"%ENV_KEY%=" ".env"`) do (
  set "ENV_RAW=%%B"
  if defined ENV_RAW (
    if "!ENV_RAW:~0,1!"=="^\"" if "!ENV_RAW:~-1!"=="^\"" set "ENV_RAW=!ENV_RAW:~1,-1!"
  )
  set "%ENV_KEY%=!ENV_RAW!"
  goto :eof
)
goto :eof

:normalize_bool
set "BOOL_KEY=%~1"
call set "BOOL_VALUE=%%%BOOL_KEY%%%"
if /i "%BOOL_VALUE%"=="1" set "%BOOL_KEY%=true"
if /i "%BOOL_VALUE%"=="yes" set "%BOOL_KEY%=true"
if /i "%BOOL_VALUE%"=="on" set "%BOOL_KEY%=true"
if /i "%BOOL_VALUE%"=="0" set "%BOOL_KEY%=false"
if /i "%BOOL_VALUE%"=="no" set "%BOOL_KEY%=false"
if /i "%BOOL_VALUE%"=="off" set "%BOOL_KEY%=false"
goto :eof

:normalize_count
set "COUNT_KEY=%~1"
call set "COUNT_VALUE=%%%COUNT_KEY%%%"
for /f "delims=0123456789" %%X in ("%COUNT_VALUE%") do (
  echo Invalid numeric value for %COUNT_KEY% in .env: %COUNT_VALUE%
  exit /b 1
)
if "%COUNT_VALUE%"=="" (
  echo Empty numeric value for %COUNT_KEY% in .env.
  exit /b 1
)
set "%COUNT_KEY%=%COUNT_VALUE%"
exit /b 0

:launch_chain
set "CHAIN_NAME=%~1"
set "CHAIN_PATH=%~2"
set "CHAIN_ENABLED_KEY=%~3"
set "CHAIN_COUNT_KEY=%~4"
call set "CHAIN_ENABLED=%%%CHAIN_ENABLED_KEY%%%"
call set "CHAIN_COUNT=%%%CHAIN_COUNT_KEY%%%"

if /i not "%CHAIN_ENABLED%"=="true" (
  echo %CHAIN_NAME%: disabled in .env ^(%CHAIN_ENABLED_KEY%=%CHAIN_ENABLED%^).
  goto :eof
)
if "%CHAIN_COUNT%"=="0" (
  echo %CHAIN_NAME%: console count is 0 ^(%CHAIN_COUNT_KEY%^).
  goto :eof
)

set "CHAIN_TEMPLATE=%TEMPLATE%"
if /i "%CHAIN_NAME%"=="ton" (
  set "CHAIN_TEMPLATE=%TON_TEMPLATE%"
)

for /l %%i in (1,1,%CHAIN_COUNT%) do (
  start "%CHAIN_NAME% recovery %%i" cmd /k ""%RECOVERY_EXE%" --template "%CHAIN_TEMPLATE%" --chains "%CHAIN_NAME%" --bip39-passphrase "" --paths-%CHAIN_NAME% "%CHAIN_PATH%" --scan-limit %SCAN_LIMIT% --max-candidates %MAX_CANDIDATES% --threads %THREADS% --env-file ".env""
  set /a STARTED_CONSOLES+=1
)
goto :eof
