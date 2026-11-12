#include "htr_i2c_map.h"
#include "unity.h"
#include "main.h"
#include "drv_i2c2_bus.h"
#include "mb_regmodel.h"
#include "drv_rgb_i2c2.h"
#include "drv_heater_i2c2.h"
#include <string.h>
#include "rgb.h"
#include "i2c_bench_trace.h"

GPIO_TypeDef test_gpioa, test_gpiob;
static I2C_TypeDef regs;
I2C_HandleTypeDef hi2c2;
static uint32_t tick, analog_seen, digital_seen;
static int calls, disable_calls, init_calls, resets, pulses, start_edges, stop_edges;
static int scl_release, sda_release, scl_external_low, sda_external_low;
static int release_sda_after, master_holds_scl, gpio_mode, retries_fail;
static int init_fail, filter_fail;
static HAL_StatusTypeDef first_result;
static uint32_t first_error;
static uint16_t address_seen, len_seen;
static uint32_t timeout_seen;
static uint8_t reg_seen;
static int writes_seen;
static uint8_t mock_color[3], mock_requested[3], mock_mode, mock_control;
static int probe_nack, busy_wait_timeout, phase_injection, hold_scl_on_first_hal;
void rgb_get_requested(uint8_t out[3]) { memcpy(out,mock_requested,3); }
void rgb_get_applied(uint8_t out[3]) { memcpy(out,mock_color,3); }
uint16_t rgb_get_status(void) { return RGB_ST_PRESENT | RGB_ST_APPLY_OK; }
void rgb_set_logical(uint8_t r,uint8_t g,uint8_t b)
{ mock_requested[0]=r;mock_requested[1]=g;mock_requested[2]=b; }
static modbus_exc_t heater_off(uint16_t r,uint16_t *v)
{ (void)r; *v=0; return MB_EXC_NONE; }


