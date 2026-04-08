@echo off
REM Copy HelloWorld.arx and ReloadHelloWorld.lsp to Documents folders

set SOURCE_ARX="c:\HSBCAD\HelloWorldAcad2025\x64\Debug 2025\HelloWorld.arx"
set SOURCE_LSP="c:\HSBCAD\HelloWorldAcad2025\ReloadHelloWorld.lsp"
set DEST_ACADPLUGINS="%USERPROFILE%\Documents\acadplugins"
set DEST_DOCS="%USERPROFILE%\Documents"

echo ==========================================
echo Deploying HelloWorld Plugin Files
echo ==========================================
echo.

REM Create acadplugins directory if it doesn't exist
if not exist %DEST_ACADPLUGINS% (
    echo Creating acadplugins directory...
    mkdir %DEST_ACADPLUGINS%
)

REM Copy ARX to acadplugins
echo Copying HelloWorld.arx to acadplugins...
copy /Y %SOURCE_ARX% "%DEST_ACADPLUGINS%\HelloWorld.arx"

if %ERRORLEVEL% EQU 0 (
    echo SUCCESS: ARX copied to acadplugins
) else (
    echo ERROR: Failed to copy ARX to acadplugins
    echo The file may be in use by AutoCAD.
)

echo.

REM Copy ARX to Documents
echo Copying HelloWorld.arx to Documents...
copy /Y %SOURCE_ARX% "%DEST_DOCS%\HelloWorld.arx"

if %ERRORLEVEL% EQU 0 (
    echo SUCCESS: ARX copied to Documents
) else (
    echo ERROR: Failed to copy ARX to Documents
)

echo.

REM Copy LSP to both locations
echo Copying ReloadHelloWorld.lsp to acadplugins...
copy /Y %SOURCE_LSP% "%DEST_ACADPLUGINS%\ReloadHelloWorld.lsp"

if %ERRORLEVEL% EQU 0 (
    echo SUCCESS: LSP copied to acadplugins
) else (
    echo ERROR: Failed to copy LSP to acadplugins
)

echo.

echo Copying ReloadHelloWorld.lsp to Documents...
copy /Y %SOURCE_LSP% "%DEST_DOCS%\ReloadHelloWorld.lsp"

if %ERRORLEVEL% EQU 0 (
    echo SUCCESS: LSP copied to Documents
) else (
    echo ERROR: Failed to copy LSP to Documents
)

echo.
echo ==========================================
echo Deployment Complete
echo ==========================================
echo.
echo Files deployed to:
echo   %DEST_ACADPLUGINS%
echo   %DEST_DOCS%
echo.
echo To use the reload command:
echo   1. Type APPLOAD in AutoCAD
echo   2. Add ReloadHelloWorld.lsp to Startup Suite
echo   3. Use RELOADHW command to reload the plugin
echo.
pause
