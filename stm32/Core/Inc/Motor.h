#ifndef __MOTOR_H__
#define __MOTOR_H__

#include "main.h"
#include "tim.h"
#include <stdbool.h>

/* 电机编号 */
typedef enum {
    MOTOR_LEFT = 0,   /* PA0 - TIM2_CH1 */
    MOTOR_RIGHT = 1    /* PA1 - TIM2_CH2 */
} Motor_TypeDef;

/* 电机方向 */
typedef enum {
    MOTOR_FORWARD  = 0,
    MOTOR_BACKWARD = 1
} Motor_DirTypeDef;

/* PWM 参数（与 CubeMX TIM2 配置一致）*/
#define MOTOR_PWM_MAX      1000    /* 最大占空比值 (对应 Period) */
#define MOTOR_PWM_FREQ     1000    /* PWM 频率 1kHz */

/* 电机方向控制引脚 (H 桥 INA/INB)
   INA=0, INB=1 → 正转
   INA=1, INB=0 → 反转
   INA=1, INB=1 → 刹车停止 */
#define MOTOR_LEFT_INA_PORT     L_INA_GPIO_Port
#define MOTOR_LEFT_INA_PIN      L_INA_Pin
#define MOTOR_LEFT_INB_PORT     L_INB_GPIO_Port
#define MOTOR_LEFT_INB_PIN      L_INB_Pin
#define MOTOR_RIGHT_INA_PORT    R_INA_GPIO_Port
#define MOTOR_RIGHT_INA_PIN     R_INA_Pin
#define MOTOR_RIGHT_INB_PORT    R_INB_GPIO_Port
#define MOTOR_RIGHT_INB_PIN     R_INB_Pin

/*==================== 常用函数 ====================*/

/* 启动 PWM 输出 */
void Motor_Start(Motor_TypeDef motor);

/* 停止 PWM 输出 */
void Motor_Stop(Motor_TypeDef motor);

/* 设置速度（百分比 0~100）*/
void Motor_SetSpeed(Motor_TypeDef motor, uint8_t percent);

/* 设置原始占空比（0~MOTOR_PWM_MAX）*/
void Motor_SetPulse(Motor_TypeDef motor, uint16_t pulse);

/* 设置方向（Forward/Backward）*/
void Motor_SetDirection(Motor_TypeDef motor, Motor_DirTypeDef dir);

/* 正转（设置方向 + 保持当前占空比）*/
void Motor_Backward(void);

/* 反转（设置方向 + 保持当前占空比）*/
void Motor_Forward(void);

/* 制动（急停）*/
void Motor_Brake(Motor_TypeDef motor);

/* 平滑调速：从当前速度渐变到目标速度 */
void Motor_RampTo(Motor_TypeDef motor, uint16_t targetPulse, uint16_t stepDelayMs);

/* 获取当前占空比 */
uint16_t Motor_GetPulse(Motor_TypeDef motor);

/* 获取当前方向: 1=正转, 0=反转/停止 */
int Motor_GetDirection(Motor_TypeDef motor);

/* 获取最近一次超声波测距 (cm) */
int32_t Motor_GetUltraL(void);
int32_t Motor_GetUltraR(void);

/* PS2 遥控: LY=油门, RX=转向 */
void Motor_PS2Control(int ly_raw, int rx_raw);

/* PS2 按钮: START 启动电机+亮灯, SELECT 停止+灭灯 */
void Motor_PS2Switch(int ps2_key);

/* 电机是否已启动 */
int Motor_IsRunning(void);

/* 行驶控制: 自动=超声波避障, 手动=PS2摇杆 */
void Motor_DriveUpdate(bool motorOn, bool isAuto);
void Motor_PrintVOFA(void);
void Motor_NfcTriggerStop(void);
bool Motor_NfcStopped(void);
void Motor_NfcClear(void);
bool Motor_WallReady(void);
void Motor_ToggleWall(void);
bool Motor_Calibrate(void);
void Motor_StartCalibrate(void); /* 进入校准模式 */
void Motor_StopCalibrate(void);  /* 退出校准模式 */

/* NFC 寻卡振荡: 检测到卡后小车前后微动保持卡在读取范围 */
void Motor_NfcStartHunt(void);
void Motor_NfcStopHunt(void);
bool Motor_NfcIsHunting(void);
bool Motor_NfcHuntTimeout(void);
uint32_t Motor_NfcHuntElapsed(void);  /* 振荡已过时间 ms */

#endif /* __MOTOR_H__ */