void setUp(void)
{
    memset(&regs, 0, sizeof(regs));
    memset(&hi2c2, 0, sizeof(hi2c2));
    hi2c2.Instance=&regs; hi2c2.Init.Timing=0x60302730u;
    tick=0; calls=disable_calls=init_calls=resets=pulses=start_edges=stop_edges=0;
    analog_seen=digital_seen=99;
    scl_release=sda_release=1;
    scl_external_low=sda_external_low=master_holds_scl=gpio_mode=0;
    release_sda_after=0; retries_fail=init_fail=filter_fail=0;
    first_result=HAL_OK; first_error=0; writes_seen=0;
    memset(mock_color,0,3);memset(mock_requested,0,3);
    mock_mode=0;mock_control=0x70;probe_nack=busy_wait_timeout=phase_injection=hold_scl_on_first_hal=0;
    drv_i2c2_init(); mb_reg_reset(); drv_i2c2_register_diagnostics();
    drv_rgb_i2c2_probe_register();
    mb_reg_bind(53,heater_off,0);mb_reg_bind(10,heater_off,0);
}
void tearDown(void) {}
uint32_t HAL_GetTick(void) { return tick; }
void HAL_Delay(uint32_t ms) { tick+=ms; }
void test_disable(void) { ++disable_calls; master_holds_scl=0; }
void test_reset(void) { ++resets; regs.CR1=regs.ISR=0; }
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *p, uint16_t pin)
{
    (void)pin;
    return p==GPIOA ? (scl_release&&!scl_external_low&&!master_holds_scl?GPIO_PIN_SET:GPIO_PIN_RESET)
                   : (sda_release&&!sda_external_low?GPIO_PIN_SET:GPIO_PIN_RESET);
}
void HAL_GPIO_WritePin(GPIO_TypeDef *p, uint16_t pin, GPIO_PinState v)
{
    (void)pin;
    if(p==GPIOA) {
        if(gpio_mode && !scl_release && v==GPIO_PIN_SET) {
            ++pulses;
            if(release_sda_after && pulses>=release_sda_after) sda_external_low=0;
        }
        scl_release=(v==GPIO_PIN_SET);
    } else {
        if(gpio_mode && !sda_release && v==GPIO_PIN_SET && HAL_GPIO_ReadPin(GPIOA,GPIO_PIN_11)) ++stop_edges;
        if(gpio_mode && sda_release && v==GPIO_PIN_RESET && HAL_GPIO_ReadPin(GPIOA,GPIO_PIN_11)) ++start_edges;
        sda_release=(v==GPIO_PIN_SET);
    }
}
void HAL_GPIO_Init(GPIO_TypeDef *p, GPIO_InitTypeDef *g)
{
    (void)p;
    TEST_ASSERT_EQUAL(GPIO_MODE_OUTPUT_OD,g->Mode);
    TEST_ASSERT_EQUAL(GPIO_NOPULL,g->Pull);
    gpio_mode=1;
}
HAL_StatusTypeDef HAL_I2C_DeInit(I2C_HandleTypeDef *h) { (void)h; return HAL_OK; }
HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *h)
{
    ++init_calls; gpio_mode=0;
    TEST_ASSERT_EQUAL_HEX32(0x60302730u,h->Init.Timing);
    return init_fail?HAL_ERROR:HAL_OK;
}
HAL_StatusTypeDef HAL_I2CEx_ConfigAnalogFilter(I2C_HandleTypeDef *h,uint32_t v)
{ (void)h; analog_seen=v; return filter_fail?HAL_ERROR:HAL_OK; }
HAL_StatusTypeDef HAL_I2CEx_ConfigDigitalFilter(I2C_HandleTypeDef *h,uint32_t v)
{ (void)h; digital_seen=v; return HAL_OK; }
uint32_t HAL_I2C_GetError(I2C_HandleTypeDef *h) { return h->ErrorCode; }
static HAL_StatusTypeDef xfer(I2C_HandleTypeDef *h,uint16_t a,uint16_t r,uint16_t size,uint8_t *b,uint16_t n,uint32_t t)
{
    (void)b; TEST_ASSERT_EQUAL(I2C_MEMADD_SIZE_8BIT,size);
    ++calls; address_seen=a; reg_seen=(uint8_t)r; len_seen=n; timeout_seen=t;
    if (phase_injection) {
        h->XferCount=n; h->XferSize=n; h->pBuffPtr=b; h->State=0x21;
        i2c_bench_stage(h,I2C_TRACE_REGISTER);
        regs.ISR=0x10;regs.CR2=0x01000000;
        i2c_bench_error(h,regs.ISR,0);
        i2c_bench_stage(h,I2C_TRACE_WAIT_END);
        h->XferCount=0; h->pBuffPtr=b+n;
        regs.ISR=0x8200;regs.CR2=0x4000;
        i2c_bench_error(h,regs.ISR,1);
        regs.ISR=regs.CR2=0;h->ErrorCode=HAL_I2C_ERROR_AF|HAL_I2C_ERROR_TIMEOUT;
        return HAL_ERROR;
    }
    if (busy_wait_timeout) { h->ErrorCode |= HAL_I2C_ERROR_TIMEOUT; return HAL_ERROR; }
    if (calls==1 && hold_scl_on_first_hal) master_holds_scl=1;
    h->ErrorCode=first_error;
    return calls==1?first_result:(retries_fail==2?HAL_ERROR:(retries_fail?HAL_TIMEOUT:HAL_OK));
}
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *h,uint16_t a,uint16_t r,uint16_t s,uint8_t *b,uint16_t n,uint32_t t)
{
    HAL_StatusTypeDef status=xfer(h,a,r,s,b,n,t);
    if(status==HAL_OK) memset(b,0,n);
    if(status==HAL_OK && n==1 && r==2) b[0]=mock_mode;
    if(status==HAL_OK && n==1 && r==1) b[0]=mock_control;
    return status;
}
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *h,uint16_t a,uint16_t r,uint16_t s,uint8_t *b,uint16_t n,uint32_t t)
{
    ++writes_seen;
    HAL_StatusTypeDef status=xfer(h,a,r,s,b,n,t);
    if(r==4 && probe_nack) { h->ErrorCode=HAL_I2C_ERROR_AF; return HAL_ERROR; }
    return status;
}
static uint16_t diag(uint16_t r) { uint16_t v; TEST_ASSERT_EQUAL(MB_EXC_NONE,mb_reg_read(r,&v)); return v; }
static int read_one(void) { uint8_t b=0; return drv_i2c2_read(0x64,2,&b,1,20); }


