include(FetchContent)
FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.17.0
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

set(CPP_STANDARDS 11 14 17 23 26)

file(GLOB VAPOR_HEADER_PATHS RELATIVE ${PROJECT_SOURCE_DIR}/include/vapor
        ${PROJECT_SOURCE_DIR}/include/vapor/*.hpp)
set(VAPOR_HEADERS ${VAPOR_HEADER_PATHS})

include(GoogleTest)

foreach (STD ${CPP_STANDARDS})
    if (STD LESS_EQUAL CMAKE_CXX_STANDARD_LATEST)
        set(TEST_TARGET run_tests_cpp${STD})

        add_executable(${TEST_TARGET} main_test.cpp
                test_expected.cpp
                test_string_view.cpp
                test_span.cpp
                test_string.cpp
                test_functional.cpp
                test_type_traits.cpp
                test_hardening.cpp
                test_vector.cpp
                test_inline_vector.cpp
                test_array.cpp
                test_optional.cpp
                test_unique_ptr.cpp
                test_tuple.cpp
                test_function_ref.cpp
                test_algorithm.cpp
                test_bitset.cpp
                test_bit.cpp
                test_hash.cpp
                test_iterator.cpp
                test_bit_view.cpp
                test_heap.cpp
                test_atomic.cpp
                test_variant.cpp
                test_algorithm_more.cpp
                test_scope_guard.cpp
                test_small_string.cpp
                test_string_util.cpp
                test_string_builder.cpp
                test_shared_ptr.cpp
                test_hash_map.cpp
                test_flat_map.cpp
                test_containers_r4.cpp
                test_utilities_r4.cpp
                test_containers_r5.cpp
                test_io_scheduler.cpp)

        target_compile_features(${TEST_TARGET} PRIVATE cxx_std_${STD})

        # Every public header must compile on its own under strict warnings.
        set(HEADER_CHECK_SOURCES "")
        foreach (HDR ${VAPOR_HEADERS})
            set(HDR_SRC ${CMAKE_CURRENT_BINARY_DIR}/header_check_cpp${STD}/${HDR}.cpp)
            file(WRITE ${HDR_SRC} "#include <vapor/${HDR}>\n")
            list(APPEND HEADER_CHECK_SOURCES ${HDR_SRC})
        endforeach ()
        add_library(header_check_cpp${STD} OBJECT ${HEADER_CHECK_SOURCES})
        target_compile_features(header_check_cpp${STD} PRIVATE cxx_std_${STD})
        target_link_libraries(header_check_cpp${STD} PRIVATE vapor::vapor)
        target_compile_options(header_check_cpp${STD} PRIVATE ${VAPOR_STRICT_WARNINGS})
        if (NOT MSVC)
            # vapor must not depend on any C++ standard library header by default.
            target_compile_options(header_check_cpp${STD} PRIVATE -nostdinc++)
        endif ()

        target_compile_options(${TEST_TARGET} PRIVATE ${VAPOR_STRICT_WARNINGS})
        # The tests exercise the optional std interoperability layer.
        target_compile_definitions(${TEST_TARGET} PRIVATE VAPOR_USE_STL=1)

        target_link_libraries(${TEST_TARGET} PRIVATE
                vapor::vapor
                gtest_main
        )
        find_package(Threads REQUIRED)
        target_link_libraries(${TEST_TARGET} PRIVATE Threads::Threads)

        gtest_discover_tests(${TEST_TARGET} TEST_PREFIX "cpp${STD}.")
    else ()
        message(STATUS "Skipping C++${STD} test suite: Compiler latest support is C++${CMAKE_CXX_STANDARD_LATEST}")
    endif ()
endforeach ()