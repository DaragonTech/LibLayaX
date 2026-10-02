@echo off
rem laya.dll - confirmation run. Put this file next to laya.dll and run:
rem     run-tests.bat MODEL_FOLDER [cpu^|vulkan]
rem Example: run-tests.bat E:\Garbage\!laya\convaiinnovations@laya
rem Everything is printed as it happens. To keep a copy:  run-tests.bat MODEL > log.txt 2>&1
rem Set LAYA_DEBUG=1 beforehand to see what the library is doing inside each call.
setlocal DisableDelayedExpansion
cd /d "%~dp0"

set "MODEL=%~1"
if "%MODEL%"=="" set /p "MODEL=Model folder (contains rl_agent_config.json): "
rem Normalise the path: full path, no trailing backslash (a trailing \ would escape the closing quote).
for %%I in ("%MODEL%\.") do set "MODEL=%%~fI"
if "%MODEL:~-1%"=="\" set "MODEL=%MODEL:~0,-1%"
set "BACKEND=%~2"
if "%BACKEND%"=="" set "BACKEND=cpu"

if not exist "laya.dll" (
  echo laya.dll is not in this folder: %CD%
  exit /b 2
)
if not exist "%MODEL%\rl_agent_config.json" (
  echo No model found at: %MODEL%
  echo ^(expected rl_agent_config.json there^)
  exit /b 2
)

set LAYA_CRASH_REPORT=1
set "LAYA_TEST_BACKEND=%BACKEND%"
set FAILED=0
if exist laya-crash.txt del laya-crash.txt

echo laya.dll confirmation run, backend %BACKEND%
echo model: %MODEL%
echo.

set RC0=skipped
if not exist laya-probe.exe goto :suite
echo ===== 1. laya-probe.exe (C++ runtime, a few seconds) =====
laya-probe.exe
set RC0=%ERRORLEVEL%
if not "%RC0%"=="0" set FAILED=1
echo exit code %RC0%
echo.

:suite
echo ===== 2. test-capi.exe (each [step] is printed before it runs) =====
set "LAYA_TEST_MODEL=%MODEL%"
test-capi.exe
set RC1=%ERRORLEVEL%
if not "%RC1%"=="0" set FAILED=1
echo exit code %RC1%
echo.

set RC2=skipped
if not exist LayaTests.exe goto :bench
echo ===== 3. LayaTests.exe (Pascal binding) =====
LayaTests.exe "%MODEL%" %BACKEND%
set RC2=%ERRORLEVEL%
if not "%RC2%"=="0" set FAILED=1
echo exit code %RC2%
echo.

:bench
echo ===== 4. laya-bench.exe (speed) =====
if /i "%BACKEND%"=="cpu" (
  laya-bench.exe "%MODEL%" "{\"backend\":\"cpu\"}"
) else (
  laya-bench.exe "%MODEL%"
)
set RC3=%ERRORLEVEL%
if not "%RC3%"=="0" set FAILED=1
echo exit code %RC3%

if exist laya-crash.txt (
  echo.
  echo ===== laya-crash.txt =====
  type laya-crash.txt
  set FAILED=1
)

echo.
echo ------------------------------------------------------------
if "%FAILED%"=="0" (
  echo RESULT: ALL PASSED   ^(probe %RC0%, test-capi %RC1%, LayaTests %RC2%, laya-bench %RC3%^)
) else (
  echo RESULT: SOMETHING FAILED   ^(probe %RC0%, test-capi %RC1%, LayaTests %RC2%, laya-bench %RC3%^)
  echo Please send everything printed above.
)
if "%FAILED%"=="0" (exit /b 0) else (exit /b 1)
