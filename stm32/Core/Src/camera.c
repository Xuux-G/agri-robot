/**
 * @file    camera.c
 * @brief   MaixCam2 UART7 数据包接收与解析（原 ESP32 引脚）
 *
 * 数据包格式:
 * AA AA x_dir y_dir x_dist_h x_dist_l y_dist_h y_dist_l flag FF FF
 */

#include "camera.h"
#include "usart.h"
#include <stdio.h>

typedef enum {
    CAMERA_WAIT_AA1 = 0,
    CAMERA_WAIT_AA2,
    CAMERA_RECV_PAYLOAD
} CameraPacketState;

static volatile CameraPacketState gRxState = CAMERA_WAIT_AA1;
static volatile uint8_t gRxBuf[CAM_PAYLOAD_SIZE];
static volatile uint8_t gRxIndex = 0;
static volatile CameraCommand gLatestCommand;
static volatile bool gCommandPending = false;

void Camera_Init(void)
{
    gRxState = CAMERA_WAIT_AA1;
    gRxIndex = 0;
    gCommandPending = false;
    __HAL_UART_ENABLE_IT(&CAM_UART, UART_IT_RXNE);
    printf("  Camera UART7 init OK (PE7 RX, PE8 TX)\n");
}

void Camera_IRQHandler(void)
{
    while (__HAL_UART_GET_FLAG(&CAM_UART, UART_FLAG_RXNE)) {
        uint8_t byte = (uint8_t)CAM_UART.Instance->RDR;

        switch (gRxState) {
        case CAMERA_WAIT_AA1:
            if (byte == 0xAA) gRxState = CAMERA_WAIT_AA2;
            break;

        case CAMERA_WAIT_AA2:
            if (byte == 0xAA) {
                gRxIndex = 0;
                gRxState = CAMERA_RECV_PAYLOAD;
            } else {
                gRxState = CAMERA_WAIT_AA1;
            }
            break;

        case CAMERA_RECV_PAYLOAD:
            gRxBuf[gRxIndex++] = byte;
            if (gRxIndex >= CAM_PAYLOAD_SIZE) {
                gRxState = CAMERA_WAIT_AA1;
                gRxIndex = 0;

                if (gRxBuf[7] == 0xFF && gRxBuf[8] == 0xFF) {
                    gLatestCommand.x_dir  = gRxBuf[0];
                    gLatestCommand.y_dir  = gRxBuf[1];
                    gLatestCommand.x_dist = ((uint16_t)gRxBuf[2] << 8) | gRxBuf[3];
                    gLatestCommand.y_dist = ((uint16_t)gRxBuf[4] << 8) | gRxBuf[5];
                    gLatestCommand.flag   = gRxBuf[6];
                    gCommandPending = true;
                    printf("[CAM RAW] %02X %02X %02X%02X %02X%02X %02X %02X%02X\n",
                           gRxBuf[0],gRxBuf[1],gRxBuf[2],gRxBuf[3],
                           gRxBuf[4],gRxBuf[5],gRxBuf[6],gRxBuf[7],gRxBuf[8]);
                }
            }
            break;
        }
    }
}

bool Camera_GetCommand(CameraCommand *command)
{
    if (command == NULL) return false;

    __disable_irq();
    if (!gCommandPending) {
        __enable_irq();
        return false;
    }

    command->x_dir  = gLatestCommand.x_dir;
    command->y_dir  = gLatestCommand.y_dir;
    command->x_dist = gLatestCommand.x_dist;
    command->y_dist = gLatestCommand.y_dist;
    command->flag   = gLatestCommand.flag;
    gCommandPending = false;
    __enable_irq();
    return true;
}

void Camera_ClearCommand(void)
{
    __disable_irq();
    gCommandPending = false;
    __enable_irq();
}

bool Camera_TargetDetected(void)
{
    CameraCommand command;
    return Camera_GetCommand(&command) && command.flag == 1;
}