/* Leave the mock positioned at the failing transfer that triggers recovery. */
static void prime_recovery(void)
{
    TEST_ASSERT_EQUAL(-1,read_one());
    calls=0;
    TEST_ASSERT_EQUAL(-1,read_one());
    TEST_ASSERT_EQUAL(0,diag(80));
    calls=0;
}

void test_idle_lines_use_f1_start_stop_after_three_failures(void)
{
    first_result=HAL_TIMEOUT;
    regs.ISR=I2C_ISR_BUSY;
    prime_recovery();
    TEST_ASSERT_EQUAL(0,read_one());
    TEST_ASSERT_EQUAL(1,start_edges);
    TEST_ASSERT_EQUAL(1,pulses);
    TEST_ASSERT_EQUAL(1,stop_edges);
    TEST_ASSERT_EQUAL(1,resets);
    TEST_ASSERT_EQUAL(3,diag(83));
}

void test_success_breaks_failure_streak(void)
{
    first_result=HAL_TIMEOUT;
    prime_recovery();
    first_result=HAL_OK;
    TEST_ASSERT_EQUAL(0,read_one());
    first_result=HAL_TIMEOUT;
    calls=0;
    prime_recovery();
}
void test_healthy_does_not_recover(void)
{ TEST_ASSERT_EQUAL(0,read_one()); TEST_ASSERT_EQUAL(1,calls); TEST_ASSERT_EQUAL(0,disable_calls); }
void test_idle_nack_does_not_touch_shared_bus(void)
{ first_result=HAL_ERROR; first_error=HAL_I2C_ERROR_AF; TEST_ASSERT_EQUAL(-1,read_one()); TEST_ASSERT_EQUAL(0,disable_calls); }
void test_master_scl_low_is_released_and_retry_succeeds(void)
{
    uint16_t before=diag(80);
    master_holds_scl=1;
    TEST_ASSERT_EQUAL(0,read_one()); TEST_ASSERT_EQUAL(1,calls);
    TEST_ASSERT_EQUAL(1,disable_calls); TEST_ASSERT_EQUAL(1,init_calls); TEST_ASSERT_EQUAL(1,resets);
    TEST_ASSERT_EQUAL(before+1,diag(80)); TEST_ASSERT_EQUAL(2,diag(83));
    TEST_ASSERT_EQUAL(1,stop_edges); TEST_ASSERT_EQUAL(0,gpio_mode);
}
void test_sda_low_is_clocked_free_and_stop_sent(void)
{
    sda_external_low=1; release_sda_after=3;
    regs.CR1=I2C_CR1_ANFOFF|(7u<<8);
    TEST_ASSERT_EQUAL(0,read_one()); TEST_ASSERT_EQUAL(1,calls);
    TEST_ASSERT_EQUAL(4,pulses); /* Three clear clocks plus STOP's SCL rise. */
    TEST_ASSERT_EQUAL(1,stop_edges); TEST_ASSERT_EQUAL(1,analog_seen); TEST_ASSERT_EQUAL(7,digital_seen);
    TEST_ASSERT_EQUAL(1,diag(83));
}
void test_nack_with_stuck_bus_does_recover(void)
{
    first_result=HAL_TIMEOUT;
    prime_recovery();
    first_result=HAL_ERROR;
    first_error=HAL_I2C_ERROR_AF;
    hold_scl_on_first_hal=1;
    TEST_ASSERT_EQUAL(0,read_one());
    TEST_ASSERT_EQUAL(1,disable_calls);
    TEST_ASSERT_EQUAL(0,diag(138));
}
void test_permanent_scl_low_is_bounded_and_restores_peripheral(void)
{
    first_result=HAL_TIMEOUT; scl_external_low=1;
    TEST_ASSERT_EQUAL(-1,read_one()); TEST_ASSERT_EQUAL(0,calls); TEST_ASSERT_EQUAL(1,init_calls);
    TEST_ASSERT_LESS_THAN(30,tick); TEST_ASSERT_EQUAL(0,gpio_mode); TEST_ASSERT_TRUE(scl_release&&sda_release);
}
void test_permanent_sda_low_gets_only_nine_clocks(void)
{
    first_result=HAL_TIMEOUT; sda_external_low=1;
    TEST_ASSERT_EQUAL(-1,read_one()); TEST_ASSERT_EQUAL(10,pulses); /* Nine + STOP. */
    TEST_ASSERT_EQUAL(0,calls); TEST_ASSERT_LESS_THAN(40,tick); TEST_ASSERT_EQUAL(0,gpio_mode);
}
void test_retry_failure_is_not_an_unbounded_recovery_loop(void)
{ first_result=HAL_TIMEOUT; retries_fail=1; prime_recovery(); TEST_ASSERT_EQUAL(-1,read_one()); TEST_ASSERT_EQUAL(2,calls); TEST_ASSERT_EQUAL(1,disable_calls); }
void test_init_failure_is_reported_without_retry(void)
{ first_result=HAL_TIMEOUT; init_fail=1; prime_recovery(); TEST_ASSERT_EQUAL(-1,read_one()); TEST_ASSERT_EQUAL(1,calls); }
void test_filter_failure_is_reported_without_retry(void)
{ first_result=HAL_TIMEOUT; filter_fail=1; prime_recovery(); TEST_ASSERT_EQUAL(-1,read_one()); TEST_ASSERT_EQUAL(1,calls); }
void test_both_clients_use_shared_transport_and_keep_timeout(void)
{
    uint8_t b[54]={0}; const i2c_backend_t *bus=drv_heater_i2c2_backend();
    TEST_ASSERT_EQUAL(0,bus->read(0,16,b,54)); TEST_ASSERT_EQUAL(0xc8,address_seen);
    TEST_ASSERT_EQUAL(54,len_seen); TEST_ASSERT_EQUAL(20,timeout_seen);
    bus=drv_rgb_i2c2_backend(); calls=0; first_result=HAL_TIMEOUT; prime_recovery();
    TEST_ASSERT_EQUAL(-1,bus->write(0,9,b,6)); TEST_ASSERT_EQUAL(0xd8,address_seen);
    TEST_ASSERT_EQUAL(9,reg_seen); TEST_ASSERT_EQUAL(6,len_seen); TEST_ASSERT_EQUAL(20,timeout_seen);
    TEST_ASSERT_EQUAL(1,writes_seen); TEST_ASSERT_EQUAL(1,disable_calls);
}
void test_diagnostics_are_read_only(void)
{ TEST_ASSERT_EQUAL(MB_EXC_ILLEGAL_ADDRESS,mb_reg_write(80,1)); TEST_ASSERT_EQUAL(MB_EXC_ILLEGAL_ADDRESS,mb_reg_write(83,1)); }

