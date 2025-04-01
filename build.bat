@echo off
setlocal enabledelayedexpansion

if "%AEROSIM_SIMULINK_ROOT%" == "" (
    echo AEROSIM_SIMULINK_ROOT is not set. Please set it to the root directory of the Aerosim Simulink project.
    exit /b 1
)

echo.
echo Building dependencies for Aerosim Simulink S-functions...

@REM Build the librdkafka dependency
set BUILD_LIBRDKAFKA_REQUIRED=0
if not exist %AEROSIM_SIMULINK_ROOT%\matlab-apache-kafka\Software\MATLAB\app\sfun\rdkafka.dll set BUILD_LIBRDKAFKA_REQUIRED=1
if not exist %AEROSIM_SIMULINK_ROOT%\matlab-apache-kafka\Software\MATLAB\app\sfun\lib\rdkafka.lib set BUILD_LIBRDKAFKA_REQUIRED=1
if !BUILD_LIBRDKAFKA_REQUIRED! == 1 (
    echo.
    echo ----- Building librdkafka -----
    pushd %AEROSIM_SIMULINK_ROOT%\matlab-apache-kafka\Software\CPP\librdkafka
    if not exist build.win64 mkdir build.win64
    pushd build.win64
    cmake -LH -G "Visual Studio 17 2022" ..
    msbuild -verbosity:minimal RdKafka.sln /p:Configuration=Release
    xcopy /F /Y %AEROSIM_SIMULINK_ROOT%\matlab-apache-kafka\Software\CPP\librdkafka\build.win64\src\Release\rdkafka.dll %AEROSIM_SIMULINK_ROOT%\matlab-apache-kafka\Software\MATLAB\app\sfun
    xcopy /F /Y %AEROSIM_SIMULINK_ROOT%\matlab-apache-kafka\Software\CPP\librdkafka\build.win64\src\Release\rdkafka.lib %AEROSIM_SIMULINK_ROOT%\matlab-apache-kafka\Software\MATLAB\app\sfun\lib
    popd
    popd
) else (
    echo librdkafka is already built. Skipping...
)

@REM Build the jansson dependency
if not exist %AEROSIM_SIMULINK_ROOT%\matlab-apache-kafka\Software\MATLAB\app\sfun\lib\jansson.lib (
    echo.
    echo ----- Building jansson -----
    pushd %AEROSIM_SIMULINK_ROOT%\matlab-apache-kafka\Software\CPP\jansson
    if not exist build.win64 mkdir build.win64
    pushd build.win64
    cmake -LH -G "Visual Studio 17 2022" ..
    msbuild -verbosity:minimal jansson.sln /p:Configuration=Release
    xcopy /F /Y %AEROSIM_SIMULINK_ROOT%\matlab-apache-kafka\Software\CPP\jansson\build.win64\lib\Release\jansson.lib %AEROSIM_SIMULINK_ROOT%\matlab-apache-kafka\Software\MATLAB\app\sfun\lib
    popd
    popd
) else (
    echo jansson is already built. Skipping...
)

@REM Build the AeroSim Simulink S-function MEX files
echo.
echo ----- Building AeroSim Simulink S-function MEX files -----
echo Running MATLAB in batch mode (no GUI) to build the S-functions...
matlab.exe -wait -batch "run('%AEROSIM_SIMULINK_ROOT%\matlab-apache-kafka\Software\MATLAB\startup.m');run('%AEROSIM_SIMULINK_ROOT%\aerosim-sfunctions\build_aerosim_sfuns.m');exit;"
