@echo off
setlocal

REM Run from repository root.
cd /d "%~dp0"
echo run_mass_btc_eth.bat is deprecated.
echo Use run_project.bat and set RECOVERY_ENABLE_* / RECOVERY_CONSOLES_* in .env.
call run_project.bat
endlocal
