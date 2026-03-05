cmake_minimum_required(VERSION 3.10)

add_subdirectory(spectrum_sm)

add_library(service_models INTERFACE)

target_include_directories(service_models INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/common/utils/LOG
    ${CMAKE_SOURCE_DIR}/openair2/E3AP
    ${CLIBE3_INCLUDE_DIRS} 
)

target_link_libraries(service_models INTERFACE
    ${SPECTRUM_SM_LIBRARY}
)

set(SERVICE_MODELS_LIBRARY service_models PARENT_SCOPE)