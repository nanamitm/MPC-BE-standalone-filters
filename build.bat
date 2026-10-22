@ECHO OFF
REM (C) 2009-2026 see Authors.txt
REM
REM This file is part of MPC-BE.
REM
REM MPC-BE is free software; you can redistribute it and/or modify
REM it under the terms of the GNU General Public License as published by
REM the Free Software Foundation; either version 3 of the License, or
REM (at your option) any later version.
REM
REM MPC-BE is distributed in the hope that it will be useful,
REM but WITHOUT ANY WARRANTY; without even the implied warranty of
REM MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
REM GNU General Public License for more details.
REM
REM You should have received a copy of the GNU General Public License
REM along with this program.  If not, see <http://www.gnu.org/licenses/>.
REM
REM This fork only builds standalone filters (MPCVideoDec-fork.ax and
REM MpcAudioRenderer-fork.ax), not the full MPC-BE player - the "Filter"
REM configurations of MPCVideoDec.sln are the only ones this script (and the
REM solution) support.

SETLOCAL ENABLEDELAYEDEXPANSION
CD /D %~dp0

SET ARG=%*
SET ARG=%ARG:/=%
SET ARG=%ARG:-=%
SET ARGB=0
SET ARGBC=0
SET ARGF=0
SET ARGCOMP=0
SET ARGPL=0
SET ARGZI=0
SET INPUT=0
SET ARGSIGN=0
SET "Wait=True"

IF /I "%ARG%" == "?"          GOTO ShowHelp

FOR %%A IN (%ARG%) DO (
  IF /I "%%A" == "help"       GOTO ShowHelp
  IF /I "%%A" == "GetVersion" ENDLOCAL & CALL :SubGetVersion & EXIT /B
  IF /I "%%A" == "Build"      SET "BUILDTYPE=Build"     & SET /A ARGB+=1
  IF /I "%%A" == "Clean"      SET "BUILDTYPE=Clean"     & SET /A ARGB+=1
  IF /I "%%A" == "Rebuild"    SET "BUILDTYPE=Rebuild"   & SET /A ARGB+=1
  IF /I "%%A" == "Both"       SET "BUILDPLATFORM=Both"  & SET /A ARGPL+=1
  IF /I "%%A" == "Win32"      SET "BUILDPLATFORM=Win32" & SET /A ARGPL+=1
  IF /I "%%A" == "x86"        SET "BUILDPLATFORM=Win32" & SET /A ARGPL+=1
  IF /I "%%A" == "x64"        SET "BUILDPLATFORM=x64"   & SET /A ARGPL+=1
  IF /I "%%A" == "Filters"    SET /A ARGF+=1
  IF /I "%%A" == "Debug"      SET "BUILDCFG=Debug"      & SET /A ARGBC+=1
  IF /I "%%A" == "Release"    SET "BUILDCFG=Release"    & SET /A ARGBC+=1
  IF /I "%%A" == "VS2019"     SET "COMPILER=VS2019"     & SET /A ARGCOMP+=1
  IF /I "%%A" == "VS2022"     SET "COMPILER=VS2022"     & SET /A ARGCOMP+=1
  IF /I "%%A" == "VS2026"     SET "COMPILER=VS2026"     & SET /A ARGCOMP+=1
  IF /I "%%A" == "Zip"        SET "ZIP=True"            & SET /A ARGZI+=1
  IF /I "%%A" == "Sign"       SET "SIGN=True"           & SET /A ARGSIGN+=1
  IF /I "%%A" == "NoWait"     SET "Wait=False"
)

REM pre-build checks

CALL "%~dp0update_revision.cmd"

IF EXIST "%~dp0environments.bat" CALL "%~dp0environments.bat"

IF NOT DEFINED MPCBE_MINGW GOTO MissingVar
IF NOT DEFINED MPCBE_MSYS  GOTO MissingVar

FOR %%X IN (%*) DO (
  IF /I "%%X" NEQ "NoWait" SET /A INPUT+=1
)
SET /A VALID=%ARGB%+%ARGPL%+%ARGF%+%ARGBC%+%ARGZI%+%ARGSIGN%+%ARGCOMP%

IF %VALID% NEQ %INPUT% GOTO UnsupportedSwitch

