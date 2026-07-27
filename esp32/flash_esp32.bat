@echo off
REM ============================================
REM  ESP32 一键强制上传脚本
REM  用法: flash_esp32.bat
REM  前提: ESP32 已通过 USB 连接，COM4 可用
REM ============================================
set MPREMOTE=d:\base-station\.venv\Scripts\mpremote.exe
set COM=COM4
set SRC=d:\base-station\esp32

echo ========================================
echo   ESP32 Force Upload Tool
echo   COM=%COM%  Source=%SRC%
echo ========================================
echo.

REM 先删再传，防止 Up to date 假成功
echo [1/6] Removing old main.py ...
%MPREMOTE% connect %COM% fs rm :main.py 2>nul
echo [2/6] Removing old config.py ...
%MPREMOTE% connect %COM% fs rm :config.py 2>nul
echo [3/6] Uploading config.py ...
%MPREMOTE% connect %COM% fs cp "%SRC%\config.py" :config.py
echo [4/6] Uploading main.py ...
%MPREMOTE% connect %COM% fs cp "%SRC%\main.py" :main.py
echo [5/6] Uploading voice_module.py ...
%MPREMOTE% connect %COM% fs cp "%SRC%\voice_module.py" :voice_module.py
echo [6/6] Resetting ESP32 ...
%MPREMOTE% connect %COM% reset

echo.
echo ========================================
echo   Done! ESP32 is rebooting.
echo   Wait 5s, then test TTL at 9600 baud.
echo ========================================
pause
