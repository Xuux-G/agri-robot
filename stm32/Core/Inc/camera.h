#ifndef __CAMERA_H__
#define __CAMERA_H__

#include "main.h"
#include "usart.h"
#include <stdbool.h>
#include <stdint.h>

/* MaixCam2 串口协议: AA AA + 7 字节数据 + FF FF，共 11 字节。 */
#define CAM_UART          huart8  /* UART8: PE0=RX, PE1=TX */
#define CAM_PACKET_SIZE   11U
#define CAM_PAYLOAD_SIZE   9U

typedef struct {
    uint8_t  x_dir;       /* X 轴方向: 底部旋转方向 */
    uint8_t  y_dir;       /* Y 轴方向: 伸缩方向 */
    uint16_t x_dist;      /* X 轴动作量，由 STM32 按比例换算为脉冲 */
    uint16_t y_dist;      /* Y 轴距离，当前按厘米使用 */
    uint8_t  flag;        /* 0=运动, 1=到位, 2=目标丢失, 3=过近, 4=当前树任务结束 */
} CameraCommand;

void Camera_Init(void);
void Camera_IRQHandler(void);

/* 读取一条最新命令。没有新包时返回 false，防止同一动作重复执行。 */
bool Camera_GetCommand(CameraCommand *command);

/* 清除启动作业前遗留的旧命令。 */
void Camera_ClearCommand(void);

/* 兼容原扫描代码: 读取到 flag=1 时返回 true。 */
bool Camera_TargetDetected(void);

#endif
