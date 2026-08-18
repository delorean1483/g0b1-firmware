/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32g0xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define STARTER_Pin GPIO_PIN_11
#define STARTER_GPIO_Port GPIOC
#define FUEL_PUMP_Pin GPIO_PIN_12
#define FUEL_PUMP_GPIO_Port GPIOC
#define TRUCK_IGNITION_Pin GPIO_PIN_2
#define TRUCK_IGNITION_GPIO_Port GPIOD
#define OIL_PRESSURE_Pin GPIO_PIN_6
#define OIL_PRESSURE_GPIO_Port GPIOD
#define HEAT_REVERSER_Pin GPIO_PIN_4
#define HEAT_REVERSER_GPIO_Port GPIOB
#define COMPRESSOR_CLUTCH_Pin GPIO_PIN_5
#define COMPRESSOR_CLUTCH_GPIO_Port GPIOB
#define GLOW_PLUG_Pin GPIO_PIN_8
#define GLOW_PLUG_GPIO_Port GPIOB
#define CONDENSER_FAN_Pin GPIO_PIN_9
#define CONDENSER_FAN_GPIO_Port GPIOB
#define EVAP_FAN_Pin GPIO_PIN_10
#define EVAP_FAN_GPIO_Port GPIOC

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
