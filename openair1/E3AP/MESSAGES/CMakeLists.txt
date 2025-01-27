set(E3AP_VERSION 1 0 0)
make_version(E3AP_cc ${E3AP_VERSION})
#add_definitions(-DE3AP_VERSION=${E3AP_cc})
string(REPLACE ";" "." E3AP_RELEASE "${E3AP_VERSION}")

if(E3AP_RELEASE VERSION_EQUAL "1.0.0")
  include(ASN1/V1/e3ap-1.0.0.cmake)
else()
  message(FATAL_ERROR "unknown E3AP_RELEASE ${E3AP_RELEASE}")
endif()

message(STATUS "Selected E3AP_VERSION: ${E3AP_RELEASE}")
# > asn1c.log 2>&1
add_custom_command(OUTPUT ${e3ap_source} ${e3ap_headers}
  COMMAND ${ASN1C_EXEC} -pdu=all -gen-APER -gen-UPER -no-gen-JER -no-gen-BER -no-gen-OER -fno-include-deps -fcompound-names -findirect-choice -no-gen-example -D ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/${E3AP_GRAMMAR}
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${E3AP_GRAMMAR}
  COMMENT "Generating E3AP source files from ${CMAKE_CURRENT_SOURCE_DIR}/${E3AP_GRAMMAR}"
)

add_library(asn1_e3ap ${e3ap_source})
target_include_directories(asn1_e3ap PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_options(asn1_e3ap PRIVATE -DASN_DISABLE_OER_SUPPORT -w)

