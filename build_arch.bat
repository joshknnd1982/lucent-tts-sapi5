@echo off
rem build_arch.bat x86|x64 [target] - configure and build one architecture with MSVC.
setlocal
set ARCH=%1
if "%ARCH%"=="" set ARCH=x64
set TARGET=%2
set ROOT=%~dp0
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSINSTALLDIR=%%i\"
if not defined VSINSTALLDIR (
    echo ERROR: Visual Studio installation not found.
    exit /b 1
)
call "%VSINSTALLDIR%VC\Auxiliary\Build\vcvarsall.bat" %ARCH% >nul
if errorlevel 1 exit /b 1
if "%ARCH%"=="x86" (set GEN=Win32) else (set GEN=x64)
cmake -A %GEN% -S "%ROOT%." -B "%ROOT%build_%ARCH%"
if errorlevel 1 exit /b 1
if "%TARGET%"=="" (
    cmake --build "%ROOT%build_%ARCH%" --config Release
) else (
    cmake --build "%ROOT%build_%ARCH%" --config Release --target %TARGET%
)
exit /b %errorlevel%
