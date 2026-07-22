@echo off
setlocal enabledelayedexpansion

if "%CC%"=="" (
    echo Don't run this directly
    exit /b 1
)

cd /d "%~dp0"

if not exist tmp mkdir tmp

type nul > tmp\config.log
> config.bat echo @echo off

>tmp\test.c echo.#include ^<stdbool.h^>
>>tmp\test.c echo.int main(void^){return 0;}
call :check if stdbool.h works
if errorlevel 1 (
    >>config.bat echo set INCLUDES=%%INCLUDES%% /Icompat\stdbool
)

>tmp\test.c echo.#include ^<stdio.h^>
>>tmp\test.c echo.int main(void^){
>>tmp\test.c echo.    char buf[64];
>>tmp\test.c echo.    return snprintf(buf, sizeof(buf^), "test"^);
>>tmp\test.c echo.}
call :check for snprintf
if errorlevel 1 (
    >>config.bat echo set DEFINES=%%DEFINES%% /DNO_SNPRINTF
    >>config.bat echo set INCLUDES=%%INCLUDES%% /Icompat\stdio
    >>config.bat echo set SRCS=%%SRCS%% compat\stdio\nanoprintf_impl.c
)

>tmp\test.c echo.#include ^<math.h^>
>>tmp\test.c echo.int main(void^){return fmin(0,0);}
call :check for fmin
if errorlevel 1 (
    >>config.bat echo set DEFINES=%%DEFINES%% /DNO_FMIN
)

>tmp\test.c echo.#include ^<math.h^>
>>tmp\test.c echo.int main(void^){return fmax(0,0);}
call :check for fmax
if errorlevel 1 (
    >>config.bat echo set DEFINES=%%DEFINES%% /DNO_FMAX
)

>tmp\test.c echo.#include ^<math.h^>
>>tmp\test.c echo.int main(void^){return round(0);}
call :check for round
if errorlevel 1 (
    >>config.bat echo set DEFINES=%%DEFINES%% /DNO_ROUND
)

del tmp\test.c tmp\test.obj 2>nul
exit /b 0

:check
echo.checking %* >> tmp\config.log
%CC% /nologo /Oi- tmp\test.c /c /Fo:tmp\test.obj >> tmp\config.log 2>&1
if %errorlevel% equ 0 (
    echo checking %*: yes
    exit /b 0
) else (
    echo checking %*: no
    exit /b 1
)
