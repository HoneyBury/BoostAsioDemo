add_library(project_v2 STATIC
    common/module_marker.cpp
)
target_include_directories(project_v2
    PUBLIC
        "${CMAKE_SOURCE_DIR}/include"
)
target_link_libraries(project_v2
    PUBLIC
        project_app
)
apply_project_warnings(project_v2)
