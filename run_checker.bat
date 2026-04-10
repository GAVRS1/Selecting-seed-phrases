@echo off
setlocal EnableExtensions

REM Run from repository root.
cd /d "%~dp0"

if not exist ".env" (
  echo Missing .env file. Copy .env.example to .env and set RECOVERY_POSTGRES_CONN.
  goto :error
)

set "PYTHON_BIN=python"
where py >nul 2>&1
if not errorlevel 1 (
  set "PYTHON_BIN=py -3"
)

start "Python balance checker" cmd /k "%PYTHON_BIN% scripts\check_wallet_balances.py --env-file .env --output recovered_wallets.txt --delay-seconds 0.2"

echo Python balance checker started in a separate console.
goto :end

:error
echo.
echo Failed with error code %errorlevel%.

:end
pause
endlocal
