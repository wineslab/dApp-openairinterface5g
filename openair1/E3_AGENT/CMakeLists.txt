message(STATUS "Add E3 Agent capabilities ${CMAKE_SOURCE_DIR}")

add_library(E3AGENT e3_agent.c)
# target_link_libraries(e3agent PUBLIC e3agent)

# target_include_directories(e3agent PUBLIC ${CMAKE_CURRENT_DIR})
# target_link_libraries(e3agent /path/to/your/libexample.a)