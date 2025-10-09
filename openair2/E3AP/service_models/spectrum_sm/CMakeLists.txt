# Spectrum Service Model CMakeLists.txt

cmake_minimum_required(VERSION 3.10)

add_subdirectory(MESSAGES)

# Spectrum SM source files
set(SPECTRUM_SM_SOURCES
    spectrum_sm.c
    spectrum_enc.c
    spectrum_dec.c
)

# Create spectrum SM library
add_library(spectrum_sm STATIC ${SPECTRUM_SM_SOURCES})

# Include directories
target_include_directories(spectrum_sm PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_BINARY_DIR}/MESSAGES  # ASN.1 generated headers
    ${CMAKE_SOURCE_DIR}/common/utils/LOG
    ${CMAKE_SOURCE_DIR}/openair2/E3AP
    ${CMAKE_SOURCE_DIR}/common/utils/T/tracer  # T-tracer headers
)

target_compile_definitions(spectrum_sm PRIVATE T_MESSAGES_PATH="${OPENAIR_DIR}/common/utils/T/T_messages.txt")

target_link_libraries(spectrum_sm spectrum_sm_asn1 tracer_utils)
add_dependencies(spectrum_sm spectrum_sm_asn1)

# Compile-time format selection (inherit from E3AP)
if(E3_ENCODING_FORMAT STREQUAL "ASN1")
    target_compile_definitions(spectrum_sm PRIVATE SPECTRUM_SM_ASN1_FORMAT)
    message(STATUS "Spectrum SM: Using ASN.1 encoding")
else()
    target_compile_definitions(spectrum_sm PRIVATE SPECTRUM_SM_JSON_FORMAT)
    target_link_libraries(spectrum_sm json-c)
    message(STATUS "Spectrum SM: Using JSON encoding")
endif()

# Export spectrum SM library
set(SPECTRUM_SM_LIBRARY spectrum_sm PARENT_SCOPE)