/**
 * @file    jy61.c
 * @brief   JY61/JY61P 陀螺仪驱动 (UART5, 9600bps)
 *          55 53 RL RH PL PH YL YH _ _ CHK
 */

#include "jy61.h"
#include "usart.h"
#include <stdio.h>

static volatile float gYaw   = 0.0f;
static volatile float gPitch = 0.0f;
static volatile bool  gValid = false;

static uint8_t  gRxBuf[11];
static uint8_t  gRxIdx = 0;
static uint8_t  gRxState = 0;  /* 0=等0x55, 1=等0x53, 2=收数据 */

void JY61_Init(void)
{
    HAL_NVIC_SetPriority(UART5_IRQn, 1, 0);
    HAL_UARTEx_SetRxFifoThreshold(&huart5, UART_RXFIFO_THRESHOLD_1_8);
    HAL_UARTEx_EnableFifoMode(&huart5);
    __HAL_UART_ENABLE_IT(&huart5, UART_IT_RXNE);
    printf("  JY61 init OK\n");
}

float JY61_GetYaw(void)
{
    return gYaw;
}

/* JY61P 硬件零位校准: 发送校准指令, 模块自动将当前朝向置为 0° */
void JY61_ZeroYaw(void)
{
    uint8_t INS_1[5] = {0xFF, 0xAA, 0x69, 0x88, 0xB5};  /* 进入校准模式 */
    uint8_t INS_2[5] = {0xFF, 0xAA, 0x61, 0x01, 0x00};  /* 执行校准 */
    uint8_t INS_3[5] = {0xFF, 0xAA, 0x00, 0x00, 0x00};  /* 退出校准 */

    HAL_UART_Transmit(&huart5, INS_1, 5, 100);
    HAL_Delay(200);
    HAL_UART_Transmit(&huart5, INS_2, 5, 100);
    HAL_Delay(3000);
    HAL_UART_Transmit(&huart5, INS_3, 5, 100);
    HAL_Delay(200);
    printf("  JY61 zero OK\n");
}

/* ISR 中调用, 逐字节喂入 */
void JY61_IRQHandler(uint8_t b)
{
    switch (gRxState) {
    case 0:
        if (b == 0x55) { gRxBuf[0]=b; gRxIdx=1; gRxState=1; }
        break;
    case 1:
        if (b == 0x53) { gRxBuf[1]=b; gRxIdx=2; gRxState=2; }
        else { gRxState=0; gRxIdx=0; }
        break;
    case 2:
        gRxBuf[gRxIdx++] = b;
        if (gRxIdx >= 11) {
            gRxState = 0; gRxIdx = 0;
            uint8_t sum = 0;
            for (int i=0;i<10;i++) sum += gRxBuf[i];
            if (sum == gRxBuf[10]) {
                int16_t raw;
                gPitch = (float)((int16_t)(gRxBuf[5]<<8|gRxBuf[4])) / 32768.0f * 180.0f;
                raw    = (int16_t)(gRxBuf[7]<<8|gRxBuf[6]);
                gYaw   = (float)raw / 32768.0f * 180.0f;
                gValid = true;
            }
        }
        break;
    }
}
