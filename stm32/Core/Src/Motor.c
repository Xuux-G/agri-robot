#include "Motor.h"
#include <stdlib.h>
#include <stdio.h>
#include "pstwo.h"
#include "hcsr04.h"
#include "jy61.h"
//#include "auto_drive.h"

/* 当前占空比，用于读取和渐变调速 */
static uint16_t motorPulse[2] = {0, 0};

/* VOFA+ PID 调试数据 */
static struct { int32_t dL, dR; float err, adj; int lSpd, rSpd; int32_t ultraL, ultraR; } gPidDbg;

/* ─── 贴墙参数 ─── */
#define BASE_SPD       15
int gWallSide = 1;  /* 0=左墙 1=右墙, 动态切换 */
#define WALL_TARGET_CM    11.0f   /* 贴墙目标距离 cm */
#define WALL_RANGE_CM     1.0f   /* 允许浮动范围 ±1cm */
#define WALL_MAX_CM       30      /* 超过此纯陀螺 */
#define WALL_KP           4.0f   /* 超声误差→转向增益 (小=缓慢靠墙) */
#define WALL_MAX_ADJ      6      /* 超声转向最大修正 */
#define GYRO_KP           4.0f   /* 陀螺 P 增益 */
#define GYRO_DONE         0.8f   /* 陀螺完成阈值 ±° */


/* NFC 触发立即停车状态 */
static bool     gNfcStopped = false;   /* 已停车 */
static bool     gCalibrating = false;  /* 陀螺校准中 */
static bool     gWallReady  = false;   /* 贴墙到位+陀螺纠完 */
static bool     gWallReset  = false;   /* 换墙重置PID */
bool Motor_WallReady(void) { return gWallReady; }
void Motor_ToggleWall(void) {
    gWallSide = !gWallSide;
    gWallReset = true;
    printf("[Motor] Wall side→%s\n", gWallSide?"R":"L");
}

void Motor_StartCalibrate(void) { gCalibrating = true; }
void Motor_StopCalibrate(void)  { gCalibrating = false; }

/* 原地旋转校准陀螺: 左右轮相反转, |yaw|<0.5°完成 */
bool Motor_Calibrate(void)
{
    float yaw = JY61_GetYaw();
    #define CAL_SPEED 12
    if (yaw < -0.5f) {
        Motor_Start(MOTOR_LEFT); Motor_Start(MOTOR_RIGHT);
        Motor_SetDirection(MOTOR_LEFT,  MOTOR_BACKWARD);
        Motor_SetDirection(MOTOR_RIGHT, MOTOR_FORWARD);
        Motor_SetPulse(MOTOR_LEFT,  MOTOR_PWM_MAX * CAL_SPEED / 100);
        Motor_SetPulse(MOTOR_RIGHT, MOTOR_PWM_MAX * CAL_SPEED / 100);
        return false;
    } else if (yaw > 0.5f) {
        Motor_Start(MOTOR_LEFT); Motor_Start(MOTOR_RIGHT);
        Motor_SetDirection(MOTOR_LEFT,  MOTOR_FORWARD);
        Motor_SetDirection(MOTOR_RIGHT, MOTOR_BACKWARD);
        Motor_SetPulse(MOTOR_LEFT,  MOTOR_PWM_MAX * CAL_SPEED / 100);
        Motor_SetPulse(MOTOR_RIGHT, MOTOR_PWM_MAX * CAL_SPEED / 100);
        return false;
    }
    Motor_Brake(MOTOR_LEFT); Motor_Brake(MOTOR_RIGHT);
    Motor_SetPulse(MOTOR_LEFT, 0); Motor_SetPulse(MOTOR_RIGHT, 0);
    return true;
    #undef CAL_SPEED
}

/* NFC 寻卡后退模式: 刹车200ms → 后退500ms → 读卡 */
static bool     gNfcHunting   = false;
static uint32_t gHuntStartMs  = 0;
static uint32_t gHuntPhaseMs  = 0;
static int      gHuntPhase    = 0;  /* -1=刹车 0=后退 1=完成 */
#define HUNT_TIMEOUT  6000
#define HUNT_BRAKE_MS 200
#define HUNT_BACK_MS  300
#define HUNT_SPEED    20
extern int gLastCardNum;
extern int gLastCardNum_2;

