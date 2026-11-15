@echo off
cd /d "%~dp0"
echo ========================================================
echo Packaging StarCraft II Co-op Native Release...
echo ========================================================

python build.py --package
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Packaging failed!
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo [SUCCESS] Clean package generated!
pause
