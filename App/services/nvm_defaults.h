#ifndef NVM_DEFAULTS_H
#define NVM_DEFAULTS_H
#include <stdint.h>
/* Zero-fill shadow[0..NVM_PARAM_SIZE) then write the factory-default
   parameter values (little-endian words) at their EE offsets. */
void nvm_apply_factory_defaults(uint8_t *shadow);
#endif
