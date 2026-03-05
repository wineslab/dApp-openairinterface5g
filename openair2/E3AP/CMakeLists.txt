find_package(PkgConfig REQUIRED)
pkg_check_modules(CLIBE3 REQUIRED libe3)

if(NOT CLIBE3_FOUND)
    message(FATAL_ERROR "libe3 not found and required for E3 Agent")
endif()

message(STATUS "Using for E3AP libe3 version: ${CLIBE3_VERSION}")

include_directories(${CLIBE3_INCLUDE_DIRS})

# Set default encoding format if not specified
if(NOT DEFINED E3_ENCODING_FORMAT)
    set(E3_ENCODING_FORMAT "ASN1" CACHE STRING "E3AP encoding format (ASN1 or JSON)")
endif()

# Validate encoding format
if(NOT E3_ENCODING_FORMAT MATCHES "^(ASN1|JSON)$")
    message(FATAL_ERROR "Invalid E3_ENCODING_FORMAT: ${E3_ENCODING_FORMAT}. Must be ASN1 or JSON")
endif()

message(STATUS "E3AP encoding format: ${E3_ENCODING_FORMAT}")

add_subdirectory(service_models)
add_library(e3ap e3_agent.c config/e3_config.c)

# target_link_libraries(e3ap PUBLIC OCP_ITTI) # TODO

# Set encoding format compile definitions - both string value and format flags
target_compile_definitions(e3ap PRIVATE E3_ENCODING_FORMAT="${E3_ENCODING_FORMAT}")

if(E3_ENCODING_FORMAT STREQUAL "ASN1")
    target_compile_definitions(e3ap PRIVATE E3_ASN1_FORMAT)
    target_link_libraries(e3ap PUBLIC ${CLIBE3_LIBRARIES} asn1_e3ap ${SERVICE_MODELS_LIBRARY})
elseif(E3_ENCODING_FORMAT STREQUAL "JSON")
    target_compile_definitions(e3ap PRIVATE E3_JSON_FORMAT)
    pkg_check_modules(JSON_C REQUIRED json-c)
    if(NOT JSON_C_FOUND)
        message(FATAL_ERROR "JSON-C not found and required for JSON encoding, install it with apt-get install libjson-c-dev")
    endif()
    target_link_libraries(e3ap PUBLIC ${CLIBE3_LIBRARIES} ${JSON_C_LIBRARIES} ${SERVICE_MODELS_LIBRARY})
    target_include_directories(e3ap PUBLIC ${JSON_C_INCLUDE_DIRS})
endif()

if(E2_AGENT)
    target_link_libraries(e3ap PUBLIC e2_ran_func_cuup e2_ran_func_du_cucp_cuup)
    target_compile_definitions(e3ap PRIVATE E2_AGENT)
    target_include_directories(e3ap PUBLIC ${OPENAIR_DIR}/openair2/E2AP/RAN_FUNCTION/O-RAN)
endif()
