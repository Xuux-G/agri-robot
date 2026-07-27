/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include "Motor.h"
#include "pstwo.h"
#include "pn532.h"
#include "pn532_2.h"
#include "hcsr04.h"
#include "emm_v5.h"
#include "camera.h"
#include "jy61.h"
#include "spray_ctrl.h"
#include "nfc_handler.h"

extern int gWallSide;


/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_TIM4_Init();
  MX_TIM6_Init();
  MX_USART2_UART_Init();
  MX_TIM3_Init();
  MX_USART6_UART_Init();
  MX_UART4_Init();
  MX_USART3_UART_Init();
  MX_UART5_Init();
  MX_TIM2_Init();
  MX_UART7_Init();
  MX_UART8_Init();
  /* USER CODE BEGIN 2 */

  printf("=== Sprayer Boot ===\n");

  PS2_Init();
  PS2_SetInit();
  PS2_LX = PS2_LY = PS2_RX = PS2_RY = 128;
  PS2_KEY = 0;
  printf("  PS2 OK\n");
  HAL_Delay(300);


  HAL_GPIO_WritePin(L_INA_GPIO_Port, L_INA_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(L_INB_GPIO_Port, L_INB_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(R_INA_GPIO_Port, R_INA_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(R_INB_GPIO_Port, R_INB_Pin, GPIO_PIN_SET);
  Motor_SetPulse(MOTOR_LEFT,  0);
  Motor_SetPulse(MOTOR_RIGHT, 0);
  Motor_Stop(MOTOR_LEFT);
  Motor_Stop(MOTOR_RIGHT);
  HAL_TIM_Base_Start_IT(&htim6);
  printf("  Motor OK\n");

  PN532_Init();
  PN532_2_Init();
  HAL_NVIC_SetPriority(TIM3_IRQn, 0, 1);  
  HCSR04_Init();
  JY61_Init();
  JY61_ZeroYaw();   
  Emm_V5_InitRx();
  Emm_V5_En_Control(1, true, false);
  HAL_Delay(10);
  Emm_V5_Reset_CurPos_To_Zero(1);           /* X 归零 */
  HAL_Delay(10);
  Emm_V5_Vel_Control(1, 0, 0, 20, false);
  HAL_Delay(10);
  Emm_V5_En_Control(2, true, false);
  HAL_Delay(10);
  Emm_V5_Pos_Control(2, 0, 100, 20, 16000, false, false);
  HAL_Delay(10);
  Emm_V5_En_Control(3, true, false);
  HAL_Delay(10);
  Emm_V5_Pos_Control(3, 0, 100, 20, 17000, false, false);
  HAL_Delay(10);
  Emm_V5_Synchronous_motion(0);
  HAL_Delay(10);
  Camera_Init();
  Spray_Init();

  Motor_Start(MOTOR_LEFT);
  Motor_Start(MOTOR_RIGHT);
  static bool motorOn = false, isAuto = false;
  HAL_GPIO_WritePin(LD1_GPIO_Port, LD1_Pin, GPIO_PIN_SET);
  printf("  L2=AUTO R2=MANUAL START=ON/OFF\n");
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
  
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    PS2_ModeHandler(&isAuto, &motorOn);
    LED_StatusUpdate(motorOn, isAuto);
    Motor_DriveUpdate(motorOn, isAuto);

    if (!isAuto) {
        if (Motor_NfcStopped()) Motor_NfcClear();
        HAL_Delay(10); continue;
    }

    if (Spray_IsBusy()) {
        Spray_Update();
        HAL_Delay(20); continue;
    }

    if (!motorOn) {
        static uint32_t lastYaw = 0;
        if (HAL_GetTick() - lastYaw > 300) {
            lastYaw = HAL_GetTick();
            printf("Yaw:%d.%d US:%d,%dcm\r\n",
                   (int)JY61_GetYaw(), (int)(JY61_GetYaw()*10)%10,
                   (int)Motor_GetUltraL(), (int)Motor_GetUltraR());
        }
    }

    if (motorOn && isAuto) {
        static uint32_t lp = 0;
        if (HAL_GetTick()-lp > 200) { lp=HAL_GetTick(); Motor_PrintVOFA(); }
    }

    PN532_Worker();
    PN532_2_Worker();

    static int nfcTrig = 0;
    static bool sprayed = false;
    static uint32_t nfcCoolUntil = 0;
    static int gBlockCount = 0;

    if (PN532_JustWritten())  nfcTrig = 1;
    if (PN532_2_JustWritten()) nfcTrig = 2;

    if (nfcTrig && motorOn && !Motor_NfcStopped()) {
        if (HAL_GetTick() < nfcCoolUntil) { nfcTrig = 0; }
        else { Motor_NfcTriggerStop(); gBlockCount++; }
    }

    if (Motor_NfcStopped()) {
        static uint32_t ss = 0;
        if (ss == 0) ss = HAL_GetTick();

        if (gNfcCardNum != 0 && !Spray_IsBusy() && !sprayed) {
            gBlockCount = gNfcCardNum;
            Spray_SetNfcSide(nfcTrig);
            Spray_Start(PEST_MED);
            sprayed = true;
        } else if (HAL_GetTick() - ss > 5000) {
            int fb = (gBlockCount > 0) ? gBlockCount : 1;
            gNfcCardNum = fb;
            { uint8_t f[]={0xAA,0xAA,(uint8_t)fb,0xFF,0xFF};
              HAL_UART_Transmit(&huart7, f, 5, 100);
              printf("       UART7: AA AA 0%d FF FF (timeout)\n", fb); }
            ss = 0;
            printf("[NFC] Timeout, force CARD=%d\n", fb);
        }
    } else { sprayed = false; }

    if (!Spray_IsBusy()) Spray_Update();

    if (!Spray_IsBusy() && Motor_NfcStopped() && sprayed) {
        static bool _done6 = false;
        if (gNfcCardNum == 6 && !_done6) {
            _done6 = true;
            { uint8_t f[]={0xAA,0xAA,0x07,0xFF,0xFF};
              HAL_UART_Transmit(&huart7, f, 5, 100);
              printf("       UART7: AA AA 07 FF FF (all done)\n"); }
            Motor_Stop(MOTOR_LEFT);
            Motor_Stop(MOTOR_RIGHT);
            Motor_NfcClear();
            gNfcCardNum = 0; nfcTrig = 0; sprayed = false;
            printf("[Spray] ALL 6 DONE, stop\n");
            while(1) { HAL_Delay(100); }
        }
        Motor_ToggleWall();
        Motor_NfcClear();
        gNfcCardNum = 0;
        nfcTrig = 0;
        sprayed = false;
        nfcCoolUntil = HAL_GetTick() + 3000;
    }

    HAL_Delay(20);
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /*AXI clock gating */
  RCC->CKGAENR = 0xFFFFFFFF;

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = 64;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 70;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks*/
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_6) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x0;
  MPU_InitStruct.Size = MPU_REGION_SIZE_4GB;
  MPU_InitStruct.SubRegionDisable = 0x87;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
