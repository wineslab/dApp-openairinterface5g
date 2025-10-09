#include "e3_config.h"
#include "configuration.h"
#include "common/utils/LOG/log.h"
#include "config.h"
#include "common/config/config_paramdesc.h"
#include "common/config/config_userapi.h"
#include <string.h>
#include <stdlib.h>

#define E3CONFIG_SECTION "E3Configuration"

#define CONFIG_STRING_E3_LINK_PARAM "link"
#define CONFIG_STRING_E3_TRANSPORT_PARAM "transport"
#define CONFIG_STRING_E3_SAMPLING_PARAM "sampling"

// clang-format off
/*-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
/*                                            configuration parameters for the E3AP device                                                                                     */
/*   optname                     helpstr                     paramflags           XXXptr                               defXXXval                          type         numelt  */
/*-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
#define simOpt PARAMFLAG_NOFREE|PARAMFLAG_CMDLINE_NOPREFIXENABLED
#define E3_PARAMS_DESC {					\
    {"link",             "Link layer for E3",        simOpt,  .strptr=&e3_configs->link,               .defstrval="posix",           TYPE_STRING,    0 },\
    {"transport",        "Transport layer for E3",   simOpt,  .strptr=&(e3_configs->transport),        .defstrval="ipc",                 TYPE_STRING,    0 },\
  };
// clang-format on

const char *E3_VALID_CONFIGURATIONS[][2] = {
    {"zmq", "ipc"},
    {"zmq", "tcp"},
    // {"zmq", "sctp"}, // implemented but not working because zeromq does not support it (yet)
    {"posix", "tcp"},
    {"posix", "sctp"},
    {"posix", "ipc"}};

void e3_readconfig(e3_config_t *e3_configs)
{
  paramdef_t e3_params[] = E3_PARAMS_DESC;

  int ret = config_get(config_get_if(), e3_params, sizeof(e3_params) / sizeof(*(e3_params)), E3CONFIG_SECTION);
  AssertFatal(ret >= 0, "configuration couldn't be performed\n");

  LOG_I(E3AP,
        "this is the configuration extracted: link %s transport %s\n",
        e3_configs->link,
        e3_configs->transport);
}

void validate_configuration(e3_config_t *config)
{
  if (!config) {
    LOG_E(E3AP, "Configuration is null");
    abort();
  }

  // Check if link is "posix" or "zmq" using strncmp
  if (strncmp(config->link, "posix", 5) != 0 && strncmp(config->link, "zmq", 3) != 0) {
    LOG_E(E3AP, "Wrong link");
    abort();
  }

  // Check if transport is "tcp", "sctp", or "ipc" using strncmp
  if (strncmp(config->transport, "tcp", 3) != 0 && strncmp(config->transport, "sctp", 4) != 0
      && strncmp(config->transport, "ipc", 3) != 0) {
    LOG_E(E3AP, "Wrong transport");
    abort();
  }

  // Validate the combination of link and transport
  int combo_valid = 0;
  for (size_t i = 0; i < sizeof(E3_VALID_CONFIGURATIONS) / sizeof(E3_VALID_CONFIGURATIONS[0]); i++) {
    if (strncmp(config->link, E3_VALID_CONFIGURATIONS[i][0], strlen(E3_VALID_CONFIGURATIONS[i][0])) == 0
        && strncmp(config->transport, E3_VALID_CONFIGURATIONS[i][1], strlen(E3_VALID_CONFIGURATIONS[i][1])) == 0) {
      combo_valid = 1;
      break;
    }
  }

  if (!combo_valid) {
    LOG_E(E3AP, "Wrong combination");
    abort();
  }
}