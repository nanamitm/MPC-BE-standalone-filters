@echo off
setlocal
where cl >nul 2>nul
if errorlevel 1 (
  echo Run from an x64 Visual Studio Native Tools command prompt.
  exit /b 1
)
cd /d "%~dp0.."
set "AUDIO_TEST_LIBS=%~1"
if not defined AUDIO_TEST_LIBS set "AUDIO_TEST_LIBS=_bin\validation-upstream-x64"
set "AUDIO_TEST_OUT=_bin\spatial-tests"
if not exist "%AUDIO_TEST_OUT%" mkdir "%AUDIO_TEST_OUT%"
cl /nologo /utf-8 /EHsc /std:c++17 /MT /DUNICODE /D_UNICODE /DNDEBUG /I src /I include /I src\ExtLib\BaseClasses tests\audio_decoder_graph.cpp /Fo:"%AUDIO_TEST_OUT%\graph.obj" /Fe:"%AUDIO_TEST_OUT%\graph.exe" /link /LTCG /LIBPATH:"%AUDIO_TEST_LIBS%" /LIBPATH:"_bin\lib\Release_x64" BaseClasses.lib
if errorlevel 1 exit /b 1
for %%C in (1 2 13) do for %%S in (0 1) do (
  "%AUDIO_TEST_OUT%\graph.exe" "%AUDIO_TEST_LIBS%\MpaDecFilter-fork.ax" %%C %%S
  if errorlevel 1 exit /b 1
  "%AUDIO_TEST_OUT%\graph.exe" "%AUDIO_TEST_LIBS%\MpaDecFilter-fork.ax" %%C %%S 0 "%AUDIO_TEST_LIBS%\AudioSwitcher-fork.ax"
  if errorlevel 1 exit /b 1
)
"%AUDIO_TEST_OUT%\graph.exe" "%AUDIO_TEST_LIBS%\MpaDecFilter-fork.ax" 13 1 1 "%AUDIO_TEST_LIBS%\AudioSwitcher-fork.ax"
if errorlevel 1 exit /b 1
"%AUDIO_TEST_OUT%\graph.exe" "%AUDIO_TEST_LIBS%\MpaDecFilter-fork.ax" 24 1 0 "%AUDIO_TEST_LIBS%\AudioSwitcher-fork.ax"
if errorlevel 1 exit /b 1
"%AUDIO_TEST_OUT%\graph.exe" "%AUDIO_TEST_LIBS%\MpaDecFilter-fork.ax" 97 0
if errorlevel 1 exit /b 1
"%AUDIO_TEST_OUT%\graph.exe" "%AUDIO_TEST_LIBS%\MpaDecFilter-fork.ax" 98 0
if errorlevel 1 exit /b 1
"%AUDIO_TEST_OUT%\graph.exe" "%AUDIO_TEST_LIBS%\MpaDecFilter-fork.ax" 98 0 0 "%AUDIO_TEST_LIBS%\AudioSwitcher-fork.ax"
if errorlevel 1 exit /b 1
"%AUDIO_TEST_OUT%\graph.exe" "%AUDIO_TEST_LIBS%\MpaDecFilter-fork.ax" 99 0
if errorlevel 1 exit /b 1
"%AUDIO_TEST_OUT%\graph.exe" "%AUDIO_TEST_LIBS%\MpaDecFilter-fork.ax" 99 0 0 "%AUDIO_TEST_LIBS%\AudioSwitcher-fork.ax"
if errorlevel 1 exit /b 1
rem Optional real endpoint test: all samples are silent; no filter registration.
if /i "%~2"=="endpoint" (
  "%AUDIO_TEST_OUT%\graph.exe" "%AUDIO_TEST_LIBS%\MpaDecFilter-fork.ax" 13 1 0 "%AUDIO_TEST_LIBS%\AudioSwitcher-fork.ax" "%AUDIO_TEST_LIBS%\MpcAudioRenderer-fork.ax"
  if errorlevel 1 exit /b 1
)
exit /b 0
