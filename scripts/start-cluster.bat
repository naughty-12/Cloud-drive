@echo off
echo ============================================
echo  0323 Cloud Disk — Cluster Mode (2 Nodes)
echo ============================================
echo.

REM Ensure storage directories exist
if not exist "C:\disk1\" mkdir "C:\disk1"
if not exist "C:\disk2\" mkdir "C:\disk2"

REM Config files must be in the same directory as the .exe
set RELEASE_DIR=..\0323server\release
if not exist "%RELEASE_DIR%\0323server.exe" (
    echo ERROR: 0323server.exe not found in %RELEASE_DIR%
    echo Please build the server first (cd 0323server ^&^& qmake ^&^& mingw32-make)
    pause
    exit /b 1
)

REM Copy configs to release dir if needed
if not exist "%RELEASE_DIR%\server.conf" (
    copy ..\0323server\server.conf "%RELEASE_DIR%\server.conf" >nul
)
if not exist "%RELEASE_DIR%\server_node_b.conf" (
    copy ..\0323server\server_node_b.conf "%RELEASE_DIR%\server_node_b.conf" >nul
)

cd /d "%RELEASE_DIR%"

echo [1/2] Starting Node A (127.0.0.1:8899, HTTP :8900, Storage: C:\disk1\) ...
start "0323Server-NodeA" 0323server.exe --config server.conf

REM Give Node A a moment to bind ports and warm up
timeout /t 2 /nobreak >nul

echo [2/2] Starting Node B (127.0.0.1:8898, HTTP :8901, Storage: C:\disk2\) ...
start "0323Server-NodeB" 0323server.exe --config server_node_b.conf

echo.
echo ============================================
echo  Cluster is running!
echo ============================================
echo   Node A: TCP 8899  HTTP 8900
echo   Node B: TCP 8898  HTTP 8901
echo.
echo   Now start the client in a separate terminal:
echo     cd 0323client\release ^&^& 0323client.exe
echo ============================================
echo.
echo Press any key in this window to kill both server nodes...
pause >nul

echo Stopping servers...
taskkill /FI "WINDOWTITLE eq 0323Server-NodeA" /T /F 2>nul
taskkill /FI "WINDOWTITLE eq 0323Server-NodeB" /T /F 2>nul
echo Done.
pause
