@echo off
title Minecraft Fabric Server 1.21.7
echo Minecraft sunucusu baslatiliyor...

java -Xms256M -Xmx3G -XX:+UseG1GC -jar fabric-server-mc.1.21.7-loader.0.16.9-launcher.1.0.1.jar nogui

echo.
echo Sunucu kapandi!
pause