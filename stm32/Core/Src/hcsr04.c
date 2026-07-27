/**
 * @file    hcsr04.c
 * @brief   HC-SR04 超声波测距 (DWT 轮询 + EMA, 阻塞 ~60ms max)
 */

#include "hcsr04.h"
#include <stdio.h>

#define TIMEOUT_US  15000    /* 15ms ECHO 超时 (~2.5m) */
#define MIN_US      10
#define MAX_US      30000

static void delay_us(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * (SystemCoreClock / 1000000U);
    while ((DWT->CYCCNT - start) < ticks);
}

static uint32_t gMM[HCSR04_NUM];  /* EMA 滤波值 */

/* 获取 TRIG/ECHO 引脚 */
static void GetPins(HCSR04_Id id, GPIO_TypeDef **trigPort, uint16_t *trigPin,
                    GPIO_TypeDef **echoPort, uint16_t *echoPin)
{
    if (id == HCSR04_1) {
        *trigPort = HC1_TRIG_GPIO_Port; *trigPin = HC1_TRIG_Pin;
        *echoPort = HC1_ECHO_GPIO_Port; *echoPin = HC1_ECHO_Pin;
    } else {
        *trigPort = HC2_TRIG_GPIO_Port; *trigPin = HC2_TRIG_Pin;
        *echoPort = HC2_ECHO_GPIO_Port; *echoPin = HC2_ECHO_Pin;
    }
}

/* EMA 滤波 */
static void Filter(HCSR04_Id id, uint32_t rawMM)
{
    if (gMM[id] == 0)
        gMM[id] = rawMM;
    else
        gMM[id] = rawMM;  /* 无滤波, 原始值 */
}

void HCSR04_Init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    gMM[0] = gMM[1] = 0;

    /* ECHO 脚改普通输入, 避免 AF_PP 模式干扰 */
    GPIO_InitTypeDef s = {0};
    s.Pin = HC1_ECHO_Pin | HC2_ECHO_Pin;
    s.Mode = GPIO_MODE_INPUT;
    s.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &s);

    HAL_Delay(500);
    printf("  HCSR04 init OK\n");
}

/* 触发 + 测距 + 滤波 (阻塞 ~60ms max), 主循环 200ms 调用 */
void HCSR04_Update(HCSR04_Id id)
{
    GPIO_TypeDef *trigP, *echoP;
    uint16_t trigPin, echoPin;
    GetPins(id, &trigP, &trigPin, &echoP, &echoPin);

    /* TRIG 25μs */
    HAL_GPIO_WritePin(trigP, trigPin, GPIO_PIN_SET);
    delay_us(25);
    HAL_GPIO_WritePin(trigP, trigPin, GPIO_PIN_RESET);

    /* 等 ECHO 上升沿 */
    uint32_t t0 = DWT->CYCCNT;
    uint32_t to = TIMEOUT_US * (SystemCoreClock / 1000000);
    while (!HAL_GPIO_ReadPin(echoP, echoPin)) {
        if ((int32_t)(DWT->CYCCNT - t0) > (int32_t)to) return;
    }
    uint32_t t1 = DWT->CYCCNT;

    /* 等 ECHO 下降沿 */
    while (HAL_GPIO_ReadPin(echoP, echoPin)) {
        if ((int32_t)(DWT->CYCCNT - t1) > (int32_t)to) return;
    }
    uint32_t t2 = DWT->CYCCNT;

    int32_t us = (int32_t)(t2 - t1) / (SystemCoreClock / 1000000);
    if (us >= MIN_US && us <= MAX_US)
        Filter(id, (uint32_t)us * 100 / 58);
}

uint32_t HCSR04_GetMM(HCSR04_Id id)
{
    return gMM[id];
}
