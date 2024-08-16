
:: Build for Visual Studio compiler. Run your copy of vcvars32.bat or vcvarsall.bat to setup command-line compiler.
:: Or run in "x64 Native Tools Command Promt for VS 20XX"

@echo off
setlocal

:: Unpack argument flags
for %%a in (%*) do (set "%%a=1")

set OUT_EXE=scaletd.exe
set OUT_DIR=build
set INCLUDES=/I include /I src
set SOURCES=src\main.c
set LIBS=lib\raylib.lib
set COMMON=/nologo /utf-8
set DEFINES=/D UNICODE /D _UNICODE
if "%debug%"=="1" (
    set OPTIONS=/Zi /MD /Od
) else (
    set OPTIONS=/O2
)

if "%clean%"=="1" (del /F /Q %OUT_DIR%)
if "%INPUT%"=="clean" (exit /b)

if not exist %OUT_DIR% mkdir %OUT_DIR%

@echo on
cl %COMMON% %OPTIONS% %INCLUDES% %DEFINES% %SOURCES% /Fe"%OUT_DIR%/%OUT_EXE%" /Fo%OUT_DIR%/ /link %LIBS% || exit /B
@echo off

copy /Y "bin\raylib.dll" %OUT_DIR%

if "%run%"=="1" (
    cd %~dp1
    call "%OUT_DIR%\%OUT_EXE%"
)