void Motor_NfcStartHunt(void) {
    gNfcHunting  = true;
    gHuntStartMs = HAL_GetTick();
    gHuntPhaseMs = HAL_GetTick();
    gHuntPhase   = -1;
    gLastCardNum   = 0;
    gLastCardNum_2 = 0;
    gNfcStopped  = false;
    printf("[Motor] NFC hunt start\n");
}
void Motor_NfcStopHunt(void) {
    gNfcHunting = false;
    gNfcStopped = true;
    Motor_Brake(MOTOR_LEFT);
    Motor_Brake(MOTOR_RIGHT);
    Motor_SetPulse(MOTOR_LEFT,  0);
    Motor_SetPulse(MOTOR_RIGHT, 0);
    gPidDbg.lSpd = gPidDbg.rSpd = 0;
    printf("[Motor] NFC hunt stop\n");
}
bool Motor_NfcIsHunting(void)  { return gNfcHunting; }
bool Motor_NfcHuntTimeout(void){ return gNfcHunting && (HAL_GetTick()-gHuntStartMs > HUNT_TIMEOUT); }
uint32_t Motor_NfcHuntElapsed(void) { return HAL_GetTick() - gHuntStartMs; }

/* 后退: 刹车HUNT_BRAKE_MS → 后退HUNT_BACK_MS → 停住 */
static void Motor_HuntUpdate(void)
{
    uint32_t now = HAL_GetTick();
    uint32_t elap = now - gHuntPhaseMs;
    uint16_t p = MOTOR_PWM_MAX * HUNT_SPEED / 100;

    if (gHuntPhase == -1) {
        /* 刹车阶段 */
        Motor_SetPulse(MOTOR_LEFT,  0);
        Motor_SetPulse(MOTOR_RIGHT, 0);
        if (elap > HUNT_BRAKE_MS) {
            gHuntPhaseMs = now;
            gHuntPhase = 0;
        }
        return;
    }

    if (gHuntPhase == 0) {
        /* 后退阶段 */
        Motor_Start(MOTOR_LEFT);
        Motor_Start(MOTOR_RIGHT);
        Motor_SetDirection(MOTOR_LEFT,  MOTOR_BACKWARD);
        Motor_SetDirection(MOTOR_RIGHT, MOTOR_BACKWARD);
        Motor_SetPulse(MOTOR_LEFT,  p);
        Motor_SetPulse(MOTOR_RIGHT, p);
        if (elap > HUNT_BACK_MS) {
            Motor_Brake(MOTOR_LEFT);
            Motor_Brake(MOTOR_RIGHT);
            Motor_SetPulse(MOTOR_LEFT,  0);
            Motor_SetPulse(MOTOR_RIGHT, 0);
            gHuntPhase = 1;  /* 完成, 原地停车等读卡 */
        }
        return;
    }

    /* gHuntPhase==1: 已完成后退, 停车等待 */
}

void Motor_NfcTriggerStop(void)
{
    if (!gNfcStopped) {
        Motor_Brake(MOTOR_LEFT);
        Motor_Brake(MOTOR_RIGHT);
        Motor_SetPulse(MOTOR_LEFT,  0);
        Motor_SetPulse(MOTOR_RIGHT, 0);
        gNfcStopped = true;
        gPidDbg.lSpd = gPidDbg.rSpd = 0;
        printf("[Motor] NFC immediate stop\n");
    }
}
bool Motor_NfcStopped(void)  { return gNfcStopped; }
void Motor_NfcClear(void)    {
    gNfcStopped = false;
    gWallReset = true;  /* 让PID重置积分 */
}

/* 获取电机对应的定时器通道 */
static uint32_t Motor_GetChannel(Motor_TypeDef motor)
{
    return (motor == MOTOR_LEFT) ? TIM_CHANNEL_1 : TIM_CHANNEL_2;
}

