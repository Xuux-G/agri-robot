#ifndef __JY61_H__
#define __JY61_H__

#include "main.h"
#include <stdbool.h>

/* 初始化: UART5 中断接收 */
void JY61_Init(void);

/* 获取偏航角 Yaw (°), 校准后 0=初始朝向, 右转增加 */
float JY61_GetYaw(void);

/* 硬件零位校准: 发送指令给 JY61P, 当前朝向置为 0° (耗时约 3.5s) */
void JY61_ZeroYaw(void);

/* UART5 中断处理 (在 it.c 中调用) */
void JY61_IRQHandler(uint8_t byte);

#endif
