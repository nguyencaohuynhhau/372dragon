@echo off
title TLBB Server Manager - WSL2
color 0A
echo ===================================================
echo   TLBB 3.72 Game Server Manager (WSL2)
echo   Dang kiem tra va khoi dong he thong...
echo ===================================================
echo.
wsl -d Ubuntu-22.04 -u havvng -- bash -c "cd ~/372dragon/Server/GameServer && ./start_zone.sh all && echo '' && ./start_zone.sh status"
echo.
echo ===================================================
echo   [OK] MAY CHU DA SAN SANG 100%!
echo   Ban co the mo Play.cmd de vao game ngay bay gio.
echo ===================================================
pause
