message(STATUS "Add E3 Agent capabilities ${CMAKE_SOURCE_DIR}")

add_library(E3AGENT e3_agent.c)
# target_link_libraries(e3agent PUBLIC e3agent)

target_include_directories(E3AGENT PUBLIC ${OPENAIR_DIR}/common/utils/T/tracer)
# target_link_libraries(e3agent /path/to/your/libexample.a)