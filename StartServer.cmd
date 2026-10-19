@echo off
title TLBB Server Manager - WSL2
echo ===================================================
echo   Dang khoi dong TLBB Game Server tren WSL2...
echo ===================================================
wsl -d Ubuntu-22.04 -u havvng -- bash -c "cd ~/372dragon/Server/GameServer && ./start_zone.sh all"
echo.
echo Dang kiem tra trang thai sau khi khoi dong:
wsl -d Ubuntu-22.04 -u havvng -- bash -c "cd ~/372dragon/Server/GameServer && ./start_zone.sh status"
echo.
echo ===================================================
echo   Server da san sang! Ban co the mo Play.cmd
echo ===================================================
pause