IF %ARGB%    GTR 1 (GOTO UnsupportedSwitch) ELSE IF %ARGB% == 0    (SET "BUILDTYPE=Build")
IF %ARGPL%   GTR 1 (GOTO UnsupportedSwitch) ELSE IF %ARGPL% == 0   (SET "BUILDPLATFORM=Both")
IF %ARGF%    GTR 1 (GOTO UnsupportedSwitch)
IF %ARGBC%   GTR 1 (GOTO UnsupportedSwitch) ELSE IF %ARGBC% == 0   (SET "BUILDCFG=Release")
IF %ARGZI%   GTR 1 (GOTO UnsupportedSwitch) ELSE IF %ARGZI% == 0   (SET "ZIP=False")
IF %ARGCOMP% GTR 1 (GOTO UnsupportedSwitch)

IF NOT EXIST "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" GOTO MissingVar

SET "PARAMS=-property installationPath -requires Microsoft.Component.MSBuild Microsoft.VisualStudio.Component.VC.ATLMFC Microsoft.VisualStudio.Component.VC.Tools.x86.x64"

IF /I "%COMPILER%" == "VS2019" (
  SET "PARAMS=%PARAMS% -version [16.0,17.0)"
) ELSE IF /I "%COMPILER%" == "VS2022" (
  SET "PARAMS=%PARAMS% -version [17.0,18.0)"
) ELSE IF /I "%COMPILER%" == "VS2026" (
  SET "PARAMS=%PARAMS% -version [18.0,19.0)"
) ELSE (
  SET "PARAMS=%PARAMS% -latest"
)

SET "VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" %PARAMS%"

FOR /f "delims=" %%A IN ('!VSWHERE!') DO SET "VCVARS=%%A\Common7\Tools\vsdevcmd.bat"

IF NOT EXIST "%VCVARS%" GOTO MissingVar

SET "BIN=_bin"

IF /I "%SIGN%" == "True" (
  IF NOT EXIST "%~dp0contrib\signinfo.txt" (
    ECHO WARNING: signinfo.txt not found.
    SET "SIGN=False"
  )
)

:Start
REM Check if the %LOG_DIR% folder exists otherwise MSBuild will fail
SET "LOG_DIR=%BIN%\logs"
IF NOT EXIST "%LOG_DIR%" MD "%LOG_DIR%"

CALL :SubDetectWinArch

SET "MSBUILD_SWITCHES=/nologo /consoleloggerparameters:Verbosity=minimal /maxcpucount /nodeReuse:true"

SET START_TIME=%TIME%
SET START_DATE=%DATE%

IF /I "%BUILDPLATFORM%" == "Win32" (GOTO Win32) ELSE IF /I "%BUILDPLATFORM%" == "x64" (GOTO x64)

:Win32
CALL "%VCVARS%" -arch=x86
REM again set the source directory (fix possible bug in VS2017)
CD /D %~dp0

CALL :SubFilters Win32
IF !ERRORLEVEL! NEQ 0 EXIT /B !ERRORLEVEL!
IF /I "%ZIP%" == "True" CALL :SubCreatePackages Win32

:x64
IF /I "%BUILDPLATFORM%" == "Win32" GOTO End

CALL "%VCVARS%" -arch=amd64
REM again set the source directory (fix possible bug in VS2017)
CD /D %~dp0

CALL :SubFilters x64
IF !ERRORLEVEL! NEQ 0 EXIT /B !ERRORLEVEL!
IF /I "%ZIP%" == "True" CALL :SubCreatePackages x64

:End
TITLE Compiling MPCVideoDec [FINISHED]
SET END_TIME=%TIME%
CALL :SubGetDuration
CALL :SubMsg "INFO" "Compilation started on %START_DATE%-%START_TIME% and completed on %DATE%-%END_TIME% [%DURATION%]"
ENDLOCAL
EXIT /B

:SubFilters
TITLE Compiling MPCVideoDec - %BUILDCFG% Filter^|%1...
MSBuild.exe MPCVideoDec.sln %MSBUILD_SWITCHES%^
 /target:%BUILDTYPE% /property:Configuration="%BUILDCFG% Filter";Platform=%1^
 /flp1:LogFile=%LOG_DIR%\filters_errors_%BUILDCFG%_%1.log;errorsonly;Verbosity=diagnostic^
 /flp2:LogFile=%LOG_DIR%\filters_warnings_%BUILDCFG%_%1.log;warningsonly;Verbosity=diagnostic
