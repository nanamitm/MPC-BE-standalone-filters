@echo off
@cd /d "%~dp0"
@set "FILTER_PATH=%~dp0MpcAudioRenderer-fork.ax"
@if not exist "%FILTER_PATH%" goto missing
:install
@regsvr32.exe "%FILTER_PATH%" /s
@if %errorlevel% NEQ 0 goto error
:success
@echo.
@echo.
@echo    Installation succeeded.
@echo.
@echo    Please do not delete "%FILTER_PATH%".
@echo    The installer has not copied the file anywhere.
@echo.
@goto done
:missing
@echo.
@echo.
@echo    Installation failed.
@echo.
@echo    MpcAudioRenderer-fork.ax was not found.
@echo    Put the script in the same folder as MpcAudioRenderer-fork.ax.
@echo.
@goto done
:error
@echo.
@echo.
@echo    Installation failed.
@echo.
@echo    You need to right click "Install_MpcAudioRenderer-fork_64.cmd" and choose "Run as administrator".
@echo.
:done
@pause >NUL
