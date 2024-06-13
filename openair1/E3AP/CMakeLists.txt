message(STATUS "Add E3 Agent capabilities")

add_library(e3ap e3_agent.c)

# target_link_libraries(e3ap PUBLIC OCP_ITTI)

target_include_directories(e3ap PUBLIC ${OPENAIR_DIR}/common/utils/T/tracer)

target_compile_definitions(e3ap PRIVATE T_MESSAGES_PATH="${OPENAIR_DIR}/common/utils/T/T_messages.txt")

target_link_libraries(e3ap PUBLIC tracer_utils)