cmake_minimum_required(VERSION 3.10)

add_subdirectory(MESSAGES)

set(L1_KPM_SM_SOURCES
    l1_kpm_sm.c
    l1_kpm_enc.c
    e3_ran_buffers.c
)

# Both encodings are compiled in; the active one is selected at runtime from
# the config file (E3Configuration.encoding).
add_library(l1_kpm_sm STATIC ${L1_KPM_SM_SOURCES})

target_include_directories(l1_kpm_sm PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/common/utils/LOG
    ${CMAKE_SOURCE_DIR}/openair1
    ${CMAKE_SOURCE_DIR}/openair2/E3AP
    ${CLIBE3_INCLUDE_DIRS}
)

target_link_libraries(l1_kpm_sm
    json-c
    rt
    pthread
)

target_include_directories(l1_kpm_sm PUBLIC ${L1_KPM_SM_ASN1_INCLUDE_DIR})
target_link_libraries(l1_kpm_sm ${L1_KPM_SM_ASN1_LIB})
add_dependencies(l1_kpm_sm ${L1_KPM_SM_ASN1_LIB})

set(L1_KPM_SM_LIBRARY l1_kpm_sm PARENT_SCOPE)
