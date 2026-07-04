@echo off
rem One-command build: configure + compile + self-test + stage a playable dist\.
rem Needs CMake and a C++ toolchain (Visual Studio Build Tools work; SDL2/glm
rem are fetched and built automatically if not installed — first run is slow).
rem
rem   build.bat            build, run the headless self-test suite, stage dist\
rem   build.bat skiptests  build and stage without the test gate
rem   build.bat run        all of the above, then launch the game
setlocal
cd /d "%~dp0"

set SKIP_TESTS=0
set RUN_AFTER=0
for %%A in (%*) do (
  if /i "%%A"=="skiptests" set SKIP_TESTS=1
  if /i "%%A"=="run" set RUN_AFTER=1
)

echo == configure ==
cmake -B build -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1

echo == build ==
cmake --build build --config Release --parallel
if errorlevel 1 exit /b 1

set BIN=build\Release\godgame.exe
if not exist "%BIN%" set BIN=build\godgame.exe
if not exist "%BIN%" (
  echo build finished but no godgame.exe found under build\
  exit /b 1
)

if "%SKIP_TESTS%"=="1" goto stage
echo == self-test (--headless) ==
"%BIN%" --headless > build\headless.log 2>&1
if errorlevel 1 (
  type build\headless.log
  echo SELF-TEST FAILED — dist\ not staged (full log: build\headless.log)
  exit /b 1
)

:stage
echo == stage dist\ ==
if not exist dist mkdir dist
copy /y "%BIN%" dist\godgame.exe >nul
copy /y docs\PLAY.txt dist\PLAY.txt >nul
rem Static SDL2 is the normal case (single exe). If CMake linked a system/shared
rem SDL2 instead, ship its DLL next to the exe.
for /r build %%D in (SDL2.dll) do if exist "%%D" copy /y "%%D" dist\ >nul
echo ready: dist\godgame.exe   (see dist\PLAY.txt; maps\ and saves\ appear beside it as you play)

if "%RUN_AFTER%"=="1" start "" dist\godgame.exe
exit /b 0
