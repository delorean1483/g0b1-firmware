#ifndef MBP_HEATER_H
#define MBP_HEATER_H

/* Registers 53-74: heater command + telemetry block (heater coprocessor). Register
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
     67 transport_errors    RO  (G0B1<->coprocessor I2C failures)
     68 heater_type         RO  active type (0 none, 1 VEVOR, 2 AUTOTERM)
     69 heater_setpoint_c   RW  5..30 (setpoint-type heaters)
     70 heater_caps         RO
     71 heater_phase        RO  0 off,1 detecting,2 starting,3 running,4 stop_req,5 cooldown,6 fault
     72 heater_vendor_state RO  major<<8 | sub
     73 heater_detect       RO  detect_result<<8 | line_sense
     74 heater_type_select  RW  0xFF auto, or forced type (bench)
*/
void mbp_heater_register(void);

#endif /* MBP_HEATER_H */
