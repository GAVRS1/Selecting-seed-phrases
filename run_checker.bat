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

start "BTC balance checker" cmd /k "%PYTHON_BIN% scripts\check_wallet_balances.py --env-file .env --chain btc --output recovered_wallets.txt"
start "ETH balance checker" cmd /k "%PYTHON_BIN% scripts\check_wallet_balances.py --env-file .env --chain eth --output recovered_wallets.txt"
start "SOL balance checker" cmd /k "%PYTHON_BIN% scripts\check_wallet_balances.py --env-file .env --chain sol --output recovered_wallets.txt"

echo Started 3 separate checker consoles:
echo   - BTC table checker
echo   - ETH ^(EVM^) table checker
echo   - SOL table checker
echo Delay between wallet checks is resolved by checker from .env/env ^(RECOVERY_BALANCE_CHECKER_DELAY_SECONDS^) or default 0.2 sec.
goto :end

:error
echo.
echo Failed with error code %errorlevel%.

:end
pause
endlocal
