add_executable(battle_demo battle_demo_main.cpp)
target_link_libraries(battle_demo PRIVATE project_game)
set_target_properties(battle_demo PROPERTIES FOLDER "legacy/showcase")
