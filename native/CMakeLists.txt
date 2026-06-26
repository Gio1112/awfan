cmake_minimum_required(VERSION 3.24)

project(awfan VERSION 1.0.0 LANGUAGES CXX)

add_executable(awfan
    src/main.cpp
    src/native_cli_release.cpp
    src/wmi_probe.cpp
    src/awcc_inspect.cpp
    src/awcc_exact.cpp
    src/awcc_raw_v3.cpp
)

target_compile_features(awfan PRIVATE cxx_std_20)
target_include_directories(awfan PRIVATE include)
target_compile_definitions(awfan PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)
target_link_libraries(awfan PRIVATE wbemuuid ole32 oleaut32)

if(MSVC)
    set_property(TARGET awfan PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    target_compile_options(awfan PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()

install(TARGETS awfan RUNTIME DESTINATION .)
install(FILES
    package/install.ps1
    package/uninstall.ps1
    package/awfan-completion.ps1
    package/README.txt
    DESTINATION .
)

set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_NAME "awfan")
set(CPACK_PACKAGE_VENDOR "Gio1112")
set(CPACK_PACKAGE_VERSION "1.0.0-rc1")
set(CPACK_PACKAGE_FILE_NAME "awfan-1.0.0-rc1-windows-x64")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
include(CPack)
