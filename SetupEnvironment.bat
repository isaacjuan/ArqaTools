@echo off
REM SetupEnvironment.bat - Configure ObjectARX environment variables for ArqaTools project
REM This matches the beamapp project structure

echo ========================================
echo ObjectARX Environment Setup for AutoCAD 2025
echo ========================================
echo.

REM Check if OARX2025 is already set
if defined OARX2025 (
    echo OARX2025 is already set to: %OARX2025%
    echo.
) else (
    echo OARX2025 environment variable is NOT set.
    echo.
    echo Setting default path...
    setx OARX2025 "C:\ObjectARX_for_AutoCAD_2025_Win_64bit"
    set OARX2025=C:\ObjectARX_for_AutoCAD_2025_Win_64bit
    echo OARX2025 set to: %OARX2025%
    echo.
)

REM Verify the path exists
if exist "%OARX2025%\inc" (
    echo [OK] ObjectARX SDK found at: %OARX2025%
) else (
    echo [ERROR] ObjectARX SDK not found at: %OARX2025%
    echo.
    echo Please install ObjectARX SDK or update the path in this script.
    echo You can download it from Autodesk Developer Network.
    pause
    exit /b 1
)

echo.
echo ========================================
echo Environment Setup Complete!
echo ========================================
echo.
echo You can now build the ArqaTools project in Visual Studio.
echo.
echo Required Environment Variables:
echo   OARX2025 = %OARX2025%
echo.
echo NOTE: You may need to restart Visual Studio for changes to take effect.
echo.
pause
