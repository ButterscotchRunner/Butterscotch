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

echo.checking for stdbool.h: >> tmp\config.log
%CC% /nologo /Oi- tmp\test.c /c /Fo:tmp\test.obj >> tmp\config.log 2>&1
if %errorlevel% neq 0 (
    echo checking for stdbool.h: no
    >>config.bat echo set INCLUDES=%%INCLUDES%% /Icompat\stdbool
) else (
    echo checking for stdbool.h: yes
)

>tmp\test.c echo.#include ^<stdio.h^>
>>tmp\test.c echo.int main(void^){
>>tmp\test.c echo.    char buf[64];
>>tmp\test.c echo.    return snprintf(buf, sizeof(buf^), "test"^);
>>tmp\test.c echo.}

echo.checking for snprintf: >> tmp\config.log
%CC% /nologo /Oi- tmp\test.c /c /Fo:tmp\test.obj >> tmp\config.log 2>&1
if %errorlevel% neq 0 (
    echo checking for snprintf: no
    >>config.bat echo set DEFINES=%%DEFINES%% /DNO_SNPRINTF
    >>config.bat echo set INCLUDES=%%INCLUDES%% /Icompat\stdio
    >>config.bat echo set SRCS=%%SRCS%% compat\stdio\nanoprintf_impl.c
) else (
    echo checking for snprintf: yes
)

del tmp\test.c tmp\test.obj 2>nul
