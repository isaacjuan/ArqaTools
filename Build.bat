@echo off
REM Build script for HelloWorld AutoCAD 2025 plugin
REM This script sets up the Visual Studio environment and builds the project

echo ==========================================
echo Building HelloWorld AutoCAD 2025 Plugin
echo ==========================================
echo.

REM Setup Visual Studio Developer Command Prompt environment
REM Skip if already initialized (VSCMD_VER is set by VsDevCmd.bat).
REM Calling it repeatedly in the same session grows PATH until Windows
REM hits the 8191-character limit ("input line too long").
if not defined VSCMD_VER (
    call "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"
    if errorlevel 1 (
        echo ERROR: Could not setup Visual Studio environment
        pause
        exit /b 1
    )
)

echo.
echo Building project...
echo.

REM Build the solution (use /t:Rebuild to force full rebuild and update timestamps)
msbuild HelloWorld.sln /t:Rebuild /p:Configuration="Debug 2025" /p:Platform=x64 /v:m

if errorlevel 1 (
    echo.
    echo ERROR: Build failed!
    pause
    exit /b 1
)

echo.
echo ==========================================
echo Build completed successfully!
echo ==========================================
echo.
echo Copying HelloWorld.arx to deployment locations...

REM Copy the compiled plugin to Documents folder (might fail if locked)
copy /Y "x64\Debug 2025\HelloWorld.arx" "%USERPROFILE%\Documents\"
if errorlevel 1 (
    echo WARNING: Could not copy to Documents folder (file may be locked)
) else (
    echo         1 file(s) copied.
)

REM Copy to acadPlugins folder if it exists, create if not
if not exist "%USERPROFILE%\Documents\acadPlugins\" (
    mkdir "%USERPROFILE%\Documents\acadPlugins"
)

copy /Y "x64\Debug 2025\HelloWorld.arx" "%USERPROFILE%\Documents\acadPlugins\"
if errorlevel 1 (
    echo WARNING: Could not copy to acadPlugins folder (file may be locked)
) else (
    echo         1 file(s) copied.
)

REM Copy LSP file to both locations
echo.
echo Copying ReloadHelloWorld.lsp...
copy /Y "ReloadHelloWorld.lsp" "%USERPROFILE%\Documents\"
copy /Y "ReloadHelloWorld.lsp" "%USERPROFILE%\Documents\acadPlugins\"

echo.
echo Plugin copied to: %USERPROFILE%\Documents\acadPlugins\HelloWorld.arx
echo.
echo Plugin copied to: %USERPROFILE%\Documents\HelloWorld.arx
echo.
echo LSP copied to both locations
echo.
echo You can now load it in AutoCAD with: NETLOAD
echo Or use APPLOAD to add ReloadHelloWorld.lsp to Startup Suite
echo Then use RELOADHW command for quick reloading
echo.
pause
