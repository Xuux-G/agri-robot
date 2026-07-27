#ifndef __PSTWO_H
#define __PSTWO_H

#include "main.h"
#include <stdbool.h>

/* PS2 引脚定义 (GPIOE) */
#define PS2_DI_READ()       HAL_GPIO_ReadPin(PS2_DI_GPIO_Port, PS2_DI_Pin)

#define PS2_DO_H()          HAL_GPIO_WritePin(PS2_DO_GPIO_Port, PS2_DO_Pin, GPIO_PIN_SET)
#define PS2_DO_L()          HAL_GPIO_WritePin(PS2_DO_GPIO_Port, PS2_DO_Pin, GPIO_PIN_RESET)

#define PS2_CS_H()          HAL_GPIO_WritePin(PS2_CS_GPIO_Port, PS2_CS_Pin, GPIO_PIN_SET)
#define PS2_CS_L()          HAL_GPIO_WritePin(PS2_CS_GPIO_Port, PS2_CS_Pin, GPIO_PIN_RESET)

#define PS2_CLK_H()         HAL_GPIO_WritePin(PS2_CLK_GPIO_Port, PS2_CLK_Pin, GPIO_PIN_SET)
#define PS2_CLK_L()         HAL_GPIO_WritePin(PS2_CLK_GPIO_Port, PS2_CLK_Pin, GPIO_PIN_RESET)

/* 按键常量 */
#define PSB_SELECT      1
#define PSB_L3          2
#define PSB_R3          3
#define PSB_START       4
#define PSB_PAD_UP      5
#define PSB_PAD_RIGHT   6
#define PSB_PAD_DOWN    7
#define PSB_PAD_LEFT    8
#define PSB_L2          9
#define PSB_R2          10
#define PSB_L1          11
#define PSB_R1          12
#define PSB_GREEN       13
#define PSB_RED         14
#define PSB_BLUE        15
#define PSB_PINK        16

#define PSB_TRIANGLE    13
#define PSB_CIRCLE      14
#define PSB_CROSS       15
#define PSB_SQUARE      16

/* 摇杆数据索引 */
#define PSS_RX 5
#define PSS_RY 6
#define PSS_LX 7
#define PSS_LY 8

extern uint8_t PS2_Data[9];
extern uint16_t PS2_MASK[16];
extern uint16_t PS2_Handkey;
extern volatile int PS2_LastRelease;

void PS2_Init(void);
uint8_t PS2_RedLight(void);
void PS2_ReadData(void);
void PS2_Cmd(uint8_t CMD);
uint8_t PS2_DataKey(void);
uint8_t PS2_AnologData(uint8_t button);
void PS2_ClearData(void);
void PS2_Vibration(uint8_t motor1, uint8_t motor2);
void PS2_EnterConfing(void);
void PS2_TurnOnAnalogMode(void);
void PS2_VibrationMode(void);
void PS2_ExitConfing(void);
void PS2_SetInit(void);
void PS2_Receive(void);
void PS2_HandleButtons(void);
void PS2_PrintDebug(void);

void PS2_Receive(void);
/* 模式切换 + 电机启停 (L2=自动, R2=手动, START=启停) */
void PS2_ModeHandler(bool *isAuto, bool *motorOn);

#endif
