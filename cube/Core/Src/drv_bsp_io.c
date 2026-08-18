/* drv_bsp_io.c — STM32G0 GPIO implementation of bsp_io_backend_t (Task 3).
 *
 * Relays and discrete inputs for the EF-G0B1R "G0B1 APU Manager". The portable
 * core (control_outputs.c / control_io.c) speaks only in board_pins.h logical
 * IDs; this file is the single place that knows physical pins and polarity.
 *
 * Pin map (spec §4) — CONFIRM each net against the schematic when configuring
 * the .ioc. Raw GPIO base/pin are used (not CubeMX label macros) so the driver
 * is independent of the User Labels chosen in the .ioc: the pins need only be
 * configured as GPIO_Output (relays) / GPIO_Input (switches).
 *
 * Output polarity: every relay is driven active-HIGH through a ULN2003
 * Darlington (MCU pin HIGH -> Darlington input high -> output sinks the coil ->
 * relay ENERGIZED), matching the PIC original (LATx = 1 == ON). Power-on /
 * safe-default is therefore GPIO LOW = de-energized.
 *
 * Input polarity (from the PIC original, the known-good reference):
 *   IN_OIL_PRESSURE  (PD6): main.c:752 `(Oil_Pressure)?0:1  // GND = fault`
 *       -> pin HIGH == oil pressure GOOD. in_read() returns TRUE on HIGH.
 *       *** BENCH CONFIRM (Task 3 Step 4): oil-switch polarity on the EF-G0B1R
 *           RC network — is closed == pressure-good? Flip active_high if not. ***
 *   IN_TRUCK_IGNITION (PD2): main.c:755 `(Truck_Engine_State)?0:1 // active low`
 *       -> pin LOW == ignition/truck-engine present. in_read() returns TRUE on LOW.
 */
#include "drv_bsp_io.h"
#include "board_pins.h"
#include "main.h"   /* CubeMX HAL: GPIO_TypeDef, GPIOx, HAL_GPIO_* , HAL_Delay */

typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
    bool          active_high;   /* logical-asserted level: true=HIGH, false=LOW */
} io_pin_t;

/* index == bsp_out_t (board_pins.h). All active-HIGH through the ULN2003. */
static const io_pin_t s_out[OUT_COUNT] = {
    [OUT_FUEL_PUMP]         = { GPIOC, GPIO_PIN_12, true }, /* PC12  Fl_Pmp_Snoid   */
    [OUT_STARTER]           = { GPIOC, GPIO_PIN_11, true }, /* PC11  Sttr_Snoid     */
    [OUT_GLOW_PLUG]         = { GPIOB, GPIO_PIN_8,  true }, /* PB8   Glow_Plug      */
    [OUT_COMPRESSOR_CLUTCH] = { GPIOB, GPIO_PIN_5,  true }, /* PB5   Cmprssr_Clutch */
    [OUT_HEAT_REVERSER]     = { GPIOB, GPIO_PIN_4,  true }, /* PB4   Heat_Reverser  */
    [OUT_EVAP_FAN]          = { GPIOC, GPIO_PIN_10, true }, /* PC10  Evap_Fan       */
    [OUT_CONDENSER_FAN]     = { GPIOB, GPIO_PIN_9,  true }, /* PB9   Condenser_Fan  */
};

/* index == bsp_in_t (board_pins.h). See input-polarity note in the file header. */
static const io_pin_t s_in[IN_COUNT] = {
    [IN_OIL_PRESSURE]   = { GPIOD, GPIO_PIN_6, true  }, /* PD6  Low_OilPress  (HIGH=good) */
    [IN_TRUCK_IGNITION] = { GPIOD, GPIO_PIN_2, false }, /* PD2  Trck_Ignition (LOW=present) */
};

static void io_out_set(void *ctx, uint8_t out, bool on)
{
    (void)ctx;
    if (out >= OUT_COUNT) return;
    const io_pin_t *m = &s_out[out];
    /* on==active_high -> drive to the asserted level; else the opposite. */
    HAL_GPIO_WritePin(m->port, m->pin,
                      (on == m->active_high) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static bool io_out_get(void *ctx, uint8_t out)
{
    (void)ctx;
    if (out >= OUT_COUNT) return false;
    const io_pin_t *m = &s_out[out];
    /* Read back the pin (IDR reflects the driven level for an output). */
    return (HAL_GPIO_ReadPin(m->port, m->pin) == GPIO_PIN_SET) == m->active_high;
}

static bool io_in_read(void *ctx, uint8_t in)
{
    (void)ctx;
    if (in >= IN_COUNT) return false;
    const io_pin_t *m = &s_in[in];
    return (HAL_GPIO_ReadPin(m->port, m->pin) == GPIO_PIN_SET) == m->active_high;
}

static const bsp_io_backend_t s_backend = {
    .out_set = io_out_set,
    .out_get = io_out_get,
    .in_read = io_in_read,
    .ctx     = NULL,
};

const bsp_io_backend_t *drv_bsp_io_backend(void) { return &s_backend; }

#if BSP_IO_BENCH_RELAY_WALK
void drv_bsp_io_bench_relay_walk(void)
{
    for (int o = 0; o < (int)OUT_COUNT; ++o) {
        io_out_set(NULL, (uint8_t)o, true);
        HAL_Delay(1000);
        io_out_set(NULL, (uint8_t)o, false);
        HAL_Delay(300);
    }
}
#endif
