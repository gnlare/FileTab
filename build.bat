@echo off
rem FileTab 빌드 (Visual Studio 2022 이상 또는 CMake 필요). x64 Release 로 빌드해 build\Release\FileTab.exe 를 만든다.
setlocal enabledelayedexpansion
cd /d "%~dp0"

where cmake >nul 2>nul
if %ERRORLEVEL% neq 0 (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "!VSWHERE!" (
        for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -products * -property installationPath`) do (
            if exist "%%i\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
                set "PATH=%%i\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;!PATH!"
            )
        )
    )
    if exist "%ProgramFiles%\CMake\bin\cmake.exe" (
        set "PATH=%ProgramFiles%\CMake\bin;!PATH!"
    )
)

where cmake >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo CMake를 찾을 수 없습니다. Visual Studio의 C++ CMake 도구 또는 CMake를 설치하고 PATH를 확인해주세요.
    goto :fail
)

cmake -S . -B build -A x64 || goto :fail
cmake --build build --config Release || goto :fail
echo.
echo 완료: %~dp0build\Release\FileTab.exe
goto :eof
:fail
echo 빌드에 실패했습니다.
exit /b 1
