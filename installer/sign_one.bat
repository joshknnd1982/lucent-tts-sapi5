@echo off
rem Signs a single file.  This is the shim Inno Setup's SignTool= directive invokes, once
rem per file it needs signed (Setup.exe and the generated uninstaller).  It forwards to
rem sign.ps1, which is a no-op when no certificate is configured.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0sign.ps1" %*
exit /b %errorlevel%
