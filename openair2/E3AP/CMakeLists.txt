find_package(PkgConfig REQUIRED)
pkg_check_modules(CLIBE3 REQUIRED libe3)

if(NOT CLIBE3_FOUND)
    message(FATAL_ERROR "libe3 not found and required for E3 Agent")
endif()

message(STATUS "Using for E3AP libe3 version: ${CLIBE3_VERSION}")

include_directories(${CLIBE3_INCLUDE_DIRS})

# Wire encoding (asn1 or json) is selected at RUNTIME from the config file
# (E3Configuration.encoding); both encoders are always compiled in. The
# E3_ENCODING_FORMAT cmake flag is accepted for compatibility but ignored.
if(DEFINED E3_ENCODING_FORMAT)
    message(STATUS "E3_ENCODING_FORMAT=${E3_ENCODING_FORMAT} ignored: encoding is runtime-selected (E3Configuration.encoding)")
endif()

pkg_check_modules(JSON_C REQUIRED json-c)
if(NOT JSON_C_FOUND)
    message(FATAL_ERROR "JSON-C not found and required for the E3 JSON encoding, install it with apt-get install libjson-c-dev")
endif()

add_subdirectory(service_models)
add_library(e3ap e3_agent.c config/e3_config.c)

# target_link_libraries(e3ap PUBLIC OCP_ITTI) # TODO

target_link_libraries(e3ap PUBLIC ${CLIBE3_LIBRARIES} ${SERVICE_MODELS_LIBRARY})
target_include_directories(e3ap PUBLIC ${JSON_C_INCLUDE_DIRS})

if(E2_AGENT)
    target_link_libraries(e3ap PUBLIC e2_ran_func_cuup e2_ran_func_du_cucp_cuup)
    target_compile_definitions(e3ap PRIVATE E2_AGENT)
    target_include_directories(e3ap PUBLIC ${OPENAIR_DIR}/openair2/E2AP/RAN_FUNCTION/O-RAN)
endif()
