add_executable(gateway_pressure
    gateway_pressure_main.cpp
)
target_link_libraries(gateway_pressure
    PRIVATE
        project_game
)
apply_project_warnings(gateway_pressure)
set_target_properties(gateway_pressure PROPERTIES FOLDER "legacy/v1")
