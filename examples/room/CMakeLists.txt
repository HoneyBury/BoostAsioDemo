add_executable(room_server room_server_main.cpp)
target_link_libraries(room_server PRIVATE project_game)
set_target_properties(room_server PROPERTIES FOLDER "legacy/v1")
