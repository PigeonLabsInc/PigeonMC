cmake_minimum_required(VERSION 3.16)
project(MinecraftServer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler-specific optimization flags
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(MSVC)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /O2 /DNDEBUG")
    else()
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -DNDEBUG")
    endif()
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(MSVC)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Od /Zi")
    else()
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -g3 -fsanitize=address")
    endif()
endif()

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/asio")
    message(STATUS "ASIO not found, downloading...")
    file(DOWNLOAD
        "https://github.com/chriskohlhoff/asio/archive/refs/tags/asio-1-28-0.tar.gz"
        "${CMAKE_CURRENT_BINARY_DIR}/asio.tar.gz"
    )
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xzf asio.tar.gz
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
    file(RENAME 
        "${CMAKE_CURRENT_BINARY_DIR}/asio-asio-1-28-0"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/asio"
    )
endif()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json")
    message(STATUS "nlohmann/json not found, downloading...")
    file(DOWNLOAD
        "https://github.com/nlohmann/json/releases/download/v3.11.2/include.zip"
        "${CMAKE_CURRENT_BINARY_DIR}/json.zip"
    )
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xzf json.zip
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
    file(RENAME
        "${CMAKE_CURRENT_BINARY_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json"
    )
endif()

file(GLOB_RECURSE SOURCES
    "src/core/*.cpp"
    "src/network/*.cpp"
    "src/world/*.cpp"
    "src/player/*.cpp"
    "src/entity/*.cpp"
    "src/server/*.cpp"
    "src/main.cpp"
)

add_executable(minecraft_server ${SOURCES})

target_include_directories(minecraft_server PRIVATE
    src/
    third_party/asio/asio/include
    third_party/nlohmann_json/
)

target_link_libraries(minecraft_server PRIVATE
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
    ZLIB::ZLIB
)

target_compile_definitions(minecraft_server PRIVATE
    ASIO_STANDALONE
    ASIO_NO_DEPRECATED
    _WIN32_WINNT=0x0A00
)

# Compiler-specific warning and optimization flags
if(MSVC)
    target_compile_options(minecraft_server PRIVATE
        /W3                    # Warning level 3 (equivalent to -Wall -Wextra)
        /fp:fast              # Fast floating-point (equivalent to -ffast-math)
        /wd4996               # Disable deprecated warnings (equivalent to -Wno-unused-parameter)
    )
else()
    target_compile_options(minecraft_server PRIVATE
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
        -ffast-math
    )
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(MSVC)
        # MSVC doesn't have -s equivalent, but /INCREMENTAL:NO helps
        target_link_options(minecraft_server PRIVATE /INCREMENTAL:NO)
    else()
        target_link_options(minecraft_server PRIVATE -s)
    endif()
    set_target_properties(minecraft_server PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION TRUE
    )
endif()

if(CMAKE_EXPORT_COMPILE_COMMANDS)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

add_custom_target(run
    COMMAND minecraft_server
    DEPENDS minecraft_server
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

install(TARGETS minecraft_server
    RUNTIME DESTINATION bin
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/server.json")
    install(FILES server.json
        DESTINATION .
    )
endif()
