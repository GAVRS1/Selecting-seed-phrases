@echo off
setlocal EnableExtensions

REM Run from repository root.
cd /d "%~dp0"

if not exist ".env" (
  echo Missing .env file. Copy .env.example to .env and set RECOVERY_POSTGRES_CONN.
  goto :error
)

set "CHECKER_DELAY_SECONDS=%RECOVERY_BALANCE_CHECKER_DELAY_SECONDS%"
if "%CHECKER_DELAY_SECONDS%"=="" set "CHECKER_DELAY_SECONDS=0.2"

set "PYTHON_BIN=python"
where py >nul 2>&1
if not errorlevel 1 (
  set "PYTHON_BIN=py -3"
)

start "Python balance checker" cmd /k "%PYTHON_BIN% scripts\check_wallet_balances.py --env-file .env --output recovered_wallets.txt --delay-seconds %CHECKER_DELAY_SECONDS%"

echo Python balance checker started in a separate console.
echo Delay between wallet checks: %CHECKER_DELAY_SECONDS% sec
goto :end

:error
echo.
echo Failed with error code %errorlevel%.

:end
pause
endlocal
