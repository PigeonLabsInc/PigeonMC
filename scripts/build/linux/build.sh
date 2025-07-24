#!/bin/bash

set -e

PROJECT_NAME="MinecraftServer"
BUILD_TYPE="${1:-Release}"
JOBS="${2:-$(nproc)}"

echo "=== High Performance Minecraft Server Build Script ==="
echo "Build Type: $BUILD_TYPE"
echo "Parallel Jobs: $JOBS"
echo

check_dependencies() {
    echo "Checking dependencies..."
    
    if ! command -v cmake &> /dev/null; then
        echo "Error: CMake is not installed"
        exit 1
    fi
    
    if ! command -v pkg-config &> /dev/null; then
        echo "Warning: pkg-config not found, may have trouble finding dependencies"
    fi
    
    CMAKE_VERSION=$(cmake --version | head -n1 | awk '{print $3}')
    echo "CMake version: $CMAKE_VERSION"
    
    if ! dpkg -l | grep -q libssl-dev && ! rpm -q openssl-devel &> /dev/null; then
        echo "Warning: OpenSSL development libraries may not be installed"
        echo "  Ubuntu/Debian: sudo apt install libssl-dev zlib1g-dev"
        echo "  CentOS/RHEL: sudo yum install openssl-devel zlib-devel"
    fi
    
    echo "Dependencies check completed"
    echo
}

setup_build_directory() {
    echo "Setting up build directory..."
    
    if [ -d "build" ]; then
        echo "Cleaning existing build directory..."
        rm -rf build
    fi
    
    mkdir -p build
    cd build
    
    echo "Build directory ready"
    echo
}

configure_cmake() {
    echo "Configuring CMake..."
    
    CMAKE_OPTIONS=(
        "-DCMAKE_BUILD_TYPE=$BUILD_TYPE"
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
    )
    
    if [ "$BUILD_TYPE" = "Release" ]; then
        CMAKE_OPTIONS+=(
            "-DCMAKE_CXX_FLAGS_RELEASE=-O3 -march=native -DNDEBUG -flto"
            "-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON"
        )
    elif [ "$BUILD_TYPE" = "Debug" ]; then
        CMAKE_OPTIONS+=(
            "-DCMAKE_CXX_FLAGS_DEBUG=-O0 -g3 -fsanitize=address -fsanitize=undefined"
            "-DCMAKE_LINKER_FLAGS_DEBUG=-fsanitize=address -fsanitize=undefined"
        )
    fi
    
    cmake .. "${CMAKE_OPTIONS[@]}"
    
    echo "CMake configuration completed"
    echo
}

build_project() {
    echo "Building project..."
    echo "Using $JOBS parallel jobs"
    
    cmake --build . --config "$BUILD_TYPE" -j "$JOBS"
    
    echo "Build completed successfully"
    echo
}

run_tests() {
    if [ "$BUILD_TYPE" = "Debug" ] && [ -f "test_runner" ]; then
        echo "Running tests..."
        ./test_runner
        echo "Tests completed"
        echo
    fi
}

show_build_info() {
    echo "=== Build Information ==="
    echo "Executable: $(pwd)/minecraft_server"
    
    if [ -f "minecraft_server" ]; then
        echo "Binary size: $(du -h minecraft_server | cut -f1)"
        echo "Build type: $BUILD_TYPE"
        
        if command -v ldd &> /dev/null; then
            echo "Dependencies:"
            ldd minecraft_server | grep -E "(ssl|crypto|z\.so)" || echo "  No external SSL/zlib dependencies found"
        fi
    fi
    
    echo
    echo "To run the server: ./build/minecraft_server"
    echo "Default configuration will be created on first run"
    echo
}

create_sample_config() {
    cd ..
    
    if [ ! -f "server.json" ]; then
        echo "Creating sample configuration..."
        cat > server.json << 'EOF'
{
    "server": {
        "name": "High Performance Minecraft Server",
        "motd": "A fast C++ Minecraft server",
        "host": "0.0.0.0",
        "port": 25565,
        "max_players": 100,
        "view_distance": 10,
        "simulation_distance": 10,
        "difficulty": "normal",
        "gamemode": "survival",
        "hardcore": false,
        "pvp": true,
        "online_mode": false,
        "spawn_protection": 16
    },
    "world": {
        "name": "world",
        "seed": 0,
        "generator": "flat",
        "spawn_x": 0,
        "spawn_y": 65,
        "spawn_z": 0
    },
    "performance": {
        "io_threads": 4,
        "worker_threads": 0,
        "max_chunks_loaded": 1000,
        "chunk_unload_timeout": 300000,
        "auto_save_interval": 300000,
        "compression_threshold": 256,
        "network_buffer_size": 8192
    },
    "logging": {
        "level": "info",
        "file": "server.log",
        "console": true,
        "max_file_size": 10485760,
        "max_files": 5
    },
    "security": {
        "ip_forwarding": false,
        "max_connections_per_ip": 3,
        "connection_throttle": 4000,
        "packet_limit_per_second": 500
    }
}
EOF
        echo "Sample configuration created: server.json"
    fi
    
    cd build
}

main() {
    echo "Starting build process..."
    echo
    
    check_dependencies
    setup_build_directory
    configure_cmake
    build_project
    run_tests
    create_sample_config
    show_build_info
    
    echo "=== Build completed successfully! ==="
    echo "Run './build/minecraft_server' to start the server"
}

case "$1" in
    "clean")
        echo "Cleaning build directory..."
        rm -rf build
        echo "Clean completed"
        ;;
    "help"|"-h"|"--help")
        echo "Usage: $0 [BUILD_TYPE] [JOBS]"
        echo
        echo "BUILD_TYPE:"
        echo "  Release  - Optimized build (default)"
        echo "  Debug    - Debug build with sanitizers"
        echo
        echo "JOBS:"
        echo "  Number of parallel build jobs (default: number of CPU cores)"
        echo
        echo "Examples:"
        echo "  $0                    # Release build with auto jobs"
        echo "  $0 Debug              # Debug build"
        echo "  $0 Release 8          # Release build with 8 jobs"
        echo "  $0 clean              # Clean build directory"
        ;;
    *)
        main
        ;;
esac
