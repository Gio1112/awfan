cmake_minimum_required(VERSION 3.24)

project(awfan_native VERSION 0.3.5 LANGUAGES CXX)

add_executable(awfan-native
    src/main.cpp
    src/wmi_probe.cpp
    src/awcc_inspect.cpp
    src/awcc_read_v2.cpp
    src/awcc_exact.cpp
    src/awcc_raw_v3.cpp
)

target_compile_features(awfan-native PRIVATE cxx_std_20)
target_include_directories(awfan-native PRIVATE include)
target_compile_definitions(awfan-native PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)
target_link_libraries(awfan-native PRIVATE wbemuuid ole32 oleaut32)

if(MSVC)
    set_property(TARGET awfan-native PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    target_compile_options(awfan-native PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()

install(TARGETS awfan-native RUNTIME DESTINATION bin)
