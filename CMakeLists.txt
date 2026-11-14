cmake_minimum_required(VERSION 3.24)
project(unity_lobby_server VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
# cpp-httplib 0.66.0 and nlohmann/json 3.12.0.
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZSTD_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_INSTALL OFF CACHE BOOL "" FORCE)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_Declare(httplib
    URL https://github.com/yhirose/cpp-httplib/archive/cdcd2ac6e6d5548b5a9130fb39ab76d2c6c614b0.tar.gz
    URL_HASH SHA256=41751bec53719663a2136d00e11c6a3d384c5af71da0d8d6fd731cab10f0ce08
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(httplib json)

find_package(Threads REQUIRED)
add_library(lobby src/lobby.cpp)
target_include_directories(lobby PUBLIC src)
target_link_libraries(lobby PUBLIC Threads::Threads)
if(WIN32)
    target_link_libraries(lobby PRIVATE bcrypt)
endif()

add_executable(lobby-server src/main.cpp)
target_link_libraries(lobby-server PRIVATE lobby httplib::httplib nlohmann_json::nlohmann_json)
if(MSVC)
    target_compile_options(lobby PRIVATE /W4 /permissive-)
    target_compile_options(lobby-server PRIVATE /W4 /permissive-)
else()
    target_compile_options(lobby PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(lobby-server PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(lobby-tests tests/lobby_tests.cpp)
    target_link_libraries(lobby-tests PRIVATE lobby)
    add_test(NAME lobby-rules COMMAND lobby-tests)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME http-api
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/http_tests.py $<TARGET_FILE:lobby-server>)
    set_tests_properties(http-api PROPERTIES TIMEOUT 30)
endif()
