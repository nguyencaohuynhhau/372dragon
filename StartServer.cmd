@echo off
title TLBB Server - WSL2 [Dang chay]
color 0A
echo ===================================================
echo   TLBB 3.72 Game Server Manager (WSL2)
echo   Dang kiem tra va khoi dong toan bo he thong...
echo ===================================================
echo.
wsl -d Ubuntu-22.04 -u havvng -- bash -c "cd ~/372dragon/Server/GameServer && ./start_zone.sh run"
pause
