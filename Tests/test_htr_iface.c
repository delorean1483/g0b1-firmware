#include "htr_iface.h"
#include "htr_types.h"
#include "fake_htr_gpio.h"
#include "unity.h"

static fake_gpio_t g;
void setUp(void){ fake_gpio_init(&g); }
void tearDown(void){}

#define LINE(l,v)  {EV_LINE,(uint8_t)(l),(uint8_t)(v)}
#define ATT(w,v)   {EV_ATTACH,(uint8_t)(w),(uint8_t)(v)}
static void expect(const fake_ev_t *want, size_t n){
    TEST_ASSERT_EQUAL_size_t(n, g.n);
    for(size_t i=0;i<n;i++){
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(want[i].kind,g.ev[i].kind,"kind");
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(want[i].a,g.ev[i].a,"a");
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(want[i].b,g.ev[i].b,"b");
    }
}
void test_init_drives_everything_safe_disconnect_first(void){
    htr_iface_init(&g.api,false);
    const fake_ev_t w[]={LINE(HTR_LINE_TX_EN,0),ATT(HTR_WIRING_3W,0),ATT(HTR_WIRING_4W,0),
                         LINE(HTR_LINE_TX_PWR,0),LINE(HTR_LINE_TX_MODE,0)};
    expect(w,5);
    TEST_ASSERT_FALSE(htr_iface_ready());
    TEST_ASSERT_EQUAL_UINT8(HTR_WIRING_NONE, htr_iface_wiring());
}
void test_enable_4w_powers_settles_then_attaches_and_connects(void){
    htr_iface_init(&g.api,false); fake_gpio_clear_log(&g);
    htr_iface_enable(HTR_WIRING_4W,100);
    const fake_ev_t w1[]={LINE(HTR_LINE_TX_EN,0),LINE(HTR_LINE_TX_MODE,1),LINE(HTR_LINE_TX_PWR,1)};
    expect(w1,3);
    htr_iface_process(104); TEST_ASSERT_FALSE(htr_iface_ready()); TEST_ASSERT_EQUAL_size_t(3,g.n);
    htr_iface_process(105); TEST_ASSERT_TRUE(htr_iface_ready());
    const fake_ev_t w2[]={LINE(HTR_LINE_TX_EN,0),LINE(HTR_LINE_TX_MODE,1),LINE(HTR_LINE_TX_PWR,1),
                          ATT(HTR_WIRING_4W,1),LINE(HTR_LINE_TX_EN,1)};
    expect(w2,5);
}
void test_enable_3w_leaves_tx_power_off_unless_board_says(void){
    htr_iface_init(&g.api,false); htr_iface_enable(HTR_WIRING_3W,0); htr_iface_process(10);
    TEST_ASSERT_FALSE(g.line[HTR_LINE_TX_PWR]); TEST_ASSERT_FALSE(g.line[HTR_LINE_TX_MODE]);
    TEST_ASSERT_TRUE(g.attached[HTR_WIRING_3W]); TEST_ASSERT_TRUE(g.line[HTR_LINE_TX_EN]);
    htr_iface_init(&g.api,true); htr_iface_enable(HTR_WIRING_3W,0);
    TEST_ASSERT_TRUE(g.line[HTR_LINE_TX_PWR]);
}
void test_class_change_goes_through_safe_state(void){
    htr_iface_init(&g.api,false); htr_iface_enable(HTR_WIRING_3W,0); htr_iface_process(10);
    fake_gpio_clear_log(&g);
    htr_iface_enable(HTR_WIRING_4W,20);
    /* first event must disconnect, and 3W must be detached before 4W power-up */
    TEST_ASSERT_EQUAL_UINT8(EV_LINE,g.ev[0].kind); TEST_ASSERT_EQUAL_UINT8(HTR_LINE_TX_EN,g.ev[0].a); TEST_ASSERT_EQUAL_UINT8(0,g.ev[0].b);
    TEST_ASSERT_FALSE(g.attached[HTR_WIRING_3W]);
    TEST_ASSERT_FALSE(htr_iface_ready());
}
void test_disable_disconnects_first(void){
    htr_iface_init(&g.api,false); htr_iface_enable(HTR_WIRING_4W,0); htr_iface_process(10);
    fake_gpio_clear_log(&g); htr_iface_disable();
    TEST_ASSERT_EQUAL_UINT8(HTR_LINE_TX_EN,g.ev[0].a); TEST_ASSERT_EQUAL_UINT8(0,g.ev[0].b);
    TEST_ASSERT_FALSE(g.line[HTR_LINE_TX_PWR]); TEST_ASSERT_FALSE(g.attached[HTR_WIRING_4W]);
    TEST_ASSERT_EQUAL_UINT8(HTR_WIRING_NONE, htr_iface_wiring());
}
int main(void){ UNITY_BEGIN();
    RUN_TEST(test_init_drives_everything_safe_disconnect_first);
    RUN_TEST(test_enable_4w_powers_settles_then_attaches_and_connects);
    RUN_TEST(test_enable_3w_leaves_tx_power_off_unless_board_says);
    RUN_TEST(test_class_change_goes_through_safe_state);
    RUN_TEST(test_disable_disconnects_first);
    return UNITY_END(); }
