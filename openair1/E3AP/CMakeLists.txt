find_package(PkgConfig REQUIRED)
pkg_check_modules(CZMQ REQUIRED libzmq)

if(NOT CZMQ_FOUND)
    message(FATAL_ERROR "CZMQ not found and required for E3AP, install it with apt-get install libczmq-dev")
endif()

add_subdirectory(MESSAGES)
add_library(e3ap e3_agent.c e3ap_handler.c e3_connector.c)

target_link_libraries(e3ap PUBLIC ${CZMQ_LIBRARIES} tracer_utils asn1_e3ap)
# target_link_libraries(e3ap PUBLIC OCP_ITTI)

target_include_directories(e3ap PUBLIC ${OPENAIR_DIR}/common/utils/T/tracer)

target_compile_definitions(e3ap PRIVATE T_MESSAGES_PATH="${OPENAIR_DIR}/common/utils/T/T_messages.txt")
