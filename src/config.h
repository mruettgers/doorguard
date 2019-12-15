#ifndef CONFIG_H
#define CONFIG_H

#include "types.h"

const char* VERSION = "1.0.0";

// Modifying the config version will probably cause a loss of the existig configuration.
// Be careful!
const char* CONFIG_VERSION = "1.0.0";

const uint8_t STATUS_PIN = LED_BUILTIN;

#endif