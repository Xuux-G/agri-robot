#ifndef __HCSR04_H__
#define __HCSR04_H__

#include "main.h"

typedef enum {
    HCSR04_1 = 0,
    HCSR04_2 = 1,
    HCSR04_NUM
} HCSR04_Id;

/* 初始化 (使能 DWT) */
void HCSR04_Init(void);

/* 触发 + 测距 + EMA 滤波 (阻塞 ~60ms max), 主循环 200ms 调用 */
void HCSR04_Update(HCSR04_Id id);

/* 获取滤波距离 (mm*100, 除100=cm, 0=未测) */
uint32_t HCSR04_GetMM(HCSR04_Id id);

#endif
