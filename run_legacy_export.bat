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

set "EXCEL_OUTPUT=%~1"
if "%EXCEL_OUTPUT%"=="" set "EXCEL_OUTPUT=legacy_wallets_export.xlsx"

set "DRY_RUN_FLAG="
if /i "%~2"=="dry-run" set "DRY_RUN_FLAG=--dry-run"

echo Running legacy export + migration for the whole table...
echo Excel output: %EXCEL_OUTPUT%
if defined DRY_RUN_FLAG echo Mode: DRY RUN (no DB changes)

%PYTHON_BIN% scripts\migrate_legacy_wallets_to_split.py --env-file .env --legacy-table recovered_wallets --excel-output "%EXCEL_OUTPUT%" %DRY_RUN_FLAG%
if errorlevel 1 goto :error

echo.
echo Legacy export finished successfully.
goto :end

:error
echo.
echo Failed with error code %errorlevel%.

:end
pause
endlocal
