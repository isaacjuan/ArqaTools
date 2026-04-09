@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"
if errorlevel 1 exit /b 1
msbuild ArqaTools.sln /t:Rebuild /p:Configuration="Release 2025" /p:Platform=x64 /v:m
if errorlevel 1 exit /b 1
echo Build Verification Successful
