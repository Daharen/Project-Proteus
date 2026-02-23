@echo off
setlocal
set SCRIPT_DIR=%~dp0
"%SCRIPT_DIR%proteus.exe" --serve --host 127.0.0.1 --port 8080 --db "%SCRIPT_DIR%proteus.db" --static_dir "%SCRIPT_DIR%web" --dev
endlocal
