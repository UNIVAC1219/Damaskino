@echo off
setlocal EnableDelayedExpansion
REM ============================================================================
REM  Damaskino - Windows build (native).  NO Visual Studio dev prompt required.
REM
REM  Usage (from any normal Command Prompt or PowerShell, or just double-click):
REM      build.bat            Build build\damaskino.exe (full engine)
REM      build.bat univac     Build build\damaskino_univac.exe (UNIVAC-1219B lite)
REM      build.bat test       Build and run the test suites
REM      build.bat run        Build, then run the Washington D.C. example
REM      build.bat clean      Remove the build folder
REM
REM  Picks a compiler automatically, in this order:
REM      1. cl   (MSVC - located via vswhere and set up here, no dev prompt)
REM      2. clang
REM      3. gcc  (MinGW-w64)
REM ============================================================================

cd /d "%~dp0"
set "TARGET=%~1"
if "%TARGET%"=="" set "TARGET=full"

if /I "%TARGET%"=="clean" (
    if exist build rmdir /s /q build
    echo Removed build\.
    goto :eof
)

if not exist build mkdir build

REM ---- Source sets ----------------------------------------------------------
set "INC=/I include /I src\json /I src\io /I src\engine /I src\weather"
set "GINC=-Iinclude -Isrc\json -Isrc\io -Isrc\engine -Isrc\weather"

set "ENGINE=src\engine\physics.c src\engine\fallout.c src\engine\terrain.c src\engine\engine.c src\io\output_teletype.c"
set "FULL=%ENGINE% src\engine\effects.c src\engine\casualties.c src\engine\lagrangian.c src\engine\ensemble.c src\weather\weather.c src\json\json.c src\io\output.c src\io\output_report.c src\io\scenario.c src\io\catalog.c src\io\validate.c src\cli\main.c"
set "UNIVAC=%ENGINE% src\univac\main.c"

REM ---- Locate a compiler ----------------------------------------------------
REM  NOTE: the MSVC setup lives in the :setup_msvc subroutine below and is
REM  invoked with `call`, NOT inlined here.  Calling vcvars64.bat from inside a
REM  parenthesized ( ) block corrupts cmd's parser and silently skips the lines
REM  after it - which is why an earlier version "found" MSVC but then errored.
set "COMPILER="
where cl >nul 2>&1 && set "COMPILER=cl"
if not defined COMPILER call :setup_msvc
if not defined COMPILER ( where clang >nul 2>&1 && set "COMPILER=clang" )
if not defined COMPILER ( where gcc   >nul 2>&1 && set "COMPILER=gcc" )
if not defined COMPILER goto :no_compiler
echo Using compiler: %COMPILER%

REM ---- Build ----------------------------------------------------------------
if /I "%TARGET%"=="univac" ( set "SRC=%UNIVAC%" & set "OUT=build\damaskino_univac.exe" & set "DEF=-DUNIVAC" ) else ( set "SRC=%FULL%" & set "OUT=build\damaskino.exe" & set "DEF=" )
if /I "%TARGET%"=="test" goto :build_test

echo Building %OUT% ...
if "%COMPILER%"=="cl" (
    set "CLDEF="
    if defined DEF set "CLDEF=/DUNIVAC"
    cl /nologo /O2 /W3 /wd4244 /wd4267 /D_USE_MATH_DEFINES %INC% !CLDEF! %SRC% /Fe:%OUT% /Fo:build\ >nul
    if errorlevel 1 goto :fail
) else (
    %COMPILER% -std=c11 -O2 -Wall -Wno-unused-parameter %GINC% %DEF% %SRC% -o %OUT% -lm
    if errorlevel 1 goto :fail
)
echo.
echo   BUILD SUCCESSFUL -^> %OUT%
echo.
if /I "%TARGET%"=="run" ( echo Running the Washington D.C. example: & echo. & %OUT% run examples\dc_500kt_surface.json --pop-density 4000 )
goto :eof

:build_test
echo Building and running tests ...
set "TESTENGINE=src\engine\physics.c src\engine\fallout.c src\engine\terrain.c src\engine\engine.c src\engine\effects.c src\engine\casualties.c src\engine\lagrangian.c src\engine\ensemble.c src\weather\weather.c src\json\json.c src\io\output.c src\io\output_report.c src\io\scenario.c src\io\catalog.c src\io\validate.c src\io\output_teletype.c"
for %%T in (test_engine test_effects test_casualties test_terrain test_lagrangian test_ensemble) do (
    if "%%T"=="test_effects" ( set "TE=src\engine\effects.c" ) else if "%%T"=="test_terrain" ( set "TE=src\engine\terrain.c src\engine\effects.c" ) else ( set "TE=%TESTENGINE%" )
    if "%COMPILER%"=="cl" (
        cl /nologo /O2 /D_USE_MATH_DEFINES %INC% !TE! tests\%%T.c /Fe:build\%%T.exe /Fo:build\ >nul || goto :fail
    ) else (
        %COMPILER% -std=c11 -O2 %GINC% !TE! tests\%%T.c -o build\%%T.exe -lm || goto :fail
    )
    echo --- %%T ---
    build\%%T.exe || goto :fail
)
echo.
echo   ALL TESTS PASSED
goto :eof

:no_compiler
echo.
echo ERROR: No C compiler found.
echo   Install one of:
echo     - Visual Studio 2022 with "Desktop development with C++"  (recommended)
echo     - LLVM/clang           https://releases.llvm.org
echo     - MinGW-w64 gcc        https://www.mingw-w64.org
echo   then re-run build.bat from any Command Prompt.
exit /b 1

:fail
echo.
echo   BUILD FAILED - see errors above.
exit /b 1

REM ---------------------------------------------------------------------------
REM  :setup_msvc  - find and initialise MSVC with no developer prompt.
REM  Runs at the top level of a subroutine (not inside a ( ) block) so that
REM  `call vcvars64.bat` cannot corrupt the parser.  Sets COMPILER=cl on success.
REM ---------------------------------------------------------------------------
:setup_msvc
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :eof
set "VSPATH="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH goto :eof
set "VCVARS=%VSPATH%\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" goto :eof
echo Setting up MSVC from "%VSPATH%" ...
call "%VCVARS%" >nul
where cl >nul 2>&1 && set "COMPILER=cl"
goto :eof
