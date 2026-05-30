set(PROJECT_GTEST_DISCOVERY_TIMEOUT 30)

add_subdirectory(v2)

option(BOOST_BUILD_PERF_TESTS "Build performance benchmark tests" OFF)
option(BOOST_BUILD_CHAOS "Build legacy v1 chaos tests" OFF)
if(BOOST_BUILD_PERF_TESTS)
    add_subdirectory(perf)
endif()

if(BOOST_BUILD_V1_LEGACY_TESTS)
    add_subdirectory(unit)
    add_subdirectory(integration)
endif()

if(BOOST_BUILD_CHAOS)
    add_subdirectory(chaos)
endif()
