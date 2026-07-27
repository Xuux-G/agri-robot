#include "pstwo.h"
#include "Motor.h"
#include <stdio.h>

/* 微秒级延时 (DWT 硬件周期计数器, 独立于 SysTick, 纳秒级精度) */
static void delay_us(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * (SystemCoreClock / 1000000U);
    while ((DWT->CYCCNT - start) < ticks);
}

/* PS2 时钟半周期 (高/低电平各 5us → 100kHz 时钟) */
#define DELAY_TIME  delay_us(5)

uint16_t PS2_Handkey;
uint8_t  PS2_Comd[2] = {0x01, 0x42};
uint8_t  PS2_Data[9] = {0};
uint16_t PS2_MASK[] = {
    PSB_SELECT,   PSB_L3,      PSB_R3,     PSB_START,
    PSB_PAD_UP,   PSB_PAD_RIGHT, PSB_PAD_DOWN, PSB_PAD_LEFT,
    PSB_L2,       PSB_R2,      PSB_L1,     PSB_R1,
    PSB_GREEN,    PSB_RED,     PSB_BLUE,   PSB_PINK
};

int PS2_LX, PS2_LY, PS2_RX, PS2_RY, PS2_KEY;
volatile int PS2_LastRelease = 0;  /* ISR 写入, 主循环读取消费 */

void PS2_Init(void)
{
    /* 使能 DWT 周期计数器 (用于 delay_us 精确微秒延时) */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    /* GPIO 初始化由 CubeMX MX_GPIO_Init() 完成, 这里仅设置初始电平 */
    PS2_DO_H();
    PS2_CS_H();
    PS2_CLK_H();
}

/* 向手柄发送命令, 同时读取 1 字节数据存入 Data[1] */
void PS2_Cmd(uint8_t CMD)
{
    volatile uint16_t ref = 0x01;

    PS2_Data[1] = 0;
    for (ref = 0x01; ref < 0x0100; ref <<= 1)
    {
        if (ref & CMD)
            PS2_DO_H();
        else
            PS2_DO_L();

        PS2_CLK_H();
        DELAY_TIME;
        PS2_CLK_L();
        DELAY_TIME;
        PS2_CLK_H();
        if (PS2_DI_READ())
            PS2_Data[1] = (uint8_t)(ref | PS2_Data[1]);
    }
    delay_us(16);
}

/* 判断手柄模式: 返回 0=红灯模拟模式, 非 0=其他 */
uint8_t PS2_RedLight(void)
{
    PS2_CS_L();
    PS2_Cmd(PS2_Comd[0]);
    PS2_Cmd(PS2_Comd[1]);
    PS2_CS_H();
    if (PS2_Data[1] == 0x73) return 0;
    else return 1;
}

/* 读取完整 9 字节手柄数据 */
void PS2_ReadData(void)
{
    volatile uint8_t  byte = 0;
    volatile uint16_t ref  = 0x01;

    PS2_CS_L();
    PS2_Cmd(PS2_Comd[0]);
    PS2_Cmd(PS2_Comd[1]);
    for (byte = 2; byte < 9; byte++)
    {
        for (ref = 0x01; ref < 0x100; ref <<= 1)
        {
            PS2_CLK_H();
            DELAY_TIME;
            PS2_CLK_L();
            DELAY_TIME;
            PS2_CLK_H();
            if (PS2_DI_READ())
                PS2_Data[byte] = (uint8_t)(ref | PS2_Data[byte]);
        }
        delay_us(16);
    }
    PS2_CS_H();
}

/* 读取按键: 返回 1~16=对应按键, 0=无按键 */
uint8_t PS2_DataKey(void)
{
    uint8_t index;

    PS2_ClearData();
    PS2_ReadData();

    PS2_Handkey = (uint16_t)((PS2_Data[4] << 8) | PS2_Data[3]);
    for (index = 0; index < 16; index++)
    {
        if ((PS2_Handkey & (1 << (PS2_MASK[index] - 1))) == 0)
            return index + 1;
    }
    return 0;
}

/* 读取摇杆模拟值 (0~255) */
uint8_t PS2_AnologData(uint8_t button)
{
    return PS2_Data[button];
}

/* 清空数据缓冲区 */
void PS2_ClearData(void)
{
    uint8_t a;
    for (a = 0; a < 9; a++)
        PS2_Data[a] = 0x00;
}

/* 震动控制: motor1=右侧小电机(0x00 关, 其他开), motor2=左侧大电机(0x40~0xFF) */
void PS2_Vibration(uint8_t motor1, uint8_t motor2)
{
    PS2_CS_L();
    delay_us(16);
    PS2_Cmd(0x01);
    PS2_Cmd(0x42);
    PS2_Cmd(0x00);
    PS2_Cmd(motor1);
    PS2_Cmd(motor2);
    PS2_Cmd(0x00);
    PS2_Cmd(0x00);
    PS2_Cmd(0x00);
    PS2_Cmd(0x00);
    PS2_CS_H();
    delay_us(16);
}

/* short poll */
void PS2_ShortPoll(void)
{
    PS2_CS_L();
    delay_us(16);
    PS2_Cmd(0x01);
    PS2_Cmd(0x42);
    PS2_Cmd(0x00);
    PS2_Cmd(0x00);
    PS2_Cmd(0x00);
    PS2_CS_H();
    delay_us(16);
}

