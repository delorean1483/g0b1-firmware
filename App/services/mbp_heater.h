#ifndef MBP_HEATER_H
#define MBP_HEATER_H

/* Registers 53-67: VEVOR diesel heater command + telemetry block. Register
   numbers are the frozen Modbus contract consumed by the cortex/gobi-agent
   (Sub-project #2) -- do not renumber.
     53 heater_request      RW  0/1
     54 heater_level        RW  1..10
     55 state               RO
     56 active_level        RO
     57 error                RO
     58 supply_mv           RO
     59 fan_rpm             RO
     60 pump_hz_x10         RO
     61 exchanger_raw       RO
     62 state_seconds       RO
     63 age_ms              RO
     64 flags               RO
     65 valid_frames        RO
     66 checksum_failures   RO
     67 transport_errors    RO
*/
void mbp_heater_register(void);

#endif /* MBP_HEATER_H */