/*==================== 获取方向控制引脚 ====================*/
static void Motor_GetDirPins(Motor_TypeDef motor,
                             GPIO_TypeDef **inaPort, uint16_t *inaPin,
                             GPIO_TypeDef **inbPort, uint16_t *inbPin)
{
    if (motor == MOTOR_LEFT) {
        *inaPort = MOTOR_LEFT_INA_PORT;  *inaPin = MOTOR_LEFT_INA_PIN;
        *inbPort = MOTOR_LEFT_INB_PORT;  *inbPin = MOTOR_LEFT_INB_PIN;
    } else {
        *inaPort = MOTOR_RIGHT_INA_PORT; *inaPin = MOTOR_RIGHT_INA_PIN;
        *inbPort = MOTOR_RIGHT_INB_PORT; *inbPin = MOTOR_RIGHT_INB_PIN;
    }
}

/*==================== 启动 PWM 输出 ====================*/
void Motor_Start(Motor_TypeDef motor)
{
    HAL_TIM_PWM_Start(&htim4, Motor_GetChannel(motor));
}

/*==================== 停止（刹车）====================*/
/*
 * INA=1, INB=1 → H 桥短路制动；PWM=0 后停止通道
 */
void Motor_Stop(Motor_TypeDef motor)
{
    GPIO_TypeDef *inaPort, *inbPort;
    uint16_t inaPin, inbPin;
    uint32_t ch = Motor_GetChannel(motor);

    /* INA=1, INB=1 (参考 WSDC2416S 刹车模式) */
    Motor_GetDirPins(motor, &inaPort, &inaPin, &inbPort, &inbPin);
    HAL_GPIO_WritePin(inaPort, inaPin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(inbPort, inbPin, GPIO_PIN_SET);

    __HAL_TIM_SET_COMPARE(&htim4, ch, 0);
    HAL_TIM_PWM_Stop(&htim4, ch);
    motorPulse[motor] = 0;
}

/*==================== 设置方向 ====================*/
/*
 * H 桥控制逻辑 (参考 WSDC2416S):
 *   MOTOR_FORWARD  : INA=0, INB=1
 *   MOTOR_BACKWARD : INA=1, INB=0
 */
void Motor_SetDirection(Motor_TypeDef motor, Motor_DirTypeDef dir)
{
    GPIO_TypeDef *inaPort, *inbPort;
    uint16_t inaPin, inbPin;

    Motor_GetDirPins(motor, &inaPort, &inaPin, &inbPort, &inbPin);

    if (dir == MOTOR_FORWARD) {
        HAL_GPIO_WritePin(inaPort, inaPin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(inbPort, inbPin, GPIO_PIN_SET);
    } else {
        HAL_GPIO_WritePin(inaPort, inaPin, GPIO_PIN_SET);
        HAL_GPIO_WritePin(inbPort, inbPin, GPIO_PIN_RESET);
    }
}

/*==================== 正转 / 反转 ====================*/
void Motor_Forward(void)
{
    Motor_SetDirection(MOTOR_LEFT, MOTOR_FORWARD);
    Motor_SetDirection(MOTOR_RIGHT,MOTOR_FORWARD);
}

void Motor_Backward(void)
{
    Motor_SetDirection(MOTOR_LEFT, MOTOR_BACKWARD);
    Motor_SetDirection(MOTOR_RIGHT,MOTOR_BACKWARD);
}

/*==================== 设置速度（百分比 0~100）====================*/
/*
 * 将 0~100 的百分比映射到 PWM 占空比
 * 0% = 停止，50% = 一半速度，100% = 全速
 */
void Motor_SetSpeed(Motor_TypeDef motor, uint8_t percent)
{
    uint16_t pulse;

    /* 限幅保护 */
    if (percent > 100) percent = 100;

    /* 百分比 → 脉冲值：percent% × MOTOR_PWM_MAX */
    pulse = (uint16_t)((uint32_t)percent * MOTOR_PWM_MAX / 100);

    Motor_SetPulse(motor, pulse);
}

/*==================== 设置原始占空比（0~MOTOR_PWM_MAX）====================*/
/*
 * 直接写入 TIM2 CCR 寄存器，精度最高
 * 范围: 0（0%占空比）~ MOTOR_PWM_MAX（100%占空比）
 */
void Motor_SetPulse(Motor_TypeDef motor, uint16_t pulse)
{
    uint32_t ch = Motor_GetChannel(motor);

    /* 限幅保护 */
    if (pulse > MOTOR_PWM_MAX) pulse = MOTOR_PWM_MAX;

    motorPulse[motor] = pulse;
    __HAL_TIM_SET_COMPARE(&htim4, ch, pulse);
}

/*==================== 制动（急停）====================*/
/*
 * INA=1, INB=1 + 100% 占空比 → H 桥下管全部导通 → 短路制动
 * 注意：此操作会产生较大电流冲击
 */
void Motor_Brake(Motor_TypeDef motor)
{
    GPIO_TypeDef *inaPort, *inbPort;
    uint16_t inaPin, inbPin;
    uint32_t ch = Motor_GetChannel(motor);

    Motor_GetDirPins(motor, &inaPort, &inaPin, &inbPort, &inbPin);
    HAL_GPIO_WritePin(inaPort, inaPin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(inbPort, inbPin, GPIO_PIN_SET);

    __HAL_TIM_SET_COMPARE(&htim4, ch, MOTOR_PWM_MAX);
    motorPulse[motor] = MOTOR_PWM_MAX;
}

/*==================== 平滑调速（带加减速）====================*/
/*
 * 从当前占空比逐步过渡到目标占空比，避免速度突变导致
 * 减速电机齿轮冲击或电流过冲。
 *
 * targetPulse  : 目标占空比 (0 ~ MOTOR_PWM_MAX)
 * stepDelayMs  : 每步延时（毫秒），值越大加速越慢
 *                典型值: 5~20ms
 *
 * 用法示例：
 *   Motor_RampTo(MOTOR_LEFT, 800, 10);  // 加速到 80%，每步 10ms
 *   Motor_RampTo(MOTOR_LEFT, 0, 20);    // 减速到停止，每步 20ms
 */
void Motor_RampTo(Motor_TypeDef motor, uint16_t targetPulse, uint16_t stepDelayMs)
{
    int16_t currentPulse = (int16_t)motorPulse[motor];
    int16_t target = (int16_t)targetPulse;

    if (target > MOTOR_PWM_MAX) target = MOTOR_PWM_MAX;

    while (currentPulse != target)
    {
        /* 每次逼近一步 */
        if (currentPulse < target)
            currentPulse++;
        else
            currentPulse--;

        Motor_SetPulse(motor, (uint16_t)currentPulse);
        HAL_Delay(stepDelayMs);  /* 阻塞延时，仅适用于简单场景 */
    }
}

/*==================== 获取当前占空比 ====================*/
uint16_t Motor_GetPulse(Motor_TypeDef motor)
{
    return motorPulse[motor];
}

int Motor_GetDirection(Motor_TypeDef motor)
{
    GPIO_TypeDef *inaPort, *inbPort;
    uint16_t inaPin, inbPin;
    Motor_GetDirPins(motor, &inaPort, &inaPin, &inbPort, &inbPin);
    /* INA=0,INB=1→正转; INA=1,INB=0→反转 */
    return (HAL_GPIO_ReadPin(inaPort, inaPin) == GPIO_PIN_RESET) ? 1 : 0;
}

/*==================== PS2 遥控差速混合 ====================*/
/*
 * PS2 遥控: LY=油门(上下), RX=转向(左右)
 *   LY上推=前进, LY下拉=后退
 *   RX左推=左转, RX右推=右转
 */
void Motor_PS2Control(int ly_raw, int rx_raw)
{
#define DEAD   10
#define MAX_P  ((int)MOTOR_PWM_MAX * 70 / 100)

    int spd, turn, left, right;

    /* 油门: LY 128中位, 0~127~255 → -100~100% */
    spd = 128 - ly_raw;
    if (spd > -DEAD && spd < DEAD) spd = 0;
    spd = spd * MAX_P / 127;

    /* 转向: RX 128中位, 左推>128, 右推<128 */
    turn = 128 - rx_raw;
    if (turn > -DEAD && turn < DEAD) turn = 0;
    turn = turn * MAX_P / 128;

    /* 转向随油门缩放, 零油门允许原地自转 */
    if (spd != 0)
        turn = turn * abs(spd) / MAX_P;

    /* 差速: 零油门原地自转, 组合运动走常规 */
    if (spd == 0) {
        left  =  turn;
        right = -turn;
    } else {
        left  = spd + turn;
        right = spd - turn;
    }

    /* 限幅 */
    if (left > MAX_P) left = MAX_P;
    if (left < -MAX_P) left = -MAX_P;
    if (right > MAX_P) right = MAX_P;
    if (right < -MAX_P) right = -MAX_P;

    /* 死区停车 */
    if (left == 0 && right == 0) {
        Motor_Stop(MOTOR_LEFT);
        Motor_Stop(MOTOR_RIGHT);
        return;
    }

    Motor_Start(MOTOR_LEFT);
    Motor_Start(MOTOR_RIGHT);
    Motor_SetDirection(MOTOR_LEFT,  left  >= 0 ? MOTOR_FORWARD : MOTOR_BACKWARD);
    Motor_SetDirection(MOTOR_RIGHT, right >= 0 ? MOTOR_FORWARD : MOTOR_BACKWARD);
    Motor_SetPulse(MOTOR_LEFT,  (uint16_t)abs(left));
    Motor_SetPulse(MOTOR_RIGHT, (uint16_t)abs(right));

#undef DEAD
#undef MAX_P
}

/*==================== PS2 按钮: 启动/停止电机 ====================*/
static int motor_on = 0;

void Motor_PS2Switch(int ps2_key)
{
    if (ps2_key == PSB_R2 && !motor_on) {
        Motor_Start(MOTOR_LEFT);
        Motor_Start(MOTOR_RIGHT);
        Motor_Forward();
        motor_on = 1;
        HAL_GPIO_WritePin(LD1_GPIO_Port, LD1_Pin, GPIO_PIN_RESET);
        printf("Motor ON\r\n");
    }
    else if (ps2_key == PSB_L2 && motor_on) {
        Motor_Stop(MOTOR_LEFT);
        Motor_Stop(MOTOR_RIGHT);
        motor_on = 0;
        HAL_GPIO_WritePin(LD1_GPIO_Port, LD1_Pin, GPIO_PIN_SET);
        printf("Motor OFF\r\n");
    }
}

int32_t Motor_GetUltraL(void) { return gPidDbg.ultraL; }
int32_t Motor_GetUltraR(void) { return gPidDbg.ultraR; }

int Motor_IsRunning(void)
{
    return motor_on;
}

/* VOFA+ FireWater 格式打印 PID 调试数据 + Yaw + 模式 */
void Motor_PrintVOFA(void)
{
    int yaw = (int)JY61_GetYaw();
    int dec = (int)(JY61_GetYaw() * 10) % 10;
    if (dec < 0) dec = -dec;
    printf("Yaw:%d.%d Err:%d Adj:%d L:%d%% R:%d%% US:%d,%dcm\r\n",
           yaw, dec,
           (int)(gPidDbg.err * 10),
           (int)(gPidDbg.adj * 10),
           gPidDbg.lSpd, gPidDbg.rSpd,
           (int)gPidDbg.ultraL, (int)gPidDbg.ultraR);
}

/* ─── PID 系数 ─── */
/* 内环陀螺: 强锁航向  外环超声: 慢调目标 */
#define KP_GYRO       3.0f
#define KI_GYRO       0.05f
#define KD_GYRO       1.5f
#define K_GYRO       1.5f    /* 陀螺仪权重 */
#define K_ULTRA      1.0f    /* 超声波权重 */

/* 行驶控制: 自动=PID走直线, 手动=PS2摇杆 */
void Motor_DriveUpdate(bool motorOn, bool isAuto)
{
    if (gNfcHunting) { Motor_HuntUpdate(); return; }

    if (motorOn && isAuto) {
        /* 超声波: 两边都测, 100ms错开避免串扰 */
        {
            static uint32_t lastHC = 0;
            static int      hcStep = 0;
            if (HAL_GetTick() - lastHC > 100) {
                lastHC = HAL_GetTick();
                if (hcStep == 0) {
                    HCSR04_Update(HCSR04_1);
                    gPidDbg.ultraR = (int32_t)(HCSR04_GetMM(HCSR04_1) / 100);
                } else {
                    HCSR04_Update(HCSR04_2);
                    gPidDbg.ultraL = (int32_t)(HCSR04_GetMM(HCSR04_2) / 100);
                }
                hcStep = !hcStep;
            }
        }
        /* ── NFC 触发: 立即停车 ── */
        if (gNfcStopped && !gCalibrating) {
            gPidDbg.lSpd = gPidDbg.rSpd = 0;
            gPidDbg.err = gPidDbg.adj = 0;
            return;
        }

        /* ── 贴墙: 连续PID, 无可见转向动作, 缓慢靠墙 ── */
        {
            static float   lockYaw = 0;
            static bool    yawLocked = false;
            int32_t wallDist;
            float curYaw = JY61_GetYaw();

            if (gWallReset) {
                yawLocked = false;
                gWallReady = false;
                gWallReset = false;
            }

            if (gWallSide == 0)
                wallDist = (int32_t)gPidDbg.ultraL;
            else
                wallDist = (int32_t)gPidDbg.ultraR;
            { static int32_t last = 11; if (wallDist > 0) last = wallDist; wallDist = last; }

            /* 锁定初始航向 */
            if (!yawLocked) { lockYaw = curYaw; yawLocked = true; }

            /* 超声误差 → 缓慢靠墙修正 */
            int adjUltra = 0;
            if (wallDist > 0 && wallDist <= WALL_MAX_CM) {
                float err = (float)wallDist - WALL_TARGET_CM;
                adjUltra = (int)(err * WALL_KP);
                if (adjUltra >  WALL_MAX_ADJ) adjUltra =  WALL_MAX_ADJ;
                if (adjUltra < -WALL_MAX_ADJ) adjUltra = -WALL_MAX_ADJ;
                if (gWallSide == 0) adjUltra = -adjUltra;

                float absErr = err > 0 ? err : -err;
                gWallReady = (absErr <= WALL_RANGE_CM);
            }

            /* 陀螺锁航向 */
            float yawErr = curYaw - lockYaw;
            if (yawErr >  180) yawErr -= 360;
            if (yawErr < -180) yawErr += 360;
            int adjGyro = (int)(GYRO_KP * yawErr);
            if (adjGyro > 30) adjGyro = 30; if (adjGyro < -30) adjGyro = -30;

            int adj = adjUltra + adjGyro;
            if (adj > 30) adj = 30; if (adj < -30) adj = -30;

            int lSpd = BASE_SPD + adj;
            int rSpd = BASE_SPD - adj;
            if (lSpd < 10) lSpd = 10; if (rSpd < 10) rSpd = 10;
            if (lSpd > 80) lSpd = 80; if (rSpd > 80) rSpd = 80;

            Motor_Start(MOTOR_LEFT);
            Motor_Start(MOTOR_RIGHT);
            Motor_SetDirection(MOTOR_LEFT,  MOTOR_FORWARD);
            Motor_SetDirection(MOTOR_RIGHT, MOTOR_FORWARD);
            Motor_SetPulse(MOTOR_LEFT,  MOTOR_PWM_MAX * rSpd / 100);
            Motor_SetPulse(MOTOR_RIGHT, MOTOR_PWM_MAX * lSpd / 100);

            gPidDbg.err  = wallDist; gPidDbg.adj = adj;
            gPidDbg.lSpd = rSpd; gPidDbg.rSpd = lSpd;
        }} else if (motorOn && !isAuto) {
        Motor_PS2Control(PS2_LY, PS2_RX);
    }
}
