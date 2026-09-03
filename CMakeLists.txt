cmake_minimum_required(VERSION 3.15)
project(LucentSAPI VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(BUILD_X64 TRUE)
    message(STATUS "Building x64 targets")
else()
    set(BUILD_X64 FALSE)
    message(STATUS "Building x86 targets")
endif()

add_compile_definitions(_CRT_SECURE_NO_WARNINGS UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
if(MSVC)
    add_compile_options(/W3 /MP /EHsc /utf-8)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Engine client library shared by every component.
add_library(lucent_core STATIC
    src/lucent_engine.cpp
    src/lucent_settings.cpp
)
target_include_directories(lucent_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(lucent_core PUBLIC advapi32)

# SAPI 5 engine DLL (built for both architectures from the same sources).
add_library(LucentSAPI SHARED
    src/sapi_main.cpp
    src/com.cpp
    src/registry.cpp
    src/ISpDataKeyImpl.cpp
    src/IEnumSpObjectTokensImpl.cpp
    src/ISpTTSEngineImpl.cpp
    src/voice_token.cpp
    src/lucent_sapi.def
)
target_link_libraries(LucentSAPI PRIVATE lucent_core ole32 oleaut32 advapi32 shell32)
if(BUILD_X64)
    target_compile_definitions(LucentSAPI PRIVATE BUILD_X64)
endif()

# Direct engine test (no SAPI).
add_executable(engine_test test/engine_test.cpp)
target_link_libraries(engine_test PRIVATE lucent_core)

# End-to-end SAPI test.
add_executable(sapi_test test/sapi_test.cpp)
target_link_libraries(sapi_test PRIVATE ole32 oleaut32 advapi32 sapi)

# Configuration utility (x86 build is the one shipped; x64 builds too for testing).
add_executable(LucentConfig WIN32
    src/config_main.cpp
    src/config.rc
)
target_link_libraries(LucentConfig PRIVATE lucent_core comctl32 ole32 oleaut32 sapi shell32 user32 gdi32)
if(MSVC)
    target_link_options(LucentConfig PRIVATE "/MANIFESTDEPENDENCY:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'")
endif()

# Accessibility checker for the configuration dialog.
add_executable(a11y_dump test/a11y_dump.cpp)
target_link_libraries(a11y_dump PRIVATE oleacc ole32 oleaut32 user32)
