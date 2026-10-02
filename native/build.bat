@echo off
rem Builds EpochPact's native parts with MSVC (x64, static CRT):
rem   build\version.dll           the loader UnityPlayer.dll picks up from the game folder
rem   build\EpochPact.Core.dll    the core it starts from <game>\EpochPact\
setlocal
set "HERE=%~dp0"
set "OUT=%HERE%build"
if not exist "%OUT%" mkdir "%OUT%"

rem No parenthesised block here: %VSDIR% would expand before the for loop sets it.
if defined VSCMD_VER goto :have_msvc
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR ( echo MSVC not found & exit /b 1 )
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
:have_msvc

set "CFLAGS=/nologo /O2 /MT /W4 /permissive- /std:c++20 /utf-8 /DNDEBUG /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN"

echo [1/2] loader (version.dll)
ml64 /nologo /c /Fo"%OUT%\version_thunks.obj" "%HERE%proxy\version_thunks.asm" || exit /b 1
cl %CFLAGS% /EHsc /c /Fo"%OUT%\version_proxy.obj" "%HERE%proxy\version_proxy.cpp" || exit /b 1
link /nologo /DLL /OUT:"%OUT%\version.dll" /DEF:"%HERE%proxy\version.def" "%OUT%\version_thunks.obj" "%OUT%\version_proxy.obj" kernel32.lib || exit /b 1

echo [2/2] core (EpochPact.Core.dll)
cl %CFLAGS% /EHa /c /Fo"%OUT%\\" "%HERE%core\common.cpp" "%HERE%core\il2cpp_api.cpp" "%HERE%core\dumper.cpp" "%HERE%core\core.cpp" || exit /b 1
link /nologo /DLL /OUT:"%OUT%\EpochPact.Core.dll" "%OUT%\common.obj" "%OUT%\il2cpp_api.obj" "%OUT%\dumper.obj" "%OUT%\core.obj" kernel32.lib user32.lib psapi.lib || exit /b 1

echo built: %OUT%\version.dll and %OUT%\EpochPact.Core.dll
endlocal
