#ifndef E3_CONFIG_H
#define E3_CONFIG_H

#include <stddef.h>

// Configuration structure for E3AP
typedef struct {
    char *link;         // Link layer (posix, zmq)
    char *transport;    // Transport layer (tcp, sctp, ipc)
} e3_config_t;

// Valid configuration combinations
extern const char *E3_VALID_CONFIGURATIONS[][2];

/**
 * @brief Read E3 configuration from the OAI config file
 * @param e3_configs Pointer to configuration structure to fill
 */
void e3_readconfig(e3_config_t *e3_configs);

/**
 * @brief Validate E3AP configuration parameters
 * @param config Pointer to configuration structure to validate
 */
void validate_configuration(e3_config_t *config);

#endif // E3_CONFIG_H