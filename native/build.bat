@echo off
rem Builds EpochPact's native parts with MSVC (x64, static CRT):
rem   build\version.dll           the loader UnityPlayer.dll picks up from the game folder
rem   build\EpochPact.Core.dll    the core it starts from <game>\EpochPact\
rem   build\hook_test.exe         decoder and hook engine tests (run it after a build)
setlocal
set "HERE=%~dp0"
set "OUT=%HERE%build"
rem An isolated loader-only build leaves the previously published core untouched.
if "%~1"=="loader" if not "%~2"=="" set "OUT=%~f2"
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
rem Default/core keep the existing research workflow. Player builds exclude
rem metadata dumps, capture trackers, test fixtures and research housekeeping.
set "CORE_FLAGS=/DEPOCHPACT_RESEARCH"
set "FLAVOR=research"
if "%~1"=="player" set "CORE_FLAGS="
if "%~1"=="player-core" set "CORE_FLAGS="
if "%~1"=="player" set "FLAVOR=player"
if "%~1"=="player-core" set "FLAVOR=player"
rem Isolated crafting fixtures only; never install general research trackers.
if "%~1"=="test-core" set "CORE_FLAGS=/DEPOCHPACT_TESTING"
if "%~1"=="test-core" set "FLAVOR=test"

if "%~1"=="core" goto :core
if "%~1"=="player-core" goto :core
if "%~1"=="test-core" goto :core
echo [1/3] loader (version.dll)
ml64 /nologo /c /Fo"%OUT%\version_thunks.obj" "%HERE%proxy\version_thunks.asm" || exit /b 1
cl %CFLAGS% /EHsc /c /Fo"%OUT%\version_proxy.obj" "%HERE%proxy\version_proxy.cpp" || exit /b 1
link /nologo /DLL /OUT:"%OUT%\version.dll" /DEF:"%HERE%proxy\version.def" "%OUT%\version_thunks.obj" "%OUT%\version_proxy.obj" kernel32.lib || exit /b 1
if "%~1"=="loader" goto :done

:core
echo [2/3] core (EpochPact.Core.dll)
rem EPOCHPACT_RESEARCH: the metadata dump, capture hooks and research commands (not for players).
ml64 /nologo /c /Fo"%OUT%\stat_thunk.obj" "%HERE%core\stat_thunk.asm" || exit /b 1
set "CORE_SRC=common il2cpp_api dumper core x64_decode hook game mainthread lifecycle xp loot density items smart_loot crafting map_view collection player stat_editor progression monolith factions cof cof_tuning commands research"
set "CORE_CPP="
set "CORE_OBJ="
for %%f in (%CORE_SRC%) do call set "CORE_CPP=%%CORE_CPP%% "%CORE%\%%f.cpp""
for %%f in (%CORE_SRC%) do call set "CORE_OBJ=%%CORE_OBJ%% "%OUT%\%%f.obj""
cl %CFLAGS% /EHa %CORE_FLAGS% /c /Fo"%OUT%\\" %CORE_CPP% || exit /b 1
link /nologo /DLL /OUT:"%OUT%\EpochPact.Core.dll" %CORE_OBJ% "%OUT%\stat_thunk.obj" kernel32.lib user32.lib psapi.lib || exit /b 1
py -3 "%HERE%..\tools\stamp_build.py" %FLAVOR% || exit /b 1
if "%~1"=="core" goto :done
if "%~1"=="player-core" goto :done
if "%~1"=="test-core" goto :done

echo [3/3] tests (hook_test.exe)
ml64 /nologo /c /Fo"%OUT%\tests\test_targets.obj" "%HERE%tests\test_targets.asm" || exit /b 1
cl %CFLAGS% /EHsc /c /Fo"%OUT%\tests\\" "%HERE%tests\hook_test.cpp" "%CORE%\x64_decode.cpp" "%CORE%\hook.cpp" || exit /b 1
link /nologo /OUT:"%OUT%\hook_test.exe" "%OUT%\tests\hook_test.obj" "%OUT%\tests\x64_decode.obj" "%OUT%\tests\hook.obj" "%OUT%\tests\test_targets.obj" kernel32.lib || exit /b 1

ml64 /nologo /c /Fo"%OUT%\tests\xp_targets.obj" "%HERE%tests\xp_targets.asm" || exit /b 1
cl %CFLAGS% /EHa /c /Fo"%OUT%\tests\\" "%HERE%tests\xp_test.cpp" "%HERE%tests\fake_game.cpp" "%CORE%\xp.cpp" || exit /b 1
link /nologo /OUT:"%OUT%\xp_test.exe" "%OUT%\tests\xp_test.obj" "%OUT%\tests\fake_game.obj" "%OUT%\tests\xp.obj" "%OUT%\tests\x64_decode.obj" "%OUT%\tests\hook.obj" "%OUT%\tests\xp_targets.obj" kernel32.lib || exit /b 1

cl %CFLAGS% /EHsc /Fo"%OUT%\tests\\" /Fe"%OUT%\stat_key_test.exe" "%HERE%tests\stat_key_test.cpp" || exit /b 1
cl %CFLAGS% /EHsc /Fo"%OUT%\tests\\" /Fe"%OUT%\density_test.exe" "%HERE%tests\density_test.cpp" || exit /b 1
cl %CFLAGS% /EHsc /Fo"%OUT%\tests\\" /Fe"%OUT%\monolith_test.exe" "%HERE%tests\monolith_test.cpp" || exit /b 1
cl %CFLAGS% /EHsc /Fo"%OUT%\tests\\" /Fe"%OUT%\cof_test.exe" "%HERE%tests\cof_test.cpp" || exit /b 1
cl %CFLAGS% /EHsc /Fo"%OUT%\tests\\" /Fe"%OUT%\loot_crafting_test.exe" "%HERE%tests\loot_crafting_test.cpp" || exit /b 1
cl %CFLAGS% /EHsc /Fo"%OUT%\tests\\" /Fe"%OUT%\review_test.exe" "%HERE%tests\review_test.cpp" || exit /b 1
cl %CFLAGS% /EHa /c /Fo"%OUT%\tests\\" "%HERE%tests\mainthread_test.cpp" "%CORE%\mainthread.cpp" || exit /b 1
link /nologo /OUT:"%OUT%\mainthread_test.exe" "%OUT%\tests\mainthread_test.obj" "%OUT%\tests\mainthread.obj" "%OUT%\tests\x64_decode.obj" "%OUT%\tests\hook.obj" kernel32.lib || exit /b 1

cl %CFLAGS% /EHa /c /Fo"%OUT%\tests\\" "%HERE%tests\lifecycle_test.cpp" "%CORE%\lifecycle.cpp" || exit /b 1
link /nologo /OUT:"%OUT%\lifecycle_test.exe" "%OUT%\tests\lifecycle_test.obj" "%OUT%\tests\lifecycle.obj" "%OUT%\tests\mainthread.obj" "%OUT%\tests\x64_decode.obj" "%OUT%\tests\hook.obj" kernel32.lib || exit /b 1

echo built: version.dll, EpochPact.Core.dll, hook_test.exe, xp_test.exe, stat_key_test.exe, density_test.exe, monolith_test.exe and cof_test.exe in %OUT%
:done
endlocal