/* 进入配置模式 */
void PS2_EnterConfing(void)
{
    PS2_CS_L();
    delay_us(16);
    PS2_Cmd(0x01);
    PS2_Cmd(0x43);
    PS2_Cmd(0x00);
    PS2_Cmd(0x01);
    PS2_Cmd(0x00);
    PS2_Cmd(0x00);
    PS2_Cmd(0x00);
    PS2_Cmd(0x00);
    PS2_Cmd(0x00);
    PS2_CS_H();
    delay_us(16);
}

/* 开启模拟红灯模式 */
void PS2_TurnOnAnalogMode(void)
{
    PS2_CS_L();
    PS2_Cmd(0x01);
    PS2_Cmd(0x44);
    PS2_Cmd(0x00);
    PS2_Cmd(0x01); /* analog=0x01; digital=0x00 */
    PS2_Cmd(0x03); /* 0x03=锁定配置, 0xEE=不锁定 */
    PS2_Cmd(0x00);
    PS2_Cmd(0x00);
    PS2_Cmd(0x00);
    PS2_Cmd(0x00);
    PS2_CS_H();
    delay_us(16);
}

/* 震动模式设置 */
void PS2_VibrationMode(void)
{
    PS2_CS_L();
    delay_us(16);
    PS2_Cmd(0x01);
    PS2_Cmd(0x4D);
    PS2_Cmd(0x00);
    PS2_Cmd(0x00);
    PS2_Cmd(0x01);
    PS2_CS_H();
    delay_us(16);
}

/* 保存并退出配置 */
void PS2_ExitConfing(void)
{
    PS2_CS_L();
    delay_us(16);
    PS2_Cmd(0x01);
    PS2_Cmd(0x43);
    PS2_Cmd(0x00);
    PS2_Cmd(0x00);
    PS2_Cmd(0x5A);
    PS2_Cmd(0x5A);
    PS2_Cmd(0x5A);
    PS2_Cmd(0x5A);
    PS2_Cmd(0x5A);
    PS2_CS_H();
    delay_us(16);
}

/* 手柄初始化序列 */
void PS2_SetInit(void)
{
    PS2_ShortPoll();
    PS2_ShortPoll();
    PS2_ShortPoll();
    PS2_EnterConfing();
    PS2_TurnOnAnalogMode();
    /* PS2_VibrationMode(); */ /* 震动模式按需开启 */
    PS2_ExitConfing();
}

/* 一次读取并存储到全局变量 */
void PS2_Receive(void)
{
    PS2_LX  = PS2_AnologData(PSS_LX);
    PS2_LY  = PS2_AnologData(PSS_LY);
    PS2_RX  = PS2_AnologData(PSS_RX);
    PS2_RY  = PS2_AnologData(PSS_RY);
    PS2_KEY = PS2_DataKey();
}

/* 按键边沿检测 → 启动/停止电机 + LED */
void PS2_HandleButtons(void)
{
    static int last_key = 0;
    if (PS2_KEY && PS2_KEY != last_key)
        Motor_PS2Switch(PS2_KEY);
    last_key = PS2_KEY;
}

/* 打印摇杆和电机速度占比+方向 (仅电机启动时打印) */
void PS2_PrintDebug(void)
{
    if (!Motor_IsRunning()) return;
    int lp = Motor_GetPulse(MOTOR_LEFT)  / 10;
    int rp = Motor_GetPulse(MOTOR_RIGHT) / 10;
    char ld = Motor_GetDirection(MOTOR_LEFT)  ? 'F' : 'B';
    char rd = Motor_GetDirection(MOTOR_RIGHT) ? 'F' : 'B';
    printf("LY:%d RY:%d RX:%d  L:%c%3d%% R:%c%3d%%  KEY:%d\r\n",
           PS2_LY, PS2_RY, PS2_RX, ld, lp, rd, rp, PS2_KEY);
}

/*
 * PS2 模式切换 + 电机启停 (松开触发)
 *   L2     → 自动模式 + 停车
 *   R2     → 手动模式 + 停车
 *   START  → 启/停电机 (当前模式)
 */
void PS2_ModeHandler(bool *isAuto, bool *motorOn)
{
    /* ISR 已做好边沿检测, 读取消费即可 */
    int key = PS2_LastRelease;
    PS2_LastRelease = 0;  /* 消费掉 */

    if (key == PSB_L2) {
        *isAuto  = true;
        *motorOn = false;
        Motor_Brake(MOTOR_LEFT);   Motor_Brake(MOTOR_RIGHT);
        Motor_Stop(MOTOR_LEFT);    Motor_Stop(MOTOR_RIGHT);
        printf("[Mode] AUTO (stopped)\n");
    }
    else if (key == PSB_R2) {
        *isAuto  = false;
        *motorOn = false;
        Motor_Brake(MOTOR_LEFT);   Motor_Brake(MOTOR_RIGHT);
        Motor_Stop(MOTOR_LEFT);    Motor_Stop(MOTOR_RIGHT);
        printf("[Mode] MANUAL (stopped)\n");
    }
    else if (key == PSB_START) {
        *motorOn = !(*motorOn);
        if (*motorOn) {
            printf("[Motor] ON (%s)\n", *isAuto ? "AUTO" : "MANUAL");
        } else {
            Motor_Brake(MOTOR_LEFT);   Motor_Brake(MOTOR_RIGHT);
            Motor_Stop(MOTOR_LEFT);    Motor_Stop(MOTOR_RIGHT);
            HAL_GPIO_WritePin(LD1_GPIO_Port, LD1_Pin, GPIO_PIN_SET);
            printf("[Motor] OFF\n");
        }
    }

}
