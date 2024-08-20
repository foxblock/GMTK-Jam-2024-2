
:: Build for Visual Studio compiler. Run your copy of vcvars32.bat or vcvarsall.bat to setup command-line compiler.
:: Or run in "x64 Native Tools Command Promt for VS 20XX"

@echo off

:: Find cl.exe
where /q cl.exe
if ERRORLEVEL 1 (
	echo Setting up x64 compile environment...
) else (
	goto buildstep
)

set "VSWHERE_PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
:: Find latest VS install with C++ workload (Component.VC.CoreIde)
if exist "%VSWHERE_PATH%" (
	for /f "tokens=*" %%i in ('"%VSWHERE_PATH%" -latest -requires Microsoft.VisualStudio.Component.VC.CoreIde -property installationPath') do set "VS_PATH=%%i"
) else (
	:: vswhere not found, guessing Visual Studio install path.
	set "VS_PATH=%ProgramFiles%\Microsoft Visual Studio\2022\Community"
	if not exist !VS_PATH!\* set "VS_PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community"
)
set "VCVARS64_PATH=%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS64_PATH%" (
	echo x64 Native Tools Command Prompt for VS ^(vcvars64.bat^) could not be found!
	echo Make sure Visual Studio ^(2019 or 2022^) with the "Desktop development with C++" workload is installed
	echo https://learn.microsoft.com/en-us/cpp/build/vscpp-step-0-installation?view=msvc-170
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
set COMMON=/nologo /utf-8
if "%debug%"=="1" (
	set DEFINES=/D UNICODE /D _UNICODE /D _DEBUG
    set OPTIONS=/Zi /MD /Od
) else (
	set DEFINES=/D UNICODE /D _UNICODE
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
endlocal