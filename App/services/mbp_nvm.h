#ifndef MBP_NVM_H
#define MBP_NVM_H
/* Register the NVM-backed providers (settings/counters/calibration).
   nvm_init(...) must have been called first. */
void mbp_nvm_register(void);
#endif /* MBP_NVM_H */
