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
#include "stm32h7xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "Motor.h"
/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */
extern int PS2_LX, PS2_LY, PS2_RX, PS2_RY, PS2_KEY;
/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define PS2_DI_Pin GPIO_PIN_3
#define PS2_DI_GPIO_Port GPIOE
#define PS2_CS_Pin GPIO_PIN_4
#define PS2_CS_GPIO_Port GPIOE
#define PS2_CLK_Pin GPIO_PIN_5
#define PS2_CLK_GPIO_Port GPIOE
#define PS_DO_Pin GPIO_PIN_6
#define PS_DO_GPIO_Port GPIOE
#define LD1_Pin GPIO_PIN_13
#define LD1_GPIO_Port GPIOC
#define PUMP3_Pin GPIO_PIN_0
#define PUMP3_GPIO_Port GPIOC
#define PUMP4_Pin GPIO_PIN_1
#define PUMP4_GPIO_Port GPIOC
#define PUMP5_Pin GPIO_PIN_2
#define PUMP5_GPIO_Port GPIOC
#define PUMP6_Pin GPIO_PIN_3
#define PUMP6_GPIO_Port GPIOC
#define HC1_TRIG_Pin GPIO_PIN_0
#define HC1_TRIG_GPIO_Port GPIOA
#define HC2_TRIG_Pin GPIO_PIN_1
#define HC2_TRIG_GPIO_Port GPIOA
#define NFC1_TX_Pin GPIO_PIN_2
#define NFC1_TX_GPIO_Port GPIOA
#define NFC2_RX_Pin GPIO_PIN_3
#define NFC2_RX_GPIO_Port GPIOA
#define Servo1_Pin GPIO_PIN_5
#define Servo1_GPIO_Port GPIOA
#define HC1_ECHO_Pin GPIO_PIN_6
#define HC1_ECHO_GPIO_Port GPIOA
#define HC2_ECHO_Pin GPIO_PIN_7
#define HC2_ECHO_GPIO_Port GPIOA
#define PUMP1_Pin GPIO_PIN_4
#define PUMP1_GPIO_Port GPIOC
#define PUMP2_Pin GPIO_PIN_5
#define PUMP2_GPIO_Port GPIOC
#define ESP32_RX_Pin GPIO_PIN_7
#define ESP32_RX_GPIO_Port GPIOE
#define ESP32_TX_Pin GPIO_PIN_8
#define ESP32_TX_GPIO_Port GPIOE
#define L_INA_Pin GPIO_PIN_11
#define L_INA_GPIO_Port GPIOE
#define L_INB_Pin GPIO_PIN_12
#define L_INB_GPIO_Port GPIOE
#define R_INA_Pin GPIO_PIN_13
#define R_INA_GPIO_Port GPIOE
#define R_INB_Pin GPIO_PIN_14
#define R_INB_GPIO_Port GPIOE
#define JY60_RX_Pin GPIO_PIN_12
#define JY60_RX_GPIO_Port GPIOB
#define JY60_TX_Pin GPIO_PIN_13
#define JY60_TX_GPIO_Port GPIOB
#define Emm_v5_TX_Pin GPIO_PIN_6
#define Emm_v5_TX_GPIO_Port GPIOC
#define Emm_v5_RX_Pin GPIO_PIN_7
#define Emm_v5_RX_GPIO_Port GPIOC
#define NFC2_RXA11_Pin GPIO_PIN_11
#define NFC2_RXA11_GPIO_Port GPIOA
#define NFC2_TX_Pin GPIO_PIN_12
#define NFC2_TX_GPIO_Port GPIOA
#define Servo2_Pin GPIO_PIN_3
#define Servo2_GPIO_Port GPIOB
#define CAM_RX_Pin GPIO_PIN_0
#define CAM_RX_GPIO_Port GPIOE
#define CAM_TX_Pin GPIO_PIN_1
#define CAM_TX_GPIO_Port GPIOE

/* USER CODE BEGIN Private defines */
#define PS2_DO_Pin       PS_DO_Pin
#define PS2_DO_GPIO_Port  PS_DO_GPIO_Port
/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
