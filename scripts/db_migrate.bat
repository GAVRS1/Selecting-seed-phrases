@echo off
setlocal EnableExtensions EnableDelayedExpansion
set "RETCODE=0"

cd /d "%~dp0.."

set "ENV_FILE=%~1"
if "%ENV_FILE%"=="" set "ENV_FILE=.env"

if not defined MIGRATIONS_DIR set "MIGRATIONS_DIR=migrations"

if not exist "%MIGRATIONS_DIR%" (
  echo Migrations directory not found: %MIGRATIONS_DIR%
  set "RETCODE=1"
  goto :error
)

if exist "%ENV_FILE%" (
  for /f "usebackq tokens=1* delims==" %%A in (`findstr /r /b /c:"[ ]*RECOVERY_POSTGRES_CONN[ ]*=" "%ENV_FILE%"`) do (
    set "RECOVERY_POSTGRES_CONN=%%B"
  )
  if defined RECOVERY_POSTGRES_CONN (
    for /f "tokens=* delims= " %%V in ("!RECOVERY_POSTGRES_CONN!") do set "RECOVERY_POSTGRES_CONN=%%V"
  )
) else (
  echo Env file not found: %ENV_FILE%
)

if not defined RECOVERY_POSTGRES_CONN (
  echo RECOVERY_POSTGRES_CONN is empty. Set it in %ENV_FILE% or in the environment.
  set "RETCODE=1"
  goto :error
)

if "!RECOVERY_POSTGRES_CONN:~0,1!"=="\"" if "!RECOVERY_POSTGRES_CONN:~-1!"=="\"" (
  set "RECOVERY_POSTGRES_CONN=!RECOVERY_POSTGRES_CONN:~1,-1!"
)

where psql >nul 2>&1
if errorlevel 1 (
  echo psql is not installed or not available in PATH.
  echo Install PostgreSQL client tools and make sure psql.exe is accessible from CMD.
  set "RETCODE=9009"
  goto :error
)

echo Ensuring schema_migrations table exists...
psql "!RECOVERY_POSTGRES_CONN!" -v ON_ERROR_STOP=1 -q -c "CREATE TABLE IF NOT EXISTS schema_migrations (filename TEXT PRIMARY KEY, applied_at TIMESTAMPTZ NOT NULL DEFAULT NOW());"
if errorlevel 1 (set "RETCODE=!errorlevel!" & goto :error)

for %%F in ("%MIGRATIONS_DIR%\*.sql") do (
  set "MIGRATION_FILE=%%~fF"
  set "MIGRATION_NAME=%%~nxF"

  for /f "usebackq delims=" %%R in (`psql "!RECOVERY_POSTGRES_CONN!" -v ON_ERROR_STOP=1 -tA -c "SELECT 1 FROM schema_migrations WHERE filename = '!MIGRATION_NAME!' LIMIT 1;"`) do (
    set "ALREADY_APPLIED=%%R"
  )

  if "!ALREADY_APPLIED!"=="1" (
    echo skip !MIGRATION_NAME!
  ) else (
    echo apply !MIGRATION_NAME!
    psql "!RECOVERY_POSTGRES_CONN!" -v ON_ERROR_STOP=1 -f "!MIGRATION_FILE!"
    if errorlevel 1 (set "RETCODE=!errorlevel!" & goto :error)

    psql "!RECOVERY_POSTGRES_CONN!" -v ON_ERROR_STOP=1 -q -c "INSERT INTO schema_migrations(filename) VALUES ('!MIGRATION_NAME!');"
    if errorlevel 1 (set "RETCODE=!errorlevel!" & goto :error)
  )

  set "ALREADY_APPLIED="
)

echo Migrations are up to date.
goto :end

:error
echo.
if "%RETCODE%"=="0" set "RETCODE=!errorlevel!"
echo Failed with error code %RETCODE%.

:end
pause
endlocal & exit /b %RETCODE%
