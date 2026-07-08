set(L1_KPM_SM_VERSION 1 0 0)
make_version(L1_KPM_SM_cc ${L1_KPM_SM_VERSION})
string(REPLACE ";" "." L1_KPM_SM_RELEASE "${L1_KPM_SM_VERSION}")

if(L1_KPM_SM_RELEASE VERSION_EQUAL "1.0.0")
  include(ASN1/V1/e3sm_l1_kpm.cmake)
else()
  message(FATAL_ERROR "unknown L1_KPM_SM_RELEASE ${L1_KPM_SM_RELEASE}")
endif()

message(STATUS "Selected L1_KPM_SM_VERSION: ${L1_KPM_SM_RELEASE}")
run_asn1c(${CMAKE_CURRENT_SOURCE_DIR}/${L1_KPM_SM_GRAMMAR}
          ""
          OUTPUT ${l1_kpm_sm_source} ${l1_kpm_sm_headers}
          OPTIONS -pdu=all -gen-APER -gen-UPER -no-gen-JER -no-gen-BER -no-gen-OER -fno-include-deps -fcompound-names -findirect-choice -no-gen-example
          COMMENT "L1-KPM SM source files"
)

add_library(l1_kpm_sm_asn1 ${l1_kpm_sm_source})
target_include_directories(l1_kpm_sm_asn1 PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_options(l1_kpm_sm_asn1 PRIVATE -DASN_DISABLE_OER_SUPPORT -w)

set(L1_KPM_SM_ASN1_LIB l1_kpm_sm_asn1 PARENT_SCOPE)
set(L1_KPM_SM_ASN1_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}" PARENT_SCOPE)
