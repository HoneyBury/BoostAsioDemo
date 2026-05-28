add_executable(login_server login_server_main.cpp)
target_link_libraries(login_server PRIVATE project_game)
set_target_properties(login_server PROPERTIES FOLDER "legacy/v1")
