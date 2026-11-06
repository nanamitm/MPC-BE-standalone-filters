@echo off
@cd /d "%~dp0"
@set "FILTER_PATH=%~dp0MpaDecFilter-fork.ax"
@if not exist "%FILTER_PATH%" goto missing
:uninstall
@regsvr32.exe "%FILTER_PATH%" /u /s
@if %errorlevel% NEQ 0 goto error
:success
@echo.
@echo.
@echo    Uninstallation succeeded.
@echo.
@goto done
:missing
@echo.
@echo.
@echo    Uninstallation failed.
@echo.
@echo    MpaDecFilter-fork.ax was not found.
@echo    Put the script in the same folder as MpaDecFilter-fork.ax.
@echo.
@goto done
:error
@echo.
@echo.
@echo    Uninstallation failed.
@echo.
@echo    You need to right click "Uninstall_MpaDecFilter-fork_64.cmd" and choose "Run as administrator".
@echo.
:done
@pause >NUL
