#ifndef TEST_I2C2_MAIN_H
#define TEST_I2C2_MAIN_H
#include <stdint.h>
typedef enum { HAL_OK=0, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef enum { GPIO_PIN_RESET=0, GPIO_PIN_SET=1 } GPIO_PinState;
typedef struct { uint32_t CR1, CR2, ISR; } I2C_TypeDef;
typedef struct { uint32_t Timing; } I2C_InitTypeDef;
typedef struct { I2C_TypeDef *Instance; I2C_InitTypeDef Init; uint32_t ErrorCode, State; uint16_t XferCount, XferSize; uint8_t *pBuffPtr; } I2C_HandleTypeDef;
typedef struct { int unused; } GPIO_TypeDef;
typedef struct { uint32_t Pin, Mode, Pull, Speed, Alternate; } GPIO_InitTypeDef;
extern GPIO_TypeDef test_gpioa, test_gpiob;
#define GPIOA (&test_gpioa)
#define GPIOB (&test_gpiob)
#define GPIO_PIN_11 (1u<<11)
#define GPIO_PIN_14 (1u<<14)
#define GPIO_MODE_OUTPUT_OD 1u
#define GPIO_NOPULL 0u
#define GPIO_SPEED_FREQ_LOW 0u
#define I2C_CR1_ANFOFF (1u<<12)
#define I2C_CR1_DNF (15u<<8)
#define I2C_CR1_DNF_Pos 8u
#define I2C_ANALOGFILTER_DISABLE 1u
#define I2C_ANALOGFILTER_ENABLE 0u
#define I2C_MEMADD_SIZE_8BIT 1u
#define I2C_ISR_BUSY (1u<<15)
#define HAL_I2C_ERROR_AF 4u
#define HAL_I2C_ERROR_ARLO 2u
#define HAL_I2C_ERROR_TIMEOUT 32u
void test_disable(void);
void test_reset(void);
#define __HAL_I2C_DISABLE(h) test_disable()
#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOB_CLK_ENABLE() ((void)0)
#define __HAL_RCC_I2C2_CLK_ENABLE() ((void)0)
#define __HAL_RCC_I2C2_FORCE_RESET() test_reset()
#define __HAL_RCC_I2C2_RELEASE_RESET() ((void)0)
uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t ms);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *, uint16_t);
void HAL_GPIO_WritePin(GPIO_TypeDef *, uint16_t, GPIO_PinState);
void HAL_GPIO_Init(GPIO_TypeDef *, GPIO_InitTypeDef *);
HAL_StatusTypeDef HAL_I2C_DeInit(I2C_HandleTypeDef *);
HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *);
HAL_StatusTypeDef HAL_I2CEx_ConfigAnalogFilter(I2C_HandleTypeDef *, uint32_t);
HAL_StatusTypeDef HAL_I2CEx_ConfigDigitalFilter(I2C_HandleTypeDef *, uint32_t);
uint32_t HAL_I2C_GetError(I2C_HandleTypeDef *);
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *, uint16_t, uint16_t, uint16_t, uint8_t *, uint16_t, uint32_t);
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *, uint16_t, uint16_t, uint16_t, uint8_t *, uint16_t, uint32_t);
#endif
