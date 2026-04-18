@echo off
setlocal EnableExtensions

REM Run from repository root.
cd /d "%~dp0"

if not exist ".env" (
  echo Missing .env file. Copy .env.example to .env and set RECOVERY_POSTGRES_CONN.
  call :set_error 1
  goto :error
)

set "RECOVERY_POSTGRES_CONN="
for /f "usebackq tokens=1,* delims==" %%A in (".env") do (
  if /I "%%~A"=="RECOVERY_POSTGRES_CONN" set "RECOVERY_POSTGRES_CONN=%%~B"
  if /I "%%~A"=="POSTGRES_CONN" if not defined RECOVERY_POSTGRES_CONN set "RECOVERY_POSTGRES_CONN=%%~B"
)

if not defined RECOVERY_POSTGRES_CONN (
  echo Missing RECOVERY_POSTGRES_CONN/POSTGRES_CONN in .env.
  call :set_error 1
  goto :error
)

start "BTC checker" cmd /k "set POSTGRES_CONN=%RECOVERY_POSTGRES_CONN%&& npm --prefix scripts\bitcoin-checker start -- all"
start "EVM all-networks checker" cmd /k "set POSTGRES_CONN=%RECOVERY_POSTGRES_CONN%&& npm --prefix scripts\evm-checker start -- all"
start "SOL checker" cmd /k "set POSTGRES_CONN=%RECOVERY_POSTGRES_CONN%&& npm --prefix scripts\solana-checker start -- all"

echo Started 3 separate checker consoles:
echo   - BTC RPC checker
echo   - EVM all-networks checker
echo   - SOL all-networks checker
echo BTC checker uses RPC URLs from .env ^(BTC_RPC_URL/BTC_RPC_URLS or RECOVERY_BTC_RPC_URL/RECOVERY_BTC_RPC_URLS^).
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
