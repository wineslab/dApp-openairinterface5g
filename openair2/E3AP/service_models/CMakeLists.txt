# E3 Service Models CMakeLists.txt

cmake_minimum_required(VERSION 3.10)

# SM Registry and Interface
set(SM_CORE_SOURCES
    sm_registry.c
)

# Create SM core library
add_library(sm_core STATIC ${SM_CORE_SOURCES})

target_include_directories(sm_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/common/utils/LOG
    ${CMAKE_SOURCE_DIR}/openair2/E3AP
)

# Add service model implementations
add_subdirectory(spectrum_sm)

# Create combined service models library as interface library
add_library(service_models INTERFACE)

# Link all SM libraries
target_link_libraries(service_models INTERFACE
    sm_core
    ${SPECTRUM_SM_LIBRARY}
)

# Include directories for service models
target_include_directories(service_models INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Export service models library
set(SERVICE_MODELS_LIBRARY service_models PARENT_SCOPE)