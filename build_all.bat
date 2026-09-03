@echo off
setlocal
rem Lucent TTS SAPI 5 wrapper - full build: x86 + x64 DLLs, config utility, tests, installer.
rem Executables are invoked by absolute path because this machine sets
rem NoDefaultCurrentDirectoryInExePath.

set ROOT=%~dp0
set BUILD_DIR_X86=%ROOT%build_x86
set BUILD_DIR_X64=%ROOT%build_x64
set OUTPUT_DIR=%ROOT%output

if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"
if not exist "%OUTPUT_DIR%\x64" mkdir "%OUTPUT_DIR%\x64"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: vswhere.exe not found. Install Visual Studio 2022 Build Tools.
    exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSINSTALLDIR=%%i\"
if not defined VSINSTALLDIR (
    echo ERROR: Visual Studio installation not found.
    exit /b 1
)
echo Using Visual Studio at %VSINSTALLDIR%

echo.
echo === x86 build ===
call "%VSINSTALLDIR%VC\Auxiliary\Build\vcvarsall.bat" x86 >nul
if errorlevel 1 exit /b 1
cmake -A Win32 -S "%ROOT%." -B "%BUILD_DIR_X86%"
if errorlevel 1 exit /b 1
cmake --build "%BUILD_DIR_X86%" --config Release
if errorlevel 1 exit /b 1

echo.
echo === x64 build ===
call "%VSINSTALLDIR%VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
if errorlevel 1 exit /b 1
cmake -A x64 -S "%ROOT%." -B "%BUILD_DIR_X64%"
if errorlevel 1 exit /b 1
cmake --build "%BUILD_DIR_X64%" --config Release
if errorlevel 1 exit /b 1

echo.
echo === staging ===
copy /Y "%BUILD_DIR_X86%\bin\Release\LucentSAPI.dll" "%OUTPUT_DIR%\" >nul
copy /Y "%BUILD_DIR_X86%\bin\Release\LucentConfig.exe" "%OUTPUT_DIR%\" >nul
copy /Y "%BUILD_DIR_X64%\bin\Release\LucentSAPI.dll" "%OUTPUT_DIR%\x64\" >nul
if not exist "%ROOT%bin\engine\ttsserver.exe" (
    echo ERROR: bin\engine\ttsserver.exe is missing - run installer\stage_engine.ps1 first.
    exit /b 1
)

echo.
echo === engine self-test (x64 client) ===
"%BUILD_DIR_X64%\bin\Release\engine_test.exe" "%ROOT%bin\engine" "%OUTPUT_DIR%\test_x64"
if errorlevel 1 (
    echo ERROR: engine_test x64 failed.
    exit /b 1
)
echo === engine self-test (x86 client) ===
"%BUILD_DIR_X86%\bin\Release\engine_test.exe" "%ROOT%bin\engine" "%OUTPUT_DIR%\test_x86" John
if errorlevel 1 (
    echo ERROR: engine_test x86 failed.
    exit /b 1
)

echo.
echo === signing payload binaries ===
rem No certificate configured means sign.ps1 prints a notice and succeeds, so an
rem unsigned build still completes.  See installer\sign.ps1 for the env vars.
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%installer\sign.ps1" "%OUTPUT_DIR%\LucentSAPI.dll" "%OUTPUT_DIR%\x64\LucentSAPI.dll" "%OUTPUT_DIR%\LucentConfig.exe"
if errorlevel 1 (
    echo ERROR: signing failed.
    exit /b 1
)

echo.
echo === installer ===
set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" (
    echo WARNING: ISCC.exe not found; installer not built.
    goto :done
)
rem $q is Inno's escape for a double quote inside a SignTool command line.
set "SIGNARG="
if defined LUCENT_SIGN_THUMBPRINT goto :signon
if defined LUCENT_SIGN_PFX goto :signon
goto :signoff
:signon
set SIGNARG=/DSIGN "/Slucentsign=$q%ROOT%installer\sign_one.bat$q $q$f$q"
:signoff
"%ISCC%" /Q %SIGNARG% "/O%OUTPUT_DIR%" "%ROOT%installer\lucent.iss"
if errorlevel 1 (
    echo ERROR: installer build failed.
    exit /b 1
)

echo.
echo === shipped file identity ===
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%installer\verify_metadata.ps1" "%OUTPUT_DIR%"
if errorlevel 1 (
    echo ERROR: shipped binaries are missing version metadata.
    exit /b 1
)

:done
echo.
echo Build completed. Output in %OUTPUT_DIR%
endlocal
