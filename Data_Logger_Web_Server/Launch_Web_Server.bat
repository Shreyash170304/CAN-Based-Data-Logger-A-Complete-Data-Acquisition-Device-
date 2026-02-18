@echo off
REM ========================================
REM CAN Data Logger - Web Server Launcher
REM ========================================
echo.
echo ========================================
echo   CAN Data Logger - Web Server
echo ========================================
echo.

REM Web server IP address
set SERVER_IP=192.168.10.1
set SERVER_URL=http://%SERVER_IP%

echo Checking WiFi connection...
echo.

REM Check if connected to CAN_Data_Logger network
netsh wlan show interfaces | findstr /C:"CAN_Data_Logger" >nul
if %errorlevel% equ 0 (
    echo [OK] Connected to CAN_Data_Logger network
) else (
    echo [WARNING] Not connected to CAN_Data_Logger network
    echo.
    echo Please connect to WiFi network: CAN_Data_Logger
    echo Password: CANDataLogger123
    echo.
    pause
    exit /b 1
)

echo.
echo Checking server availability...
ping -n 1 %SERVER_IP% >nul 2>&1
if %errorlevel% equ 0 (
    echo [OK] Server is reachable at %SERVER_IP%
) else (
    echo [WARNING] Server may not be ready yet
    echo Attempting to connect anyway...
)

echo.
echo Opening web server in Microsoft Edge...
echo URL: %SERVER_URL%
echo.

REM Try to open in Microsoft Edge
start msedge.exe %SERVER_URL%

if %errorlevel% neq 0 (
    echo [ERROR] Failed to open Microsoft Edge
    echo Trying default browser...
    start %SERVER_URL%
)

echo.
echo Web server should open in your browser.
echo If it doesn't, manually navigate to: %SERVER_URL%
echo.
pause
