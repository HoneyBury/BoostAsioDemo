add_executable(battle_server battle_server_main.cpp)
target_link_libraries(battle_server PRIVATE project_game)
set_target_properties(battle_server PROPERTIES FOLDER "legacy/v1")
