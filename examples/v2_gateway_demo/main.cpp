#include <fmt/core.h>

#include "v2/common/module_marker.h"

int main() {
    fmt::print("v2 gateway demo bootstrap: {}\n", v2::common::module_marker());
    return 0;
}
