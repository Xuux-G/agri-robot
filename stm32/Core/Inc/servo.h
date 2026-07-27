#ifndef __SERVO_H__
#define __SERVO_H__

#include "main.h"

/* 舵机编号 */
typedef enum { SERVO_1 = 0, SERVO_2 = 1 } Servo_Id;

/* 初始化: 启动 TIM2 CH1/CH2 PWM */
void Servo_Init(void);

/* 设置角度 (0~180°) */
void Servo_SetAngle(Servo_Id id, uint8_t angle);

#endif
