@echo off
rem Compila o Birdstation e gera dist\Birdstation-<versao>-win64.zip
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\package.ps1" %*
pause
