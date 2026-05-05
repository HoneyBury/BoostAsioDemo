add_executable(hello_tests
    hello_test.cpp
)

target_link_libraries(hello_tests
    PRIVATE
        hello_lib
        GTest::gtest_main
)

apply_project_warnings(hello_tests)

include(GoogleTest)
gtest_discover_tests(hello_tests)
