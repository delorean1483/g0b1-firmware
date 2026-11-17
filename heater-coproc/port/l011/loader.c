#include "htr_board.h"
#include "port.h"
#include "l011_update.h"
#include "loader_core.h"

#define FLASH_ERRORS (FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_SIZERR | \
                      FLASH_SR_NOTZEROERR | FLASH_SR_RDERR | FLASH_SR_FWWERR)
static uint8_t frame[L011_FRAME_MAX];
static uint8_t reply[4] = {L011_BOOT_ID, 0, 0, 0};

uint32_t loader_read(uint32_t address) { return *(volatile uint32_t *)address; }

static bool flash_op(uint32_t address, uint32_t value, bool erase)
{
    if (address < L011_APP_BASE || address >= L011_META_BASE + L011_PAGE_SIZE ||
        address % (erase ? L011_PAGE_SIZE : 4u)) return false;
    FLASH->PEKEYR = 0x89ABCDEFu;
    FLASH->PEKEYR = 0x02030405u;
    FLASH->PRGKEYR = 0x8C9DAEBFu;
    FLASH->PRGKEYR = 0x13141516u;
    if (FLASH->PECR & (FLASH_PECR_PELOCK | FLASH_PECR_PRGLOCK)) return false;
    FLASH->SR = FLASH_ERRORS | FLASH_SR_EOP;
    if (erase) FLASH->PECR |= FLASH_PECR_ERASE | FLASH_PECR_PROG;
    *(volatile uint32_t *)address = value;
    while (FLASH->SR & FLASH_SR_BSY) {}
    bool ok = (FLASH->SR & FLASH_ERRORS) == 0;
    FLASH->PECR = FLASH_PECR_PELOCK | FLASH_PECR_PRGLOCK | FLASH_PECR_OPTLOCK;
    return ok;
}
bool loader_erase(uint32_t address) { return flash_op(address, 0u, true); }
bool loader_word(uint32_t address, uint32_t value)
{
    return flash_op(address, value, false) && loader_read(address) == value;
}

__attribute__((naked, noreturn)) static void jump(void)
{
    __asm volatile ("ldr r0, =0x08001000\n"
                    "ldr r1, [r0]\n"
                    "msr msp, r1\n"
                    "ldr r1, [r0, #4]\n"
                    "bx r1");
}

int main(void)
{
    volatile uint32_t *const request = (volatile uint32_t *)L011_RESET_ADDR;
    /* Startup preserves the first eight SRAM bytes in both layouts. */
    bool requested = (RCC->CSR & RCC_CSR_SFTRSTF) &&
                     request[0] == L011_RESET_MAGIC && request[1] == ~L011_RESET_MAGIC;
    request[0] = request[1] = 0;
    RCC->CSR |= RCC_CSR_RMVF;
    if (!requested && loader_valid()) {
        SCB->VTOR = L011_APP_BASE;
        __DSB();
        __ISB();
        jump();
    }
    port_clock_init();
    port_gpio_init();
    port_iwdg_start();
    RCC->CCIPR = (RCC->CCIPR & ~RCC_CCIPR_I2C1SEL) | RCC_CCIPR_I2C1SEL_1;
    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;
    I2C1->TIMINGR = HB_I2C_TIMINGR;
    I2C1->OAR1 = I2C_OAR1_OA1EN | (L011_BOOT_ADDR << 1);
    I2C1->CR1 = I2C_CR1_PE;

    unsigned count = 0, tx = 0;
    bool reading = false, bad = false, reboot = false;
    for (;;) {
        port_iwdg_kick();
        uint32_t flags = I2C1->ISR;
        if (flags & I2C_ISR_ADDR) {
            reading = (flags & I2C_ISR_DIR) != 0;
            count = tx = 0;
            bad = false;
            if (reading) I2C1->ISR = I2C_ISR_TXE;
            I2C1->ICR = I2C_ICR_ADDRCF;
        }
        if (flags & I2C_ISR_RXNE) {
            uint8_t byte = (uint8_t)I2C1->RXDR;
            if (count < sizeof(frame)) frame[count++] = byte;
            else bad = true;
        }
        if (flags & I2C_ISR_TXIS) I2C1->TXDR = tx < sizeof(reply) ? reply[tx++] : 0xFFu;
        if (flags & I2C_ISR_NACKF) I2C1->ICR = I2C_ICR_NACKCF;
        if (flags & (I2C_ISR_BERR | I2C_ISR_ARLO | I2C_ISR_OVR)) {
            I2C1->ICR = I2C_ICR_BERRCF | I2C_ICR_ARLOCF | I2C_ICR_OVRCF;
            bad = true;
        }
        if (flags & I2C_ISR_STOPF) {
            I2C1->ICR = I2C_ICR_STOPCF;
            if (reading && reboot && tx >= sizeof(reply)) NVIC_SystemReset();
            if (reading || count < 3u) continue; /* one-byte status register pointer */
            I2C1->CR1 = 0u; /* NACK new addresses while flash is busy. */
            bool ok = !bad && loader_command(frame, count);
            reply[1] = ok ? 1u : 2u;
            reply[2] = frame[count - 2u];
            reply[3] = frame[count - 1u];
            reboot = ok && frame[0] == L011_REBOOT;
            I2C1->CR1 = I2C_CR1_PE;
        }
    }
}
