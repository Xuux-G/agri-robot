/**
 * @file    auto_drive.c
 * @brief   多步轨迹行驶 — 每步可独立设定动作/速度/时长/触发条件
 *
 * 用法:
 *   1. 定义轨迹数组 DriveStep steps[]
 *   2. AutoDrive_Load(steps, N)
 *   3. AutoDrive_Start()
 *   4. 主循环调用 AutoDrive_Update(超声波距离mm)
 */

#include "auto_drive.h"
#include "Motor.h"
#include <stdio.h>

static DriveStep *gSteps       = NULL;
static uint8_t    gStepCount   = 0;
static uint8_t    gCurrent     = 0;   /* 当前步骤 (1-based) */
static uint32_t   gStepStartMs = 0;
static bool       gRunning     = false;
static uint8_t    gCurSpeed    = 0;   /* 当前速度 % */

/* 执行指定动作 */
static void DoAction(DriveAction act, uint8_t speedPercent)
{
    uint16_t pulse = MOTOR_PWM_MAX * speedPercent / 100;

    switch (act) {
    case ACT_STOP:
        Motor_SetPulse(MOTOR_LEFT,  0);
        Motor_SetPulse(MOTOR_RIGHT, 0);
        break;
    case ACT_FORWARD:
        Motor_SetDirection(MOTOR_LEFT,  MOTOR_FORWARD);
        Motor_SetDirection(MOTOR_RIGHT, MOTOR_FORWARD);
        Motor_SetPulse(MOTOR_LEFT,  pulse);
        Motor_SetPulse(MOTOR_RIGHT, pulse);
        break;
    case ACT_BACKWARD:
        Motor_SetDirection(MOTOR_LEFT,  MOTOR_BACKWARD);
        Motor_SetDirection(MOTOR_RIGHT, MOTOR_BACKWARD);
        Motor_SetPulse(MOTOR_LEFT,  pulse);
        Motor_SetPulse(MOTOR_RIGHT, pulse);
        break;
    case ACT_LEFT:
        Motor_SetDirection(MOTOR_LEFT,  MOTOR_BACKWARD);
        Motor_SetDirection(MOTOR_RIGHT, MOTOR_FORWARD);
        Motor_SetPulse(MOTOR_LEFT,  pulse);
        Motor_SetPulse(MOTOR_RIGHT, pulse);
        break;
    case ACT_RIGHT:
        Motor_SetDirection(MOTOR_LEFT,  MOTOR_FORWARD);
        Motor_SetDirection(MOTOR_RIGHT, MOTOR_BACKWARD);
        Motor_SetPulse(MOTOR_LEFT,  pulse);
        Motor_SetPulse(MOTOR_RIGHT, pulse);
        break;
    }
}

/* 切换到下一步 */
static void NextStep(void)
{
    gCurrent++;
    if (gCurrent > gStepCount) {
        /* 所有步骤执行完毕 */
        DoAction(ACT_STOP, 0);
        gRunning = false;
        printf("[Auto] Done!\n");
        return;
    }

    DriveStep *s = &gSteps[gCurrent - 1];
    gStepStartMs = HAL_GetTick();
    DoAction(s->action, s->speed);
    printf("[Auto] Step %d/%d: act=%d spd=%d%% dur=%dms dist=%lumm\n",
           gCurrent, gStepCount, s->action, s->speed,
           s->duration, (unsigned long)s->stopDistMM);
}

void AutoDrive_Load(DriveStep *steps, uint8_t count)
{
    gSteps     = steps;
    gStepCount = count;
    gCurrent   = 0;
    gRunning   = false;
    printf("[Auto] Loaded %d steps\n", count);
}

void AutoDrive_Start(void)
{
    if (gStepCount == 0) return;
    Motor_Start(MOTOR_LEFT);
    Motor_Start(MOTOR_RIGHT);
    gCurrent = 0;
    gRunning = true;
    NextStep();
}

void AutoDrive_Stop(void)
{
    DoAction(ACT_STOP, 0);
    gRunning = false;
    gCurrent = 0;
    printf("[Auto] Stopped\n");
}

/* 根据剩余时间和剩余距离计算线性减速后的速度 (整数运算) */
static uint8_t CalcSpeed(DriveStep *s, uint32_t elapsed, uint32_t distMM)
{
    uint32_t ratio = 1000;  /* 速度比 (×1000), 默认 100% */

    /* 距离减速优先: 进入减速区就以距离为准 */
    if (s->stopDistMM > 0 && distMM > 0 && distMM < s->stopDistMM * 3) {
        int32_t margin = (int32_t)distMM - (int32_t)s->stopDistMM;
        if (margin <= 0) {
            ratio = 300;  /* 已到触发点, 最低 30% */
        } else {
            ratio = (uint32_t)margin * 1000 / (s->stopDistMM * 2);
            if (ratio < 300) ratio = 300;
        }
    }
    /* 仅当距离减速未激活时, 才用时间减速 */
    else if (s->duration > 0) {
        uint32_t decelStart = (uint32_t)s->duration * 7 / 10;
        if (elapsed > decelStart) {
            uint32_t remain = s->duration - elapsed;
            ratio = remain * 1000 / (s->duration - decelStart);
            if (ratio < 300) ratio = 300;
        }
    }

    if (ratio > 1000) ratio = 1000;
    return (uint8_t)((uint32_t)s->speed * ratio / 1000);
}

void AutoDrive_Update(uint32_t distMM)
{
    if (!gRunning || gCurrent == 0 || gCurrent > gStepCount) return;

    DriveStep *s = &gSteps[gCurrent - 1];
    uint32_t   elapsed = HAL_GetTick() - gStepStartMs;

    /* 条件 1: 超时切换 */
    if (s->duration > 0 && elapsed >= s->duration) {
        NextStep();
        return;
    }

    /* 条件 2: 超声波距离触发 (前方有障碍) */
    if (s->stopDistMM > 0 && distMM > 0 && distMM < s->stopDistMM) {
        printf("[Auto] Step %d triggered by dist %lu.%02lucm\n",
               gCurrent, distMM / 100, distMM % 100);
        NextStep();
        return;
    }

    /* 实时调速 (线性减速) */
    gCurSpeed = CalcSpeed(s, elapsed, distMM);
    DoAction(s->action, gCurSpeed);
}

uint8_t AutoDrive_GetSpeed(void)
{
    return gCurSpeed;
}

uint8_t AutoDrive_GetStep(void)
{
    return gCurrent;
}
