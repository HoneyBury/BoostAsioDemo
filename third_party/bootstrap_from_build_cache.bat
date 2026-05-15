@echo off
REM Copy dependency source trees from an existing build/_deps cache into
REM third_party/*-src so future configure steps can run offline.

setlocal enabledelayedexpansion
cd /d "%~dp0"

set "CACHE_DIR=%~1"
if "%CACHE_DIR%"=="" set "CACHE_DIR=%cd%\..\build\_deps"

if not exist "%CACHE_DIR%" (
    echo ERROR: cache directory not found: %CACHE_DIR%
    exit /b 1
)

echo === Bootstrapping third_party source cache from %CACHE_DIR% ===

call :copy_dep fmt
call :copy_dep googletest
call :copy_dep spdlog
call :copy_dep nlohmann_json
call :copy_dep boost
call :copy_dep hiredis

echo.
echo === Done ===
echo CMake will now prefer third_party\*-src before remote fetches.
exit /b 0

:copy_dep
set "NAME=%~1"
set "SRC=%CACHE_DIR%\%NAME%-src"
set "DST=%cd%\%NAME%-src"

if not exist "%SRC%" (
    echo [skip] %NAME%-src not found in cache
    exit /b 0
)

if exist "%DST%" (
    echo [skip] %NAME%-src already exists in third_party
    exit /b 0
)

echo [copy] %NAME%-src
robocopy "%SRC%" "%DST%" /E >nul
if errorlevel 8 (
    echo ERROR: robocopy failed for %NAME%-src
    exit /b 1
)
exit /b 0