void test_short_cooldown_skips_hal_for_both_clients(void)
{
    uint8_t b=0;
    first_result=HAL_TIMEOUT; scl_external_low=1;
    TEST_ASSERT_EQUAL(-1,read_one());
    uint32_t end=tick;
    int count=calls;
    tick=end+999u;
    TEST_ASSERT_EQUAL(-1,drv_rgb_i2c2_backend()->read(0,1,&b,1));
    TEST_ASSERT_EQUAL(-1,drv_heater_i2c2_backend()->read(0,1,&b,1));
    TEST_ASSERT_EQUAL(count,calls); TEST_ASSERT_EQUAL(1,diag(80));
    tick=end+1000u; calls=0;
    TEST_ASSERT_EQUAL(-1,read_one());
    TEST_ASSERT_EQUAL(2,diag(80));
}
void test_healthy_transfers_continue_during_cooldown(void)
{
    first_result=HAL_TIMEOUT;
    prime_recovery();
    TEST_ASSERT_EQUAL(0,read_one());
    int count=calls;
    TEST_ASSERT_EQUAL(0,read_one());
    TEST_ASSERT_EQUAL(count+1,calls); TEST_ASSERT_EQUAL(1,diag(80));
}
void test_new_fault_cannot_clear_twice_during_cooldown(void)
{
    first_result=HAL_TIMEOUT;
    prime_recovery();
    TEST_ASSERT_EQUAL(0,read_one());
    calls=0; scl_external_low=1;
    TEST_ASSERT_EQUAL(-1,read_one());
    TEST_ASSERT_EQUAL(0,calls); TEST_ASSERT_EQUAL(1,diag(80));
}
void test_cooldown_wraps_safely(void)
{
    tick=UINT32_MAX-100u; first_result=HAL_TIMEOUT; scl_external_low=1;
    TEST_ASSERT_EQUAL(-1,read_one());
    uint32_t end=tick;
    tick=end+999u; int count=calls;
    TEST_ASSERT_EQUAL(-1,read_one()); TEST_ASSERT_EQUAL(count,calls);
    tick=end+1000u; calls=0;
    TEST_ASSERT_EQUAL(-1,read_one()); TEST_ASSERT_EQUAL(2,diag(80));
}
void test_heater_command_is_not_replayed_by_transport(void)
{
    uint8_t command=3;
    first_result=HAL_TIMEOUT;
    prime_recovery();
    TEST_ASSERT_EQUAL(-1,drv_heater_i2c2_backend()->write(0,0x18,&command,1));
    TEST_ASSERT_EQUAL(1,writes_seen); TEST_ASSERT_EQUAL(1,diag(80));
    drv_i2c2_init(); calls=writes_seen=0;
    /* A future burst spanning COMMAND is equally non-repeatable. */
    uint8_t burst[2]={0,3};
    TEST_ASSERT_EQUAL(-1,drv_heater_i2c2_backend()->write(0,0x17,burst,2));
    TEST_ASSERT_EQUAL(1,writes_seen);
}


