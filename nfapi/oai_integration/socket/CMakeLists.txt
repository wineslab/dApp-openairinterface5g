# SPDX-License-Identifier: LicenseRef-CSSL-1.0

add_library(nfapi_socket_lib
        socket_pnf.c
        socket_vnf.c
        socket_common.c
)

target_include_directories(nfapi_socket_lib PUBLIC include/)

target_compile_definitions(nfapi_socket_lib PUBLIC ENABLE_SOCKET)
target_link_libraries(nfapi_socket_lib PRIVATE nfapi_nr_lib nfapi_user_lib)
target_link_libraries(nfapi_socket_lib PRIVATE log_headers)
target_link_libraries(nfapi_socket_lib PRIVATE asn1_lte_rrc_hdrs asn1_nr_rrc_hdrs)
target_link_libraries(nfapi_socket_lib PRIVATE nr_fapi_p5 nr_fapi_p7)
target_link_libraries(nfapi_socket_lib PRIVATE nfapi_vnf_lib nfapi_pnf_lib)
