add_executable(login_demo login_demo_main.cpp)
target_link_libraries(login_demo PRIVATE project_game)
set_target_properties(login_demo PROPERTIES FOLDER "legacy/showcase")
