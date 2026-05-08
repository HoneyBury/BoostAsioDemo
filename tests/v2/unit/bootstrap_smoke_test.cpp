#include <gtest/gtest.h>

#include <string_view>

#include "v2/common/module_marker.h"

TEST(V2BootstrapSmokeTest, ModuleMarkerIsAvailable) {
    EXPECT_EQ(std::string_view(v2::common::module_marker()), "v2-bootstrap");
}
