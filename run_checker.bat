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

set "PYTHON_BIN=python"
where py >nul 2>&1
if not errorlevel 1 (
  set "PYTHON_BIN=py -3"
)

start "BTC balance checker" cmd /k "%PYTHON_BIN% scripts\check_wallet_balances.py --env-file .env --chain btc --output recovered_wallets.txt"
start "EVM all-networks checker" cmd /k "set POSTGRES_CONN=%RECOVERY_POSTGRES_CONN%&& npm --prefix scripts\evm-checker start -- all"
start "SOL checker" cmd /k "set POSTGRES_CONN=%RECOVERY_POSTGRES_CONN%&& npm --prefix scripts\solana-checker start -- all"

echo Started 3 separate checker consoles:
echo   - BTC table checker
echo   - EVM all-networks checker
echo   - SOL all-networks checker
echo Delay between wallet checks is resolved by checker from .env/env ^(RECOVERY_BALANCE_CHECKER_DELAY_SECONDS^) or default 0.2 sec.
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
