add_library(e3ap e3_agent.c)

# target_link_libraries(e3ap PUBLIC OCP_ITTI)

if(USE_E3_UDS)
    message(STATUS "Use UNIX Domain Sockets for E3 interface")
    target_compile_definitions(e3ap PRIVATE USE_E3_UDS)
else()
    message(STATUS "Use TCP Sockets for E3 interface")
endif()

target_include_directories(e3ap PUBLIC ${OPENAIR_DIR}/common/utils/T/tracer)

target_compile_definitions(e3ap PRIVATE T_MESSAGES_PATH="${OPENAIR_DIR}/common/utils/T/T_messages.txt")

target_link_libraries(e3ap PUBLIC tracer_utils)