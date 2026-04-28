@echo off
REM Build the UT99 Xbox port without launching Visual Studio.
REM Uses C:\XDK\xbox\bin\vc71 tools directly via UT99-Xbox\Tools\build_xbox_cli.py.

setlocal
set SCRIPT_DIR=%~dp0

if "%1"=="clean" (
    python "%SCRIPT_DIR%UT99-Xbox\Tools\build_xbox_cli.py" --clean
    exit /b %ERRORLEVEL%
)

if "%1"=="hardware" (
    python "%SCRIPT_DIR%UT99-Xbox\Tools\build_xbox_cli.py" --out-dir "%SCRIPT_DIR%UT99-Xbox\build\release"
    exit /b %ERRORLEVEL%
)

python "%SCRIPT_DIR%UT99-Xbox\Tools\build_xbox_cli.py" %*
exit /b %ERRORLEVEL%
