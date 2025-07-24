@echo off
title Minecraft PigeonMC Server 1.20.1
echo Starting Minecraft server...

java -Xms256M -Xmx3G -XX:+UseG1GC -jar pigeon-server-mc.1.20.1.jar nogui

echo.
echo The server is down!
pause