void test_idle_nack_on_retry_does_not_block_other_client(void)
{
    first_result=HAL_TIMEOUT; first_error=HAL_I2C_ERROR_AF; retries_fail=2; prime_recovery();
    TEST_ASSERT_EQUAL(-1,read_one());
    TEST_ASSERT_EQUAL(1,diag(80));
    retries_fail=0;
    int count=calls;
    uint8_t b=0;
    TEST_ASSERT_EQUAL(0,drv_rgb_i2c2_backend()->read(0,1,&b,1));
    TEST_ASSERT_EQUAL(count+1,calls);
}
/* The cooldown only skips the HAL while the bus is known bad (a line held, or
   the last clear failed). A failed transfer on an idle bus must not block the
   other client: on the bench that turned every lamp fault into ~1 s of heater
   transport errors. */
void test_failed_retry_with_idle_lines_does_not_block_other_client(void)
{
    uint8_t b=0;
    first_result=HAL_TIMEOUT; retries_fail=1; prime_recovery();
    TEST_ASSERT_EQUAL(-1,read_one());
    TEST_ASSERT_EQUAL(1,diag(80));
    retries_fail=0;
    int count=calls;
    TEST_ASSERT_EQUAL(0,drv_heater_i2c2_backend()->read(0,1,&b,1));
    TEST_ASSERT_EQUAL(count+1,calls); TEST_ASSERT_EQUAL(1,diag(80));
}
void test_fault_during_cooldown_with_idle_lines_does_not_block_next_transfer(void)
{
    first_result=HAL_TIMEOUT;
    prime_recovery();
    TEST_ASSERT_EQUAL(0,read_one());          /* recovered, retry succeeded */
    retries_fail=1;
    TEST_ASSERT_EQUAL(-1,read_one());         /* new fault inside the cooldown */
    TEST_ASSERT_EQUAL(1,diag(80));            /* no second clear */
    retries_fail=0;
    int count=calls;
    TEST_ASSERT_EQUAL(0,read_one());
    TEST_ASSERT_EQUAL(count+1,calls);
}
void test_failed_clear_with_idle_lines_still_blocks_until_cooldown_ends(void)
{
    first_result=HAL_TIMEOUT; init_fail=1; prime_recovery();
    TEST_ASSERT_EQUAL(-1,read_one());
    TEST_ASSERT_EQUAL(1,diag(82));            /* the clear failed */
    init_fail=0;
    uint32_t end=tick; int count=calls;
    tick=end+999u;
    TEST_ASSERT_EQUAL(-1,read_one()); TEST_ASSERT_EQUAL(count,calls);
    tick=end+1000u;
    TEST_ASSERT_EQUAL(0,read_one());
}

