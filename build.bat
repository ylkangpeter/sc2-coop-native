@echo off
cd /d "%~dp0"
echo ========================================================
echo Building StarCraft II Co-op Native (Windows Native)...
echo ========================================================

python build.py
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Build failed!
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo [SUCCESS] Build completed successfully!
pause
