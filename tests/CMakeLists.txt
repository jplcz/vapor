include(FetchContent)
FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.17.0
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

set(CPP_STANDARDS 11 14 17 23 26)

include(GoogleTest)

foreach (STD ${CPP_STANDARDS})
    if (STD LESS_EQUAL CMAKE_CXX_STANDARD_LATEST)
        set(TEST_TARGET run_tests_cpp${STD})

        add_executable(${TEST_TARGET} main_test.cpp
                test_expected.cpp
                test_string_view.cpp
                test_span.cpp
                test_string.cpp
                test_vector.cpp)

        target_compile_features(${TEST_TARGET} PRIVATE cxx_std_${STD})

        target_link_libraries(${TEST_TARGET} PRIVATE
                vapor::vapor
                gtest_main
        )

        gtest_discover_tests(${TEST_TARGET} TEST_PREFIX "cpp${STD}.")
    else ()
        message(STATUS "Skipping C++${STD} test suite: Compiler latest support is C++${CMAKE_CXX_STANDARD_LATEST}")
    endif ()
endforeach ()