add_executable(admin_demo admin_demo_main.cpp)
target_link_libraries(admin_demo PRIVATE project_game)
set_target_properties(admin_demo PROPERTIES FOLDER "legacy/showcase")
