@echo off
title TLBB Server - Dung may chu
color 0C
echo ===================================================
echo   Dang dung toan bo he thong TLBB Game Server...
echo ===================================================
wsl -d Ubuntu-22.04 -u havvng -- bash -c "cd ~/372dragon/Server/GameServer && ./start_zone.sh stop"
echo.
echo ===================================================
echo   Da dung tat ca cac tien trinh thanh cong.
echo ===================================================
pause
