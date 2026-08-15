@echo off
echo ============================================
echo  0323 Cloud Disk — One-Click Startup
echo ============================================

REM 1. Start MySQL (adjust path if needed)
echo [1/3] Starting MySQL...
start "" "C:\Program Files\MySQL\MySQL Server 8.0\bin\mysqld.exe"
timeout /t 3 /nobreak >nul

REM 2. Initialize database (first run only)
echo [2/3] Initializing database...
"C:\Program Files\MySQL\MySQL Server 8.0\bin\mysql.exe" -u root -p20041130 < "%~dp0..\scripts\init-db.sql"
echo Database ready.

REM 3. Start server
echo [3/3] Starting server...
start "0323 Cloud Server" "%~dp0..\0323server\release\0323server.exe"

REM Wait for server
timeout /t 2 /nobreak >nul

REM 4. Start client
echo Starting client...
start "0323 Cloud Client" "%~dp0..\0323client\release\0323client.exe"

echo ============================================
echo  All services started!
echo  Server: 127.0.0.1:8899
echo  MySQL: localhost:3306 (root/20041130)
echo ============================================
pause
