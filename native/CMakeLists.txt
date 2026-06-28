cmake_minimum_required(VERSION 3.24)

project(awfan VERSION 1.1.4 LANGUAGES CXX)

set(AWFAN_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${AWFAN_GENERATED_DIR}")

foreach(source_name IN ITEMS frontend_main cli_main)
    file(READ
        "${CMAKE_CURRENT_SOURCE_DIR}/src/${source_name}.cpp"
        source_text
    )
    string(REGEX REPLACE
        "constexpr wchar_t kVersion\\[\\] = L\"[0-9]+\\.[0-9]+\\.[0-9]+\";"
        "constexpr wchar_t kVersion[] = L\"${PROJECT_VERSION}\";"
        source_text
        "${source_text}"
    )
    file(WRITE
        "${AWFAN_GENERATED_DIR}/${source_name}.cpp"
        "${source_text}"
    )
endforeach()

add_executable(awfan
    "${AWFAN_GENERATED_DIR}/frontend_main.cpp"
    src/broker.cpp
    src/update.cpp
)

add_executable(awfan-core
    "${AWFAN_GENERATED_DIR}/cli_main.cpp"
    src/update.cpp
    src/native_cli_release.cpp
    src/wmi_probe.cpp
    src/awcc_inspect.cpp
    src/awcc_exact.cpp
    src/awcc_raw_v3.cpp
)

add_executable(awfan-broker WIN32
    src/broker_main.cpp
    src/broker.cpp
)

foreach(target awfan awfan-core awfan-broker)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_include_directories(${target} PRIVATE include)
    target_compile_definitions(${target} PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endforeach()

target_link_libraries(awfan PRIVATE advapi32)
target_link_libraries(awfan-core PRIVATE wbemuuid ole32 oleaut32)
target_link_libraries(awfan-broker PRIVATE advapi32)

if(MSVC)
    foreach(target awfan awfan-core awfan-broker)
        set_property(TARGET ${target} PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        )
        target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc /utf-8)
    endforeach()
endif()

install(TARGETS awfan awfan-core awfan-broker RUNTIME DESTINATION .)
install(FILES
    package/install.ps1
    package/update.ps1
    package/uninstall.ps1
    package/awfan-completion.ps1
    package/README.txt
    package/CHANGELOG.txt
    package/THIRD-PARTY-NOTICES.txt
    package/LICENSE.txt
    DESTINATION .
)

set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_NAME "awfan")
set(CPACK_PACKAGE_VENDOR "Gio1112")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "awfan-${PROJECT_VERSION}-windows-x64")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
include(CPack)