void test_error_counters_and_event_log_preserve_arlo_payload(void)
{
    uint8_t data=1;
    first_result=HAL_ERROR;first_error=HAL_I2C_ERROR_ARLO;
    TEST_ASSERT_EQUAL(-1,drv_i2c2_write_once(0x6c,4,&data,1,20));
    TEST_ASSERT_EQUAL(1,diag(92));TEST_ASSERT_EQUAL(1,diag(94));
    TEST_ASSERT_EQUAL(0,diag(95));TEST_ASSERT_EQUAL(1,diag(135));
    TEST_ASSERT_EQUAL_HEX16(0x6c04,diag(163));
    TEST_ASSERT_EQUAL(HAL_I2C_ERROR_ARLO,diag(164));
    TEST_ASSERT_EQUAL_HEX16(0x0101,diag(169));
}
void test_af_and_timeout_are_separate_from_recovery(void)
{
    first_result=HAL_ERROR;first_error=HAL_I2C_ERROR_AF;
    TEST_ASSERT_EQUAL(-1,read_one());
    TEST_ASSERT_EQUAL(1,diag(102));TEST_ASSERT_EQUAL(0,diag(80));
    drv_i2c2_init();calls=0;first_error=HAL_I2C_ERROR_TIMEOUT;
    TEST_ASSERT_EQUAL(-1,read_one());
    TEST_ASSERT_EQUAL(1,diag(103));TEST_ASSERT_EQUAL(0,diag(101));
}
void test_busy_wait_timeout_does_not_inherit_other_device_arlo(void)
{
    uint8_t data=1;
    first_result=HAL_ERROR;first_error=HAL_I2C_ERROR_ARLO;
    TEST_ASSERT_EQUAL(-1,drv_i2c2_write_once(0x6c,4,&data,1,20));
    busy_wait_timeout=1;
    TEST_ASSERT_EQUAL(-1,read_one());
    TEST_ASSERT_EQUAL(1,diag(94));TEST_ASSERT_EQUAL(0,diag(101));
    TEST_ASSERT_EQUAL(1,diag(103));
    TEST_ASSERT_EQUAL(HAL_I2C_ERROR_TIMEOUT,diag(174));
}
void test_phase_log_keeps_pre_call_and_first_error_before_cleanup(void)
{
    regs.ISR=0x8201;regs.CR2=0x12345678;hi2c2.State=0x20;
    hi2c2.ErrorCode=HAL_I2C_ERROR_ARLO;phase_injection=1;
    uint8_t data=1;
    TEST_ASSERT_EQUAL(-1,drv_i2c2_write_once(0x6c,4,&data,1,20));
    TEST_ASSERT_EQUAL(1,diag(480));TEST_ASSERT_EQUAL(1,diag(481));
    TEST_ASSERT_EQUAL_HEX16(0x8201,diag(482));
    TEST_ASSERT_EQUAL_HEX16(0x5678,diag(484));
    TEST_ASSERT_EQUAL(3,diag(486));
    TEST_ASSERT_EQUAL(I2C_TRACE_REGISTER,diag(489));
    TEST_ASSERT_EQUAL_HEX16(0x10,diag(490));
    TEST_ASSERT_EQUAL_HEX16(0x0100,diag(493));
    TEST_ASSERT_EQUAL(1,diag(497));TEST_ASSERT_EQUAL(0,diag(499));
    TEST_ASSERT_EQUAL(0,diag(501));
    TEST_ASSERT_EQUAL_HEX16(0x8210,diag(508));
    TEST_ASSERT_EQUAL_HEX16(0x8002,diag(510));
    TEST_ASSERT_EQUAL(I2C_TRACE_WAIT_END,diag(511));
    TEST_ASSERT_EQUAL(0,diag(166)); /* Existing ring remains post-cleanup. */
}
void test_missing_hal_hook_is_explicitly_marked_as_fallback(void)
{
    first_result=HAL_TIMEOUT;
    TEST_ASSERT_EQUAL(-1,read_one());
    TEST_ASSERT_EQUAL_HEX16(0x8000|I2C_TRACE_ENTRY,diag(489));
    TEST_ASSERT_EQUAL(2,diag(501));
    TEST_ASSERT_EQUAL(UINT16_MAX,diag(497));
}
void test_probe_verifies_mode_and_counts_one_unreplayed_write(void)
{
    TEST_ASSERT_EQUAL(MB_EXC_NONE,mb_reg_write(109,1));
    TEST_ASSERT_EQUAL(MB_EXC_NONE,mb_reg_write(108,1));
    probe_nack=1;
    drv_rgb_i2c2_probe_process(0);
    TEST_ASSERT_EQUAL(1,diag(114));TEST_ASSERT_EQUAL(1,diag(117));
    TEST_ASSERT_EQUAL(1,diag(134));TEST_ASSERT_EQUAL(1,writes_seen);
    TEST_ASSERT_EQUAL_HEX16(0x0101,diag(169));
    drv_rgb_i2c2_probe_process(1000);
    TEST_ASSERT_EQUAL(2,diag(111));
}
void test_probe_skips_wrong_mode_and_aborts_on_color_change(void)
{
    TEST_ASSERT_EQUAL(MB_EXC_NONE,mb_reg_write(108,2));
    mock_mode=0x35;
    drv_rgb_i2c2_probe_process(0);
    TEST_ASSERT_EQUAL(1,diag(124));TEST_ASSERT_EQUAL(0,diag(114));
    mock_requested[0]=1;
    drv_rgb_i2c2_probe_process(50);
    TEST_ASSERT_EQUAL(3,diag(111));TEST_ASSERT_EQUAL(3,diag(113));
    TEST_ASSERT_EQUAL(0,writes_seen);
}
void test_probe_end_deadline_wraps_safely(void)
{
    TEST_ASSERT_EQUAL(MB_EXC_NONE,mb_reg_write(109,1));
    TEST_ASSERT_EQUAL(MB_EXC_NONE,mb_reg_write(108,2));
    drv_rgb_i2c2_probe_process(UINT32_MAX-999u);
    TEST_ASSERT_EQUAL(1,diag(111));
    drv_rgb_i2c2_probe_process(0);
    TEST_ASSERT_EQUAL(2,diag(111));
}

