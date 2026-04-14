@echo off
setlocal EnableExtensions EnableDelayedExpansion

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

set "BATCH_SIZE=%~1"
if "%BATCH_SIZE%"=="" set "BATCH_SIZE=1000"

set "EXCEL_OUTPUT=%~2"
if "%EXCEL_OUTPUT%"=="" set "EXCEL_OUTPUT=split_wallets_export.xlsx"

set "DRY_RUN_FLAG="
if /i "%~3"=="dry-run" set "DRY_RUN_FLAG=--dry-run"

echo Running split export to Excel + cleanup...
echo Batch size: %BATCH_SIZE%
echo Excel output: %EXCEL_OUTPUT%
if defined DRY_RUN_FLAG echo Mode: DRY RUN (no DB changes)

%PYTHON_BIN% scripts\export_split_recovered_wallets.py --env-file .env --table-btc recovered_wallets_btc --table-evm recovered_wallets_evm --table-sol recovered_wallets_sol --batch-size %BATCH_SIZE% --excel-output "%EXCEL_OUTPUT%" %DRY_RUN_FLAG%
if errorlevel 1 goto :error

echo.
echo Split export and cleanup finished successfully.
goto :end

:error
echo.
echo Failed with error code %errorlevel%.

:end
pause
endlocal
