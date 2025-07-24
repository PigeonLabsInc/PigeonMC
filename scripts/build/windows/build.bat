@echo off
TITLE High Performance Minecraft Server Build Script
REM ------------------------------------------------------------------
REM Usage:
REM   build.bat [BUILD_TYPE] [JOBS]
REM   build.bat clean
REM   build.bat help
REM ------------------------------------------------------------------

REM --- Parse arguments ---
SET "CMD=%~1"
IF /I "%CMD%"=="clean" GOTO :clean
IF /I "%CMD%"=="help"  GOTO :help
IF /I "%CMD%"=="-h"    GOTO :help
IF /I "%CMD%"=="--help" GOTO :help

SET "BUILD_TYPE=%~1"
IF "%BUILD_TYPE%"=="" SET "BUILD_TYPE=Release"

SET "JOBS=%~2"
IF "%JOBS%"=="" SET "JOBS=%NUMBER_OF_PROCESSORS%"

ECHO === High Performance Minecraft Server Build Script ===
ECHO Build Type: %BUILD_TYPE%
ECHO Parallel Jobs: %JOBS%
ECHO.

GOTO :main

:check_dependencies
ECHO Checking dependencies...
WHERE cmake >NUL 2>&1
IF ERRORLEVEL 1 (
    ECHO Error: CMake is not installed
    EXIT /B 1
)
WHERE pkg-config >NUL 2>&1
IF ERRORLEVEL 1 (
    ECHO Warning: pkg-config not found, may have trouble finding dependencies
)
FOR /F "tokens=3" %%I IN ('cmake --version') DO (
    SET "CMAKE_VERSION=%%I"
    GOTO :after_cmake_ver
)
:after_cmake_ver
ECHO CMake version: %CMAKE_VERSION%
ECHO Warning: OpenSSL development libraries may not be installed
ECHO   Windows: install OpenSSL SDK if needed
ECHO Dependencies check completed
ECHO.
GOTO :eof

:setup_build_directory
ECHO Setting up build directory...
IF EXIST build (
    ECHO Cleaning existing build directory...
    RD /S /Q build
)
MKDIR build
PUSHD build
ECHO Build directory ready
ECHO.
GOTO :eof

:configure_cmake
ECHO Configuring CMake...
SET "CMAKE_OPTIONS=-DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DCMAKE_EXPORT_COMPILE_COMMANDS=ON"

IF /I "%BUILD_TYPE%"=="Release" (
    SET "CMAKE_OPTIONS=%CMAKE_OPTIONS% -DCMAKE_CXX_FLAGS_RELEASE=-O3 -march=native -DNDEBUG -flto -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON"
) ELSE IF /I "%BUILD_TYPE%"=="Debug" (
    SET "CMAKE_OPTIONS=%CMAKE_OPTIONS% -DCMAKE_CXX_FLAGS_DEBUG=-O0 -g3 -fsanitize=address -fsanitize=undefined -DCMAKE_LINKER_FLAGS_DEBUG=-fsanitize=address -fsanitize=undefined"
)

cmake .. %CMAKE_OPTIONS%
ECHO CMake configuration completed
ECHO.
GOTO :eof

:build_project
ECHO Building project...
ECHO Using %JOBS% parallel jobs
cmake --build . --config %BUILD_TYPE% -- /m:%JOBS%
ECHO Build completed successfully
ECHO.
GOTO :eof

:run_tests
IF /I "%BUILD_TYPE%"=="Debug" (
    IF EXIST test_runner.exe (
        ECHO Running tests...
        test_runner.exe
        ECHO Tests completed
        ECHO.
    )
)
GOTO :eof

:show_build_info
ECHO === Build Information ===
ECHO Executable: %CD%\minecraft_server.exe
IF EXIST minecraft_server.exe (
    FOR %%I IN (minecraft_server.exe) DO ECHO Binary size: %%~zI bytes
    ECHO Build type: %BUILD_TYPE%
)
ECHO.
ECHO To run the server: %CD%\minecraft_server.exe
ECHO Default configuration will be created on first run
ECHO.
GOTO :eof

:create_sample_config
POPD
IF NOT EXIST server.json (
    ECHO Creating sample configuration...
    (
      ECHO {
      ECHO   "server": {
      ECHO     "name": "High Performance Minecraft Server",
      ECHO     "motd": "A fast C++ Minecraft server",
      ECHO     "host": "0.0.0.0",
      ECHO     "port": 25565,
      ECHO     "max_players": 100,
      ECHO     "view_distance": 10,
      ECHO     "simulation_distance": 10,
      ECHO     "difficulty": "normal",
      ECHO     "gamemode": "survival",
      ECHO     "hardcore": false,
      ECHO     "pvp": true,
      ECHO     "online_mode": false,
      ECHO     "spawn_protection": 16
      ECHO   },
      ECHO   "world": {
      ECHO     "name": "world",
      ECHO     "seed": 0,
      ECHO     "generator": "flat",
      ECHO     "spawn_x": 0,
      ECHO     "spawn_y": 65,
      ECHO     "spawn_z": 0
      ECHO   },
      ECHO   "performance": {
      ECHO     "io_threads": 4,
      ECHO     "worker_threads": 0,
      ECHO     "max_chunks_loaded": 1000,
      ECHO     "chunk_unload_timeout": 300000,
      ECHO     "auto_save_interval": 300000,
      ECHO     "compression_threshold": 256,
      ECHO     "network_buffer_size": 8192
      ECHO   },
      ECHO   "logging": {
      ECHO     "level": "info",
      ECHO     "file": "server.log",
      ECHO     "console": true,
      ECHO     "max_file_size": 10485760,
      ECHO     "max_files": 5
      ECHO   },
      ECHO   "security": {
      ECHO     "ip_forwarding": false,
      ECHO     "max_connections_per_ip": 3,
      ECHO     "connection_throttle": 4000,
      ECHO     "packet_limit_per_second": 500
      ECHO   }
      ECHO }
    ) > server.json
    ECHO Sample configuration created: server.json
)
PUSHD build
GOTO :eof

:main
CALL :check_dependencies
CALL :setup_build_directory
CALL :configure_cmake
CALL :build_project
CALL :run_tests
CALL :create_sample_config
CALL :show_build_info

ECHO === Build completed successfully! ===
ECHO Run "%CD%\minecraft_server.exe" to start the server
GOTO :eof

:clean
ECHO Cleaning build directory...
RD /S /Q build
ECHO Clean completed
GOTO :eof

:help
ECHO Usage: build.bat [BUILD_TYPE] [JOBS]
ECHO.
ECHO BUILD_TYPE:
ECHO   Release  - Optimized build (default)
ECHO   Debug    - Debug build with sanitizers
ECHO.
ECHO JOBS:
ECHO   Number of parallel build jobs (default: number of CPU cores)
ECHO.
ECHO Commands:
ECHO   clean    - Remove build directory
ECHO   help     - Show this help message
ECHO.
ECHO Examples:
ECHO   build.bat
ECHO   build.bat Debug
ECHO   build.bat Release 8
EXIT /B 0
