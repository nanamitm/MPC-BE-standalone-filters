@echo off
setlocal
where cl >nul 2>nul
if errorlevel 1 (
  echo Run from an x64 Visual Studio Native Tools command prompt.
  exit /b 1
)
cd /d "%~dp0.."
set "SPATIAL_TEST_LIBS=%~1"
if not defined SPATIAL_TEST_LIBS set "SPATIAL_TEST_LIBS=_bin\validation-upstream-x64"
if not exist "%SPATIAL_TEST_LIBS%\MpcAudioRenderer-fork.ax" exit /b 1
if not exist "_bin\spatial-tests" mkdir "_bin\spatial-tests"
set "SPATIAL_TEST_OUT=_bin\spatial-tests"
cl /nologo /utf-8 /EHsc /std:c++17 /MT /I src tests\spatial_audio_smoke.cpp /Fo:"%SPATIAL_TEST_OUT%\smoke.obj" /Fe:"%SPATIAL_TEST_OUT%\smoke.exe"
if errorlevel 1 exit /b 1
"%SPATIAL_TEST_OUT%\smoke.exe" "%SPATIAL_TEST_LIBS%\MpcAudioRenderer-fork.ax"
if errorlevel 1 exit /b 1
rem In the console test, UCRT supplies these conversions. Exclude the duplicate
rem MinGW archive members from a test-only copy; do not modify the bundled lib.
lib /nologo lib64\libmingwex.a /remove:lib64_libmingwex_a-wcstoimax.o /remove:lib64_libmingwex_a-wcstoumax.o /out:"%SPATIAL_TEST_OUT%\mingwex.lib"
if errorlevel 1 exit /b 1
cl /nologo /utf-8 /EHsc /DNDEBUG /Gy /Gw /Zc:inline /O2 /GL /std:c++17 /MT /D_UNICODE /DUNICODE /DWINVER=0x0A00 /D_WIN32_WINNT=0x0A00 /I include /I src /I src\ExtLib\ffmpeg tests\spatial_audio_downmix.cpp /Fo:"%SPATIAL_TEST_OUT%\downmix.obj" /Fe:"%SPATIAL_TEST_OUT%\downmix.exe" /link /LTCG /OPT:REF /LIBPATH:"%SPATIAL_TEST_LIBS%" /LIBPATH:lib64 BaseClasses.lib AudioTools.lib DSUtil.lib ffmpeg.lib libsoxr.lib libbs2b.lib libgcc.a "%SPATIAL_TEST_OUT%\mingwex.lib"
if errorlevel 1 exit /b 1
"%SPATIAL_TEST_OUT%\downmix.exe"
exit /b %errorlevel%
