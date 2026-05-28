add_executable(room_demo room_demo_main.cpp)
target_link_libraries(room_demo PRIVATE project_game)
set_target_properties(room_demo PROPERTIES FOLDER "legacy/showcase")
