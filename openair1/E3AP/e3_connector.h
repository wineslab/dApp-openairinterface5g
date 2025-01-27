#ifndef E3_CONNECTOR_H
#define E3_CONNECTOR_H

#include <stddef.h>
#include <stdint.h>

// Class-like structure definition
typedef struct E3Connector {
    // Shared variables between ZMQ and POSIX
    // Endpoints URL
    char *setup_endpoint;
    char *inbound_endpoint;
    char *outbound_endpoint;
    
    // Socket descriptors
    void *inbound_socket;
    void *outbound_socket;
    void *setup_socket;

    // POSIX only
    int inbound_connection_socket;
    int setup_connection_socket;
    int setup_happened;

    // ZMQ only
    void *context;

    // Wrapping function shared
    
    int (*setup_initial_connection)(struct E3Connector *self);
    int (*recv_setup_request)(struct E3Connector *self, void *buffer, size_t buffer_size);
    int (*send_response)(struct E3Connector *self, const uint8_t *response, size_t response_size);
    int (*setup_inbound_connection)(struct E3Connector *self);
    int (*receive)(struct E3Connector *self, void *buffer, size_t buffer_size);
    int (*setup_outbound_connection)(struct E3Connector *self);
    int (*send)(struct E3Connector *self, const uint8_t *payload, size_t payload_size);
    void (*dispose)(struct E3Connector *self);
} E3Connector;

E3Connector *create_connector(const char *link_layer, const char *transport_layer);

#endif // E3_CONNECTOR_H
