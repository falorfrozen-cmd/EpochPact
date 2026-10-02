@echo off
rem Builds EpochPact's native parts with MSVC (x64, static CRT):
rem   build\version.dll           the loader UnityPlayer.dll picks up from the game folder
rem   build\EpochPact.Core.dll    the core it starts from <game>\EpochPact\
rem   build\hook_test.exe         decoder and hook engine tests (run it after a build)
setlocal
set "HERE=%~dp0"
set "OUT=%HERE%build"
if not exist "%OUT%" mkdir "%OUT%"
if not exist "%OUT%\tests" mkdir "%OUT%\tests"

rem No parenthesised block here: %VSDIR% would expand before the for loop sets it.
if defined VSCMD_VER goto :have_msvc
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR ( echo MSVC not found & exit /b 1 )
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
:have_msvc

set "CFLAGS=/nologo /O2 /MT /W4 /permissive- /std:c++20 /utf-8 /DNDEBUG /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN"
set "CORE=%HERE%core"

echo [1/3] loader (version.dll)
ml64 /nologo /c /Fo"%OUT%\version_thunks.obj" "%HERE%proxy\version_thunks.asm" || exit /b 1
cl %CFLAGS% /EHsc /c /Fo"%OUT%\version_proxy.obj" "%HERE%proxy\version_proxy.cpp" || exit /b 1
link /nologo /DLL /OUT:"%OUT%\version.dll" /DEF:"%HERE%proxy\version.def" "%OUT%\version_thunks.obj" "%OUT%\version_proxy.obj" kernel32.lib || exit /b 1

echo [2/3] core (EpochPact.Core.dll)
rem EPOCHPACT_RESEARCH: the metadata dump, capture hooks and research commands (not for players).
set "CORE_SRC=common il2cpp_api dumper core x64_decode hook game mainthread xp loot commands research"
set "CORE_CPP="
set "CORE_OBJ="
for %%f in (%CORE_SRC%) do call set "CORE_CPP=%%CORE_CPP%% "%CORE%\%%f.cpp""
for %%f in (%CORE_SRC%) do call set "CORE_OBJ=%%CORE_OBJ%% "%OUT%\%%f.obj""
cl %CFLAGS% /EHa /DEPOCHPACT_RESEARCH /c /Fo"%OUT%\\" %CORE_CPP% || exit /b 1
link /nologo /DLL /OUT:"%OUT%\EpochPact.Core.dll" %CORE_OBJ% kernel32.lib user32.lib psapi.lib || exit /b 1

echo [3/3] tests (hook_test.exe)
ml64 /nologo /c /Fo"%OUT%\tests\test_targets.obj" "%HERE%tests\test_targets.asm" || exit /b 1
cl %CFLAGS% /EHsc /c /Fo"%OUT%\tests\\" "%HERE%tests\hook_test.cpp" "%CORE%\x64_decode.cpp" "%CORE%\hook.cpp" || exit /b 1
link /nologo /OUT:"%OUT%\hook_test.exe" "%OUT%\tests\hook_test.obj" "%OUT%\tests\x64_decode.obj" "%OUT%\tests\hook.obj" "%OUT%\tests\test_targets.obj" kernel32.lib || exit /b 1

ml64 /nologo /c /Fo"%OUT%\tests\xp_targets.obj" "%HERE%tests\xp_targets.asm" || exit /b 1
cl %CFLAGS% /EHa /c /Fo"%OUT%\tests\\" "%HERE%tests\xp_test.cpp" "%HERE%tests\fake_game.cpp" "%CORE%\xp.cpp" || exit /b 1
link /nologo /OUT:"%OUT%\xp_test.exe" "%OUT%\tests\xp_test.obj" "%OUT%\tests\fake_game.obj" "%OUT%\tests\xp.obj" "%OUT%\tests\x64_decode.obj" "%OUT%\tests\hook.obj" "%OUT%\tests\xp_targets.obj" kernel32.lib || exit /b 1

echo built: version.dll, EpochPact.Core.dll, hook_test.exe and xp_test.exe in %OUT%
endlocal