IF %ERRORLEVEL% NEQ 0 (
  CALL :SubMsg "ERROR" "MPCVideoDec.sln %BUILDCFG% Filter %1 - Compilation failed!"
  EXIT /B %ERRORLEVEL%
) ELSE (
  CALL :SubMsg "INFO" "MPCVideoDec.sln %BUILDCFG% Filter %1 compiled successfully"
)

IF /I "%1" == "Win32" (
  SET "DIR=%BIN%\Filters_x86"
) ELSE (
  SET "DIR=%BIN%\Filters_x64"
)

IF /I "%SIGN%" == "True" (
  CALL :SubSign %DIR% *.ax
)

EXIT /B

:SubSign
IF %ERRORLEVEL% NEQ 0 EXIT /B
REM %1 is a path
REM %2 is mask of the files to sign

PUSHD "%~1"

SET FILES=

FOR /F "delims=" %%A IN ('DIR "%2" /b') DO (
  IF "!FILES!" == "" (
    SET FILES=%%A
  ) ELSE (
    SET FILES=!FILES! %%A
  )
)
CALL "%~dp0contrib\sign.cmd" %FILES% || (CALL :SubMsg "ERROR" "Problem signing %FILES%" & GOTO Break)
CALL :SubMsg "INFO" "%2 signed successfully."

:Break
POPD
EXIT /B

:SubCreatePackages
CALL :SubDetectSevenzipPath
CALL :SubGetVersion

IF NOT DEFINED SEVENZIP (
  CALL :SubMsg "WARNING" "7-Zip wasn't found, the %1 package wasn't built"
  EXIT /B
)

SET "NAME=standalone_filters-mpc-be"
IF /I "%~1" == "Win32" (
  SET ARCH=x86
) ELSE (
  SET ARCH=x64
)

PUSHD "%BIN%"

SET PackagesOut=Packages

IF NOT EXIST "%PackagesOut%\%MPCBE_VER%" MD "%PackagesOut%\%MPCBE_VER%"

SET "PCKG_NAME=%NAME%.%MPCBE_VER%.%ARCH%"
SET "ZIP_NAME=%NAME%.%MPCBE_VER%%SUFFIX_GIT%.%ARCH%"

IF EXIST "%PackagesOut%\%MPCBE_VER%\%ZIP_NAME%.7z"     DEL "%PackagesOut%\%MPCBE_VER%\%ZIP_NAME%.7z"
IF EXIST "%PCKG_NAME%"        RD /Q /S "%PCKG_NAME%"

TITLE Copying %PCKG_NAME%...
IF NOT EXIST "%PCKG_NAME%" MD "%PCKG_NAME%"

COPY /Y /V "Filters_%ARCH%\*.ax"             "%PCKG_NAME%\*.ax" >NUL
COPY /Y /V "..\LICENSE.txt"                  "%PCKG_NAME%" >NUL
COPY /Y /V "..\Authors.txt"                  "%PCKG_NAME%" >NUL
COPY /Y /V "..\Authors mpc-hc team.txt"      "%PCKG_NAME%" >NUL
COPY /Y /V "..\README.md"                    "%PCKG_NAME%" >NUL

TITLE Creating archive %ZIP_NAME%.7z...
START "7z" /B /WAIT "%SEVENZIP%" a -t7z "%PackagesOut%\%MPCBE_VER%\%ZIP_NAME%.7z" "%PCKG_NAME%"^
 -m0=lzma -mx9 -mmt -ms=on
IF %ERRORLEVEL% NEQ 0 (
  CALL :SubMsg "ERROR" "Unable to create %ZIP_NAME%.7z!"
  EXIT /B %ERRORLEVEL%
)
CALL :SubMsg "INFO" "%ZIP_NAME%.7z successfully created"

IF EXIST "%PCKG_NAME%" RD /Q /S "%PCKG_NAME%"

POPD
EXIT /B

:SubGetVersion
REM Get the version
SET VerMajor=0
SET VerMinor=0
SET VerPatch=0
SET REVNUM=0

FOR /F "tokens=3,4 delims= " %%A IN (
  'FINDSTR /I /L /C:"define MPC_VERSION_MAJOR" "include\Version.h"') DO (SET "VerMajor=%%A")
FOR /F "tokens=3,4 delims= " %%A IN (
  'FINDSTR /I /L /C:"define MPC_VERSION_MINOR" "include\Version.h"') DO (SET "VerMinor=%%A")
