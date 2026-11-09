@echo off
rem foo_playback_custom: resolves the toolchain, then hands off to nmake.
rem   build.bat [build ^| clean ^| cleandep ^| fetch], or with no arguments for a menu.
setlocal
cd /d "%~dp0"

set "VSPF=%ProgramFiles(x86)%"
if not defined VSPF set "VSPF=%ProgramFiles%"
set "VSWHERE=%VSPF%\Microsoft Visual Studio\Installer\vswhere.exe"

rem Called by build.nmake; not a user subcommand.
if /i "%~1"=="--fetchdep" goto fetchdep

set "TARGET="
if /i "%~1"=="build" set "TARGET=all"
if /i "%~1"=="clean" set "TARGET=clean"
if /i "%~1"=="cleandep" set "TARGET=cleandep"
if /i "%~1"=="fetch" set "TARGET=fetch"

if not defined TARGET (
    if "%~1"=="" goto menu
    echo Error: unknown subcommand '%~1'
    echo        Usage: build.bat [build ^| clean ^| cleandep ^| fetch]
    exit /b 1
)

rem clean/cleandep/fetch need no toolchain.
if /i "%TARGET%"=="clean" goto nmake
if /i "%TARGET%"=="cleandep" goto nmake
if /i "%TARGET%"=="fetch" goto nmake

if defined VCToolsInstallDir (
    echo   toolchain found in the environment
) else (
    echo   toolchain not in the environment - looking for a Visual Studio Developer Command Prompt
    if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do call "%%i\Common7\Tools\VsDevCmd.bat"
)

if not defined VCToolsInstallDir (
    echo Error: no valid Visual Studio toolchain found
    exit /b 1
)

:nmake
nmake /NOLOGO /f build.nmake %TARGET%
exit /b %errorlevel%


:fetchdep
rem Fetches one pinned dependency (called by build.nmake). Transactional: download to a
rem .part file, verify the hash, extract into a scratch dir, then move the finished tree
rem into place - deps\ never holds a half tree.
set "DEP=%~2"
set "VER=%~3"
set "SHA=%~4"
set "URL=%~5"
set "NAME=%DEP%-%VER%"
set "DEST=deps\%NAME%"
set "PART=deps\%NAME%.7z.part"
set "SCRATCH=deps\_fetch"
rem 7z gives a tarball's inner .tar as one file named after the input (<name>.7z.part),
rem so the second pass cannot go by name. Set outside any parenthesised block, where a
rem plain %VAR% expansion is enough.
set "ISTAR="
echo %URL% | findstr /i /c:".tar.gz" >nul && set "ISTAR=1"
if exist "%DEST%" (
    echo   %NAME% is already fetched
) else (
    echo === fetching %NAME% ===
    if exist "%SCRATCH%" rmdir /s /q "%SCRATCH%"
    if exist "%PART%" del /q "%PART%"
    curl -fsSL -o "%PART%" "%URL%"
    if errorlevel 1 (
        echo Error: could not download %URL%
        if exist "%PART%" del /q "%PART%"
        exit /b 1
    )
    powershell -NoProfile -ExecutionPolicy Bypass -Command "if((Get-FileHash -Algorithm SHA256 '%PART%').Hash -ne '%SHA%'){Write-Host 'Error: %NAME% does not match the pinned SHA-256'; exit 1}"
    if errorlevel 1 (
        if exist "%PART%" del /q "%PART%"
        exit /b 1
    )
    mkdir "%SCRATCH%"
    rem This archive holds its tree at the root, so the scratch dir is the tree.
    7z x -y -o"%SCRATCH%" "%PART%" >nul
    if errorlevel 1 (
        echo Error: could not extract %NAME%
        rmdir /s /q "%SCRATCH%"
        del /q "%PART%"
        exit /b 1
    )
    rem A tarball leaves its inner .tar alone in the scratch dir, so the second pass takes
    rem whatever landed there; the SDK's .7z needs no such pass.
    if defined ISTAR (
        for %%f in ("%SCRATCH%\*") do (
            7z x -y -o"%SCRATCH%" "%%~ff" >nul
            if errorlevel 1 (
                echo Error: could not extract %NAME%
                rmdir /s /q "%SCRATCH%"
                del /q "%PART%"
                exit /b 1
            )
            del /q "%%~ff"
        )
    )
    move "%SCRATCH%" "%DEST%" >nul
    if errorlevel 1 (
        echo Error: could not move the extracted tree into %DEST%
        rmdir /s /q "%SCRATCH%"
        del /q "%PART%"
        exit /b 1
    )
    del /q "%PART%"
)
rem Report - never delete - a differently-versioned tree left beside the pinned one.
for /d %%d in ("deps\%DEP%-*") do if /i not "%%~nxd"=="%NAME%" echo   note: %%d is not the pinned version and is unused
exit /b 0

:menu
cls
echo   1. fetch dependencies
echo   2. build
echo   3. clean
echo   4. clean dependencies
echo   5. quit
set "VERB="
choice /C 12345 /N /M "Choice: "
if errorlevel 255 exit /b 1
if errorlevel 5 exit /b 0
if errorlevel 4 (set "VERB=cleandep" & goto chosen)
if errorlevel 3 (set "VERB=clean" & goto chosen)
if errorlevel 2 (set "VERB=build" & goto chosen)
if errorlevel 1 (set "VERB=fetch" & goto chosen)
if not defined VERB exit /b 1

:chosen
call "%~f0" %VERB%
if errorlevel 1 goto failed
goto menu

:failed
echo.
echo Press any key to continue...
pause >nul
goto menu
