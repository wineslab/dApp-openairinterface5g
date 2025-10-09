set(SPECTRUM_SM_VERSION 1 0 0)
make_version(SPECTRUM_SM_cc ${SPECTRUM_SM_VERSION})
string(REPLACE ";" "." SPECTRUM_SM_RELEASE "${SPECTRUM_SM_VERSION}")

if(SPECTRUM_SM_RELEASE VERSION_EQUAL "1.0.0")
  include(ASN1/V1/e3sm_spectrum.cmake)
else()
  message(FATAL_ERROR "unknown SPECTRUM_SM_RELEASE ${SPECTRUM_SM_RELEASE}")
endif()

message(STATUS "Selected SPECTRUM_SM_VERSION: ${SPECTRUM_SM_RELEASE}")
add_custom_command(OUTPUT ${spectrum_sm_source} ${spectrum_sm_headers}
  COMMAND ${ASN1C_EXEC} -pdu=all -gen-APER -gen-UPER -no-gen-JER -no-gen-BER -no-gen-OER -fno-include-deps -fcompound-names -findirect-choice -no-gen-example -D ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/${SPECTRUM_SM_GRAMMAR}
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${SPECTRUM_SM_GRAMMAR}
  COMMENT "Generating Spectrum SM source files from ${CMAKE_CURRENT_SOURCE_DIR}/${SPECTRUM_SM_GRAMMAR}"
)


add_library(spectrum_sm_asn1 ${spectrum_sm_source})
target_include_directories(spectrum_sm_asn1 PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_options(spectrum_sm_asn1 PRIVATE -DASN_DISABLE_OER_SUPPORT -w)

# Export variables to parent scope
set(SPECTRUM_SM_ASN1_LIB spectrum_sm_asn1 PARENT_SCOPE)
set(SPECTRUM_SM_ASN1_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}" PARENT_SCOPE)