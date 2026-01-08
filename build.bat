@echo off
setlocal

echo ==========================================
echo       ChatServer Auto Build Script
echo ==========================================

REM 如果存在 build 文件夹，先删除 (Clean)
if exist build (
    echo [INFO] Removing old build directory...
    rmdir /s /q build
)

REM 创建 build 文件夹
echo [INFO] Creating new build directory...
mkdir build

REM 进入 build 目录
cd build

REM 执行 CMake 配置 (Release 模式)
echo [INFO] Configuring CMake (Release)...
cmake .. -DCMAKE_BUILD_TYPE=Release

REM 检查配置是否成功，失败则暂停并退出
if %errorlevel% neq 0 (
    echo [ERROR] CMake configuration failed!
    cd ..
    pause
    exit /b %errorlevel%
)

REM 执行编译
echo [INFO] Building project...
cmake --build . --config Release

REM 检查编译是否成功
if %errorlevel% neq 0 (
    echo [ERROR] Build failed!
    cd ..
    pause
    exit /b %errorlevel%
)

REM 返回上级目录
cd ..

echo ==========================================
echo          Build Finished Successfully
echo ==========================================
pause