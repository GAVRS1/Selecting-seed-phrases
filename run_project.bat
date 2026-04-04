@echo off
setlocal

REM Run from repository root.
cd /d "%~dp0"

set "BUILD_DIR=build"
set "CONFIG=Release"

if "%CMAKE_GENERATOR%"=="" (
  echo [1/3] Configuring CMake (default generator)...
  cmake -S . -B "%BUILD_DIR%"
) else (
  echo [1/3] Configuring CMake (generator: %CMAKE_GENERATOR%)...
  cmake -S . -B "%BUILD_DIR%" -G "%CMAKE_GENERATOR%"
)
if errorlevel 1 goto :configure_error

echo [2/3] Building project...
cmake --build "%BUILD_DIR%" --config "%CONFIG%"
if errorlevel 1 goto :error

echo [3/3] Running recovery_tool...
if not exist recovered_wallets.txt (
  type nul > recovered_wallets.txt
)

set "TEMPLATE=abandon,ability,*,*,abandon,ability,abandon,ability,abandon,ability,abandon,ability"

if exist "%BUILD_DIR%\%CONFIG%\recovery_tool.exe" (
  set "EXE=%BUILD_DIR%\%CONFIG%\recovery_tool.exe"
) else (
  set "EXE=%BUILD_DIR%\recovery_tool.exe"
)

"%EXE%" ^
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

:configure_error
echo.
echo CMake configure failed. Most common reason: no C++ compiler toolchain installed.
echo.
echo Fix:
echo   1) Install Visual Studio Build Tools with workload "Desktop development with C++".
echo   2) Run this .bat from "x64 Native Tools Command Prompt for VS".
echo   3) Or set generator manually, for example:
echo      set CMAKE_GENERATOR=MinGW Makefiles

goto :error

:error
echo.
echo Failed with error code %errorlevel%.

:end
pause
endlocal
