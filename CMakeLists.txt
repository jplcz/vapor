cmake_minimum_required(VERSION 3.14)
project(vapor_lib VERSION 1.0.0 LANGUAGES CXX)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(VAPOR_IS_TOP_LEVEL ON)
else()
    set(VAPOR_IS_TOP_LEVEL OFF)
endif()

add_library(vapor INTERFACE)
add_library(vapor::vapor ALIAS vapor)

target_compile_features(vapor INTERFACE cxx_std_11)

target_include_directories(vapor INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

option(VAPOR_USE_STL "Enable STL features and interoperability for vapor" ON)
option(VAPOR_BUILD_TESTS "Build test suites for vapor" ${VAPOR_IS_TOP_LEVEL})
option(VAPOR_ENABLE_INSTALL "Generate installation rules for vapor" ${VAPOR_IS_TOP_LEVEL})

if(NOT VAPOR_USE_STL)
    target_compile_definitions(vapor INTERFACE VAPOR_NO_STL)
endif()

if(VAPOR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(VAPOR_ENABLE_INSTALL)
    include(GNUInstallDirs)

    install(DIRECTORY include/vapor
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(TARGETS vapor EXPORT vapor-targets)

    install(EXPORT vapor-targets
            FILE vaporTargets.cmake
            NAMESPACE vapor::
            DESTINATION ${CMAKE_INSTALL_DATADIR}/vapor/cmake
    )
endif()