FOR /F "tokens=3,4 delims= " %%A IN (
  'FINDSTR /I /L /C:"define MPC_VERSION_PATCH" "include\Version.h"') DO (SET "VerPatch=%%A")
FOR /F "tokens=3,4 delims= " %%A IN (
  'FINDSTR /I /L /C:"define MPC_VERSION_STATUS" "include\Version.h"') DO (SET "VERRELEASE=%%A")

FOR /F "tokens=3,4 delims= " %%A IN (
  'FINDSTR /I /L /C:"define REV_NUM" "revision.h"') DO (SET "REVNUM=%%A")
FOR /F "tokens=3,4 delims= " %%A IN (
  'FINDSTR /I /L /C:"define REV_DATE" "revision.h"') DO (SET "REVDATE=%%A")
FOR /F "tokens=3,4 delims= " %%A IN (
  'FINDSTR /I /L /C:"define REV_HASH" "revision.h"') DO (SET "REVHASH=%%A")

SET MPCBE_VER=%VerMajor%.%VerMinor%.%VerPatch%.%REVNUM%
SET "SUFFIX_GIT=_git%REVDATE%-%REVHASH%"

IF /I "%VERRELEASE%" == "1" (
  IF /I "%REVNUM%" == "0" (
    SET MPCBE_VER=%VerMajor%.%VerMinor%.%VerPatch%
  )
  SET "SUFFIX_GIT="
)


EXIT /B

:SubDetectWinArch
IF DEFINED PROGRAMFILES(x86) (SET x64_type=amd64) ELSE (SET x64_type=x86_amd64)
EXIT /B

:SubDetectSevenzipPath
IF EXIST "%PROGRAMFILES%\7za.exe" (SET "SEVENZIP=%PROGRAMFILES%\7za.exe" & EXIT /B)
IF EXIST "7za.exe"                (SET "SEVENZIP=7za.exe" & EXIT /B)

FOR %%A IN (7z.exe)  DO (SET "SEVENZIP_PATH=%%~$PATH:A")
IF EXIST "%SEVENZIP_PATH%" (SET "SEVENZIP=%SEVENZIP_PATH%" & EXIT /B)

FOR %%A IN (7za.exe) DO (SET "SEVENZIP_PATH=%%~$PATH:A")
IF EXIST "%SEVENZIP_PATH%" (SET "SEVENZIP=%SEVENZIP_PATH%" & EXIT /B)

IF /I "%x64_type%" == "amd64" (
  FOR /F "delims=" %%A IN (
    'REG QUERY "HKEY_LOCAL_MACHINE\SOFTWARE\Wow6432Node\7-Zip" /v "Path" 2^>Nul ^| FIND "REG_SZ"') DO (
    SET "SEVENZIP_REG=%%A" & CALL :SubSevenzipPath %%SEVENZIP_REG:*REG_SZ=%%
  )
)

FOR /F "delims=" %%A IN (
  'REG QUERY "HKEY_LOCAL_MACHINE\SOFTWARE\7-Zip" /v "Path" 2^>Nul ^| FIND "REG_SZ"') DO (
  SET "SEVENZIP_REG=%%A" & CALL :SubSevenzipPath %%SEVENZIP_REG:*REG_SZ=%%
)
EXIT /B

:ShowHelp
TITLE %~nx0 Help
ECHO.
ECHO Usage:
ECHO %~nx0 [Clean^|Build^|Rebuild] [x86^|x64^|Both] [Debug^|Release] [Zip] [Sign]
ECHO.
ECHO Notes: This fork only builds standalone filters (MPCVideoDec-fork.ax and
ECHO        MpcAudioRenderer-fork.ax) via the "Filter" configurations of MPCVideoDec.sln.
ECHO        The "Filters" keyword is still accepted for compatibility with existing
ECHO        scripts/CI but has no effect - filters are the only thing this script builds.
ECHO        You can also prefix the commands with "-", "--" or "/".
ECHO        The arguments are not case sensitive and can be ommitted.
ECHO. & ECHO.
ECHO Executing %~nx0 without any arguments will use the default ones:
ECHO "%~nx0 Build Both Release"
ECHO. & ECHO.
ECHO Examples:
ECHO %~nx0 x86           -Builds the x86 filters
ECHO %~nx0 x86 Debug     -Builds the x86 filters, Debug configuration
ECHO %~nx0 Zip           -Builds both x86 and x64 filters and creates a .7z package
ECHO %~nx0 Sign          -Builds both x86 and x64 filters and signs the output files
ECHO.
ENDLOCAL
EXIT /B

