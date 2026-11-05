#ifndef MBP_RGB_H
#define MBP_RGB_H

/* Registers 76-79: external RGB lamp. Register numbers are the frozen Modbus
   contract consumed by the cortex/gobi-agent -- do not renumber.
     76 led_red     RW  write = requested 0..255 (>255 illegal value); read = applied
     77 led_green   RW  same
     78 led_blue    RW  same
     79 led_status  RO  bit0 present, bit1 last apply OK, bit2 pending
   A write to one channel keeps the other two requested values and goes through
   rgb_set_logical(), the entry point shared by every transport. */
void mbp_rgb_register(void);

#endif /* MBP_RGB_H */
