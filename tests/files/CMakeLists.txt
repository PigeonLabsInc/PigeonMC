cmake_minimum_required(VERSION 3.16)
project(MinecraftServer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -DNDEBUG")
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -g3 -fsanitize=address")
endif()

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)

add_subdirectory(third_party/asio)
add_subdirectory(third_party/nlohmann_json)

file(GLOB_RECURSE SOURCES
    "src/*.cpp"
    "src/*.hpp"
)

add_executable(minecraft_server ${SOURCES})

target_include_directories(minecraft_server PRIVATE
    src/
    third_party/asio/asio/include
    third_party/nlohmann_json/include
)

target_link_libraries(minecraft_server PRIVATE
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
    ZLIB::ZLIB
)

target_compile_options(minecraft_server PRIVATE
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
    -ffast-math
)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_link_options(minecraft_server PRIVATE -s)
endif()
