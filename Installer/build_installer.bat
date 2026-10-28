@echo off
setlocal

rem ---------------------------------------------------------------------------
rem  Builds the KickDucker installer.
rem  Usage:   build_installer.bat [path-to-folder-containing-KickDucker.vst3]
rem  With no argument it uses the default set in KickDucker.iss
rem  (Builds\VisualStudio2022\x64\Release\VST3 relative to the project root).
rem ---------------------------------------------------------------------------

cd /d "%~dp0"

rem --- Work out which folder holds KickDucker.vst3 ---
set "BUILD_DIR=%~1"
if "%BUILD_DIR%"=="" set "BUILD_DIR=..\Builds\VisualStudio2022\x64\Release\VST3"

if not exist "%BUILD_DIR%\KickDucker.vst3" (
    echo ERROR: could not find "%BUILD_DIR%\KickDucker.vst3"
    echo Build the Release x64 configuration in Visual Studio first, or pass the folder as an argument.
    exit /b 1
)

rem --- Find the Inno Setup compiler ---
set "ISCC="
if exist "%ProgramFiles%\Inno Setup 7\ISCC.exe" set "ISCC=%ProgramFiles%\Inno Setup 7\ISCC.exe"
if not defined ISCC if exist "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not defined ISCC if exist "%ProgramFiles%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if not defined ISCC if exist "%LocalAppData%\Programs\Inno Setup 6\ISCC.exe" set "ISCC=%LocalAppData%\Programs\Inno Setup 6\ISCC.exe"

if not defined ISCC (
    echo ERROR: Inno Setup compiler not found.
    echo Install Inno Setup 6.3 or newer from https://jrsoftware.org/isdl.php and run this again.
    exit /b 1
)

echo Using: %ISCC%
echo Packaging: %BUILD_DIR%\KickDucker.vst3
echo.

"%ISCC%" "/DBuildDir=%BUILD_DIR%" KickDucker.iss
if errorlevel 1 (
    echo.
    echo ERROR: Inno Setup reported a problem - see the messages above.
    exit /b 1
)

echo.
echo ===== Done. SHA-256 of the installer(s) in installer\output : =====
for %%F in ("output\*.exe") do certutil -hashfile "%%F" SHA256

endlocal