:MissingVar
COLOR 0C
TITLE Compiling MPCVideoDec [ERROR]
ECHO Not all build dependencies were found.
ECHO.
ECHO Make sure environments.bat defines MPCBE_MINGW/MPCBE_MSYS and that Visual Studio
ECHO with the MSBuild, ATL/MFC and VC++ x86/x64 components is installed.
ECHO. & ECHO.
ECHO Press any key to exit...
PAUSE >NUL
ENDLOCAL
EXIT /B

:UnsupportedSwitch
ECHO.
ECHO Unsupported commandline switch!
ECHO.
ECHO "build.bat %*"
ECHO.
ECHO Run "%~nx0 help" for details about the commandline switches.
CALL :SubMsg "ERROR" "Compilation failed!"
EXIT /B 1

:SubSevenzipPath
SET "SEVENZIP=%*\7z.exe"
EXIT /B

:SubMsg
ECHO. & ECHO ------------------------------
IF /I "%~1" == "ERROR" (
  CALL :SubColorText "0C" "[%~1]" & ECHO  %~2
) ELSE IF /I "%~1" == "INFO" (
  CALL :SubColorText "0A" "[%~1]" & ECHO  %~2
) ELSE IF /I "%~1" == "WARNING" (
  CALL :SubColorText "0E" "[%~1]" & ECHO  %~2
)
ECHO ------------------------------ & ECHO.
IF /I "%~1" == "ERROR" (
  IF /I "%Wait%" == "True" (
    ECHO Press any key to close this window...
    PAUSE >NUL
  )
  ENDLOCAL
  EXIT /B 1
) ELSE (
  EXIT /B
)

:SubColorText
FOR /F "tokens=1,2 delims=#" %%A IN (
  '"PROMPT #$H#$E# & ECHO ON & FOR %%B IN (1) DO REM"') DO (
  SET "DEL=%%A")
<NUL SET /p ".=%DEL%" > "%~2"
FINDSTR /v /a:%1 /R ".18" "%~2" NUL
DEL "%~2" > NUL 2>&1
EXIT /B

:SubGetDuration
SET START_TIME=%START_TIME: =%
SET END_TIME=%END_TIME: =%

FOR /F "tokens=1-4 delims=:.," %%A IN ("%START_TIME%") DO (
  SET /A "STARTTIME=(100%%A %% 100) * 360000 + (100%%B %% 100) * 6000 + (100%%C %% 100) * 100 + (100%%D %% 100)"
)

FOR /F "tokens=1-4 delims=:.," %%A IN ("%END_TIME%") DO (
  SET /A "ENDTIME=(100%%A %% 100) * 360000 + (100%%B %% 100) * 6000 + (100%%C %% 100) * 100 + (100%%D %% 100)"
)

SET /A DURATION=%ENDTIME%-%STARTTIME%
IF %ENDTIME% LSS %STARTTIME% SET /A "DURATION+=24 * 360000"

SET /A DURATIONH=%DURATION% / 360000
SET /A DURATIONM=(%DURATION% - %DURATIONH%*360000) / 6000
SET /A DURATIONS=(%DURATION% - %DURATIONH%*360000 - %DURATIONM%*6000) / 100
SET /A DURATIONHS=(%DURATION% - %DURATIONH%*360000 - %DURATIONM%*6000 - %DURATIONS%*100)*10

IF %DURATIONH%  EQU 0 (SET DURATIONH=)  ELSE (SET DURATIONH=%DURATIONH%h )
IF %DURATIONM%  EQU 0 (SET DURATIONM=)  ELSE (SET DURATIONM=%DURATIONM%m )
IF %DURATIONS%  EQU 0 (SET DURATIONS=)  ELSE (SET DURATIONS=%DURATIONS%s )
IF %DURATIONHS% EQU 0 (SET DURATIONHS=) ELSE (SET DURATIONHS=%DURATIONHS%ms)

SET "DURATION=%DURATIONH%%DURATIONM%%DURATIONS%%DURATIONHS%"
EXIT /B
