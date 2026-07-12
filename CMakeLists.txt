cmake_minimum_required(VERSION 3.25)

project(osupp
    VERSION 0.1.0
    DESCRIPTION "C++23 library for osu! .osu beatmap and .osr replay files"
    LANGUAGES CXX)

option(OSUPP_BUILD_TESTS "Build osupp unit tests" ${PROJECT_IS_TOP_LEVEL})
option(OSUPP_BUILD_EXAMPLES "Build osupp examples" ${PROJECT_IS_TOP_LEVEL})
option(OSUPP_WERROR "Treat warnings as errors" OFF)

find_package(LibLZMA REQUIRED)

function(osupp_apply_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
        if(OSUPP_WERROR)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(OSUPP_WERROR)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

add_library(osupp
    src/beatmap_parse.cpp
    src/beatmap_write.cpp
    src/replay_parse.cpp
    src/replay_write.cpp
    src/detail/lzma.cpp)
add_library(osupp::osupp ALIAS osupp)

target_compile_features(osupp PUBLIC cxx_std_23)
set_target_properties(osupp PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(osupp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(osupp PRIVATE LibLZMA::LibLZMA)
osupp_apply_warnings(osupp)

if(OSUPP_BUILD_TESTS)
    enable_testing()
    find_package(Catch2 3 CONFIG REQUIRED)
    add_executable(osupp_tests
        tests/beatmap_tests.cpp
        tests/replay_tests.cpp)
    target_link_libraries(osupp_tests PRIVATE osupp::osupp Catch2::Catch2WithMain)
    target_compile_definitions(osupp_tests
        PRIVATE OSUPP_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    osupp_apply_warnings(osupp_tests)
    include(Catch)
    catch_discover_tests(osupp_tests)
endif()

if(OSUPP_BUILD_EXAMPLES)
    add_executable(osupp_example examples/main.cpp)
    target_link_libraries(osupp_example PRIVATE osupp::osupp)
    osupp_apply_warnings(osupp_example)
endif()

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

install(TARGETS osupp EXPORT osupp-targets)
install(DIRECTORY include/osupp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT osupp-targets
    NAMESPACE osupp::
    DESTINATION ${CMAKE_INSTALL_DATADIR}/osupp)
configure_package_config_file(cmake/osupp-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/osupp-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_DATADIR}/osupp)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/osupp-config-version.cmake
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/osupp-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/osupp-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_DATADIR}/osupp)
