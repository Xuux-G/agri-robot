/**
 * @file    servo.c
 * @brief   舵机驱动 (TIM2 CH1=PA5, CH2=PB3, 50Hz PWM)
 *          0.5ms→0°, 2.5ms→180°, 1μs/tick
 */

#include "servo.h"
#include "tim.h"

#define MIN_PULSE  500    /* 0.5ms → 0° */
#define MAX_PULSE  2500   /* 2.5ms → 180° */

void Servo_Init(void)
{
HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2);
Servo_SetAngle(SERVO_1, 90);  /* 中位 */
Servo_SetAngle(SERVO_2, 90);
}

void Servo_SetAngle(Servo_Id id, uint8_t angle)
{
if (angle > 180) angle = 180;
uint16_t pulse = MIN_PULSE + (uint32_t)angle * (MAX_PULSE - MIN_PULSE) / 180;
uint32_t ch = (id == SERVO_1) ? TIM_CHANNEL_1 : TIM_CHANNEL_2;
__HAL_TIM_SET_COMPARE(&htim2, ch, pulse);
}
