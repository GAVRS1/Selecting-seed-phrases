@echo off
setlocal

set ROOT_DIR=%~dp0

echo Installing npm dependencies for EVM checker...
cd /d "%ROOT_DIR%scripts\evm-checker"
call npm install
if errorlevel 1 (
  echo Failed to install dependencies for EVM checker.
  exit /b 1
)

echo Installing npm dependencies for Solana checker...
cd /d "%ROOT_DIR%scripts\solana-checker"
call npm install
if errorlevel 1 (
  echo Failed to install dependencies for Solana checker.
  exit /b 1
)



echo Installing npm dependencies for Bitcoin checker...
cd /d "%ROOT_DIR%scripts\bitcoin-checker"
call npm install
if errorlevel 1 (
  echo Failed to install dependencies for Bitcoin checker.
  exit /b 1
)

echo Done.
exit /b 0