void test_guard_recovery_invalidates_heater_write_before_hal(void)
{
    uint8_t command=3;
    sda_external_low=1;
    release_sda_after=2;
    first_result=HAL_TIMEOUT;
    TEST_ASSERT_EQUAL(-1,drv_heater_i2c2_backend()->write(0,0x18,&command,1));
    TEST_ASSERT_EQUAL(0,writes_seen);
    TEST_ASSERT_EQUAL_UINT32(1,drv_i2c2_recovery_epoch());
    TEST_ASSERT_EQUAL(1,diag(137));
    TEST_ASSERT_EQUAL(1,diag(138));
    TEST_ASSERT_EQUAL(1,diag(81));
    TEST_ASSERT_EQUAL(0,diag(107));
}

void test_heater_control_write_is_not_replayed_after_recovery(void)
{
    uint8_t request = 0;
    first_result = HAL_TIMEOUT;
    prime_recovery();
    TEST_ASSERT_EQUAL(-1, drv_heater_i2c2_backend()->write(0, HTR_R_RUN_REQUEST, &request, 1));
    TEST_ASSERT_EQUAL(1, writes_seen);
    TEST_ASSERT_EQUAL_UINT32(1, drv_i2c2_recovery_epoch());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_guard_recovery_invalidates_heater_write_before_hal);
    RUN_TEST(test_error_counters_and_event_log_preserve_arlo_payload);
    RUN_TEST(test_af_and_timeout_are_separate_from_recovery);
    RUN_TEST(test_busy_wait_timeout_does_not_inherit_other_device_arlo);
    RUN_TEST(test_phase_log_keeps_pre_call_and_first_error_before_cleanup);
    RUN_TEST(test_missing_hal_hook_is_explicitly_marked_as_fallback);
    RUN_TEST(test_probe_verifies_mode_and_counts_one_unreplayed_write);
    RUN_TEST(test_probe_skips_wrong_mode_and_aborts_on_color_change);
    RUN_TEST(test_probe_end_deadline_wraps_safely);
    RUN_TEST(test_idle_lines_use_f1_start_stop_after_three_failures);
    RUN_TEST(test_success_breaks_failure_streak);
    RUN_TEST(test_failed_retry_with_idle_lines_does_not_block_other_client);
    RUN_TEST(test_fault_during_cooldown_with_idle_lines_does_not_block_next_transfer);
    RUN_TEST(test_failed_clear_with_idle_lines_still_blocks_until_cooldown_ends);
    RUN_TEST(test_idle_nack_on_retry_does_not_block_other_client);
    RUN_TEST(test_heater_command_is_not_replayed_by_transport);
    RUN_TEST(test_heater_control_write_is_not_replayed_after_recovery);
    RUN_TEST(test_cooldown_wraps_safely);
    RUN_TEST(test_new_fault_cannot_clear_twice_during_cooldown);
    RUN_TEST(test_healthy_transfers_continue_during_cooldown);
    RUN_TEST(test_short_cooldown_skips_hal_for_both_clients);
    RUN_TEST(test_healthy_does_not_recover);
    RUN_TEST(test_idle_nack_does_not_touch_shared_bus);
    RUN_TEST(test_master_scl_low_is_released_and_retry_succeeds);
    RUN_TEST(test_sda_low_is_clocked_free_and_stop_sent);
    RUN_TEST(test_nack_with_stuck_bus_does_recover);
    RUN_TEST(test_permanent_scl_low_is_bounded_and_restores_peripheral);
    RUN_TEST(test_permanent_sda_low_gets_only_nine_clocks);
    RUN_TEST(test_retry_failure_is_not_an_unbounded_recovery_loop);
    RUN_TEST(test_init_failure_is_reported_without_retry);
    RUN_TEST(test_filter_failure_is_reported_without_retry);
    RUN_TEST(test_both_clients_use_shared_transport_and_keep_timeout);
    RUN_TEST(test_diagnostics_are_read_only);
    return UNITY_END();
}
