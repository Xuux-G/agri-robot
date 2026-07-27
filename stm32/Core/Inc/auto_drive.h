#ifndef __AUTO_DRIVE_H__
#define __AUTO_DRIVE_H__

#include "main.h"
#include <stdbool.h>

/* 动作类型 */
typedef enum {
    ACT_STOP     = 0,   /* 停止 */
    ACT_FORWARD  = 1,   /* 直走 */
    ACT_BACKWARD = 2,   /* 后退 */
    ACT_LEFT     = 3,   /* 原地左转 */
    ACT_RIGHT    = 4,   /* 原地右转 */
} DriveAction;

/* 每一步的定义 */
typedef struct {
    DriveAction action;     /* 动作 */
    uint8_t     speed;      /* 速度 0~100% */
    uint16_t    duration;   /* 持续时间 (ms), 0=不自动切下一步 */
    uint32_t    stopDistMM; /* 超声波距离阈值 (mm), 小于此值强制切下一步, 0=忽略 */
} DriveStep;

/* 装载轨迹: steps=步骤数组, count=步骤数 */
void AutoDrive_Load(DriveStep *steps, uint8_t count);

/* 主循环中调用: 处理当前步骤计时和距离触发 */
void AutoDrive_Update(uint32_t distMM);

/* 启动 / 停止 */
void AutoDrive_Start(void);
void AutoDrive_Stop(void);

/* 获取当前执行到第几步 (0=未开始, 1~count=执行中, count+1=完成) */
uint8_t AutoDrive_GetStep(void);

/* 获取当前实时速度 (0~100%) */
uint8_t AutoDrive_GetSpeed(void);

#endif
