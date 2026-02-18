@echo off
REM ========================================
REM CAN Data Logger - Live Dashboard Launcher
REM ========================================
echo.
echo ========================================
echo   CAN Data Logger - Live Dashboard
echo ========================================
echo.

REM Get the directory where this batch file is located
cd /d "%~dp0"

REM Check if Python is installed
python --version >nul 2>&1
if errorlevel 1 (
    echo ERROR: Python is not installed or not in PATH
    echo Please install Python 3.8 or higher
    pause
    exit /b 1
)

REM Check if dashboard script exists
if not exist "Live_Dashboard.py" (
    echo ERROR: Live_Dashboard.py not found in this folder
    echo Make sure this .bat is in the same folder as Live_Dashboard.py
    pause
    exit /b 1
)

echo Launching Live Dashboard...
echo.

REM Launch the dashboard in a new window so we can open the browser
start "Live Dashboard Server" cmd /c "python Live_Dashboard.py"

REM Give the server a moment to start, then open the browser
timeout /t 2 /nobreak >nul
start "" http://127.0.0.1:8600/

echo Dashboard should open in your browser.
echo If it doesn't, open: http://127.0.0.1:8600/
echo.

exit /b 0
