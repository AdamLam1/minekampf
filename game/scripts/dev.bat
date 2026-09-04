@echo off
REM Minekampf dev tool: build / run / test / shot
REM Usage: dev.bat build | dev.bat run [args...] | dev.bat test | dev.bat shot <out.bmp> [ticks]
setlocal enabledelayedexpansion
set VSVC=%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat
set GAME_DIR=%~dp0..
set BIN=%GAME_DIR%\build\release\bin
set LOG=%TEMP%\minekampf_build.log

if /i "%1"=="build" goto :build
if /i "%1"=="test" goto :test
if /i "%1"=="run" goto :run
if /i "%1"=="shot" goto :shot
if /i "%1"=="runbg" goto :runbg
if /i "%1"=="kill" goto :kill
if /i "%1"=="ai" goto :ai
echo Usage: dev.bat build ^| test ^| run [args...] ^| shot out.bmp [ticks] ^| runbg [args...] ^| kill ^| ai [ai_shot args...]
exit /b 1

:runbg
shift
call :checkbuild
start "" "%BIN%\minekampf.exe" %1 %2 %3 %4 %5 %6 %7 %8 %9
exit /b 0

:kill
taskkill /f /im minekampf.exe >nul 2>&1
echo KILLED
exit /b 0

:ai
shift
python "%GAME_DIR%\scripts\ai_shot.py" %1 %2 %3 %4 %5 %6 %7 %8 %9
exit /b %ERRORLEVEL%

:build
call "%VSVC%" >nul || exit /b 1
pushd "%GAME_DIR%"
cmake --build --preset release > "%LOG%" 2>&1
set RC=%ERRORLEVEL%
findstr /i /c:"error" /c:"FAILED" "%LOG%"
popd
if "%RC%"=="0" (echo BUILD OK) else (echo BUILD FAILED - full log: %LOG%)
exit /b %RC%

:test
"%BIN%\minekampf_tests.exe" 2>&1 | findstr /i /c:"tests ran" /c:"PASSED" /c:"FAILED"
exit /b %ERRORLEVEL%

:run
shift
call :checkbuild
"%BIN%\minekampf.exe" %1 %2 %3 %4 %5 %6 %7 %8 %9
exit /b %ERRORLEVEL%

:shot
shift
set OUT=%~1
set TICKS=%~2
if "%OUT%"=="" (echo Usage: dev.bat shot out.bmp [ticks] & exit /b 1)
if "%TICKS%"=="" set TICKS=100
call :checkbuild
"%BIN%\minekampf.exe" --screenshot "%OUT%" --ticks %TICKS%
if errorlevel 1 exit /b 1
python -c "from PIL import Image; Image.open(r'%OUT%').save(r'%OUT:.bmp=.png%')" || exit /b 1
echo SHOT OK: %OUT:.bmp=.png%
exit /b 0

:checkbuild
if not exist "%BIN%\minekampf.exe" (
    echo No binary - building first...
    call %~f0 build
)
exit /b 0
