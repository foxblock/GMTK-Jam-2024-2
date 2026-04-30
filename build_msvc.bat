
:: Build for Visual Studio compiler. Run your copy of vcvars32.bat or vcvarsall.bat to setup command-line compiler.
:: Or run in "x64 Native Tools Command Promt for VS 20XX"

@echo off

:: Find cl.exe
where /q cl.exe
if ERRORLEVEL 1 (
	echo Setting up x64 compile environment...
    echo NOTE: If you see this every time you run this script, you are probably running it in PowerShell and should run it in cmd.exe instead!
) else (
	goto buildstep
)

:: Find latest VS/BuildTools install with C++ workload (Component.VC.CoreIde)
set "VSWHERE_PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE_PATH%" (
	for /f "tokens=*" %%i in ('"%VSWHERE_PATH%" -products * -latest -requires Microsoft.VisualStudio.Component.VC.CoreIde -property installationPath') do set "VS_PATH=%%i"
)
if "%VS_PATH%"=="" (
	:: vswhere not found or returned nothing, guessing Visual Studio install path.
    echo Guessing Visual Studio/Build Tools install path...
    set "VS_PATH=%ProgramFiles%\Microsoft Visual Studio\2022\Community"
)
set "VCVARS64_PATH=%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS64_PATH%" (
	echo x64 Native Tools Command Prompt for VS ^(vcvars64.bat^) could not be found!
	echo Make sure either Visual Studio ^(2019 or 2022^) or the Visual Studio Build Tools with the "Desktop development with C++" workload are installed
	echo https://learn.microsoft.com/en-us/cpp/build/vscpp-step-0-installation?view=msvc-170
    echo If you are sure to have it installed, you can also call this script directly from a x64 Native Tools Command Prompt
	echo ^(DEBUG^) VCVARS64_PATH is "%VCVARS64_PATH%"
	exit /B
)
call "%VCVARS64_PATH%"

:buildstep

setlocal
:: Unpack argument flags
for %%a in (%*) do (set "%%a=1")

set OUT_EXE=scaletd.exe
set OUT_DIR=build
set INCLUDES=/I include /I src
set SOURCES=src\main.c
set LIBS=lib\raylib.lib
:: /wd4100 - disable warnings about unused parameters in functions
set COMMON=/nologo /utf-8 /diagnostics:column /EHsc /sdl /W4 /wd4100 /WX /external:I include /external:W1
if "%debug%"=="1" (
	set DEFINES=/DUNICODE /D_UNICODE /D_DEBUG /D_CRT_SECURE_NO_WARNINGS
    set OPTIONS=/Od /MTd /Zi 
) else (
	set DEFINES=/DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS
    set OPTIONS=/O2 /MT
)

if "%clean%"=="1" (del /F /Q %OUT_DIR%)
if "%1"=="clean" if "%2"=="" (exit /b)

if not exist %OUT_DIR% mkdir %OUT_DIR%

@echo on
cl %COMMON% %OPTIONS% %INCLUDES% %DEFINES% %SOURCES% /Fe"%OUT_DIR%/%OUT_EXE%" /Fo%OUT_DIR%/ /link %LIBS% || exit /B
@echo off

copy /Y "bin\raylib.dll" %OUT_DIR%

if "%run%"=="1" (
    cd %~dp1
    call "%OUT_DIR%\%OUT_EXE%"
)
endlocal