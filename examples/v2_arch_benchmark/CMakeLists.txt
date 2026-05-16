add_executable(v2_arch_benchmark
    main.cpp
)
target_link_libraries(v2_arch_benchmark
    PRIVATE
        project_v2
)
apply_project_warnings(v2_arch_benchmark)
if(COMMAND boost_gateway_stage_runtime_dlls)
    boost_gateway_stage_runtime_dlls(v2_arch_benchmark)
endif()
