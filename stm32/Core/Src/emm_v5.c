/**
 * @file    emm_v5.c
 * @brief   Emm_V5.0 姝ヨ繘闂幆椹卞姩鍣ㄥ畬鏁村崗璁?
 */

#include "emm_v5.h"
#include "usart.h"
#include <string.h>
#include <stdio.h>

uint16_t MMCL_count = 0, MMCL_cmd[MMCL_LEN] = {0};

/* 鈹€鈹€ USART6 涓柇鎺ユ敹: 瀛樺偍姝ヨ繘搴旂瓟鏁版嵁 鈹€鈹€ */
static volatile int32_t gEmmLastCPOS = 0;     /* 鏈€杩戜竴娆?S_CPOS 杩斿洖鍊?*/
static volatile bool    gEmmCPOSFresh = false;
static uint8_t  gEmmRxBuf[16];
static uint8_t  gEmmRxIdx = 0;

/* ISR 中调用, 逐字节喂入 (不能有 printf, 不能阻塞) */
void Emm_V5_IRQHandler(uint8_t b)
{
    if (gEmmRxIdx < 16) {
        gEmmRxBuf[gEmmRxIdx++] = b;
    }

    /* 帧尾 0x6B → 解析 */
    if (b == 0x6B && gEmmRxIdx >= 4) {
        if (gEmmRxBuf[1] == 0x36 && gEmmRxIdx >= 7) {
            gEmmLastCPOS = (int32_t)((gEmmRxBuf[2]<<24) | (gEmmRxBuf[3]<<16)
                                   | (gEmmRxBuf[4]<<8)  |  gEmmRxBuf[5]);
            gEmmCPOSFresh = true;
        }
        gEmmRxIdx = 0;
    }
}

void Emm_V5_InitRx(void)
{
    /* 彻底关闭 USART6 中断, 只用阻塞发送 */
    __HAL_UART_DISABLE_IT(&huart6, UART_IT_RXNE | UART_IT_TXE | UART_IT_TC);
    CLEAR_BIT(huart6.Instance->CR1, USART_CR1_RXNEIE | USART_CR1_TXEIE | USART_CR1_TCIE);
    CLEAR_BIT(huart6.Instance->CR3, USART_CR3_EIE);
    HAL_NVIC_DisableIRQ(USART6_IRQn);
}

/* 璇诲綋鍓嶄綅缃? 绛夊緟搴旂瓟 (瓒呮椂 200ms) */
int32_t Emm_V5_GetCPOS(uint8_t addr)
{
    while (__HAL_UART_GET_FLAG(&huart6, UART_FLAG_RXNE))
        (void)huart6.Instance->RDR;

    Emm_V5_Read_Sys_Params(addr, S_CPOS);

    uint8_t  buf[16]; uint8_t idx = 0;
    uint32_t t0 = DWT->CYCCNT;
    uint32_t timeout = SystemCoreClock / 5;  /* 200ms */
    while (DWT->CYCCNT - t0 < timeout) {
        if (__HAL_UART_GET_FLAG(&huart6, UART_FLAG_RXNE)) {
            uint8_t b = (uint8_t)(huart6.Instance->RDR);
            if (idx < 16) buf[idx++] = b;
            if (b == 0x6B && idx >= 7 && buf[1] == 0x36) {
                printf("[EMM RX] ");
                for (int k=0;k<idx;k++) printf("%02X ",buf[k]);
                printf("\r\n");
                return (int32_t)((buf[2]<<24)|(buf[3]<<16)|(buf[4]<<8)|buf[5]);
            }
            if (b == 0x6B) idx = 0;
        }
    }
    printf("[EMM] GetCPOS timeout\r\n");
    return 0;
}

static void Emm_Send(const uint8_t *cmd, uint8_t len)
{
    __disable_irq();
    HAL_StatusTypeDef r = HAL_UART_Transmit(&EMM_UART, (uint8_t *)cmd, len, 100);
    __enable_irq();
    if (r != HAL_OK) {
        printf("[EMM] TX FAIL(%d)\r\n", r);
    }
}

/* ==================== 瑙﹀彂鍔ㄤ綔 ==================== */
void Emm_V5_Trig_Encoder_Cal(uint8_t addr) {
    static uint8_t c[16]={0};
    c[0]=addr; c[1]=0x06; c[2]=0x45; c[3]=0x6B; Emm_Send(c,4);
}
void Emm_V5_Reset_Motor(uint8_t addr) {
    static uint8_t c[16]={0};
    c[0]=addr; c[1]=0x08; c[2]=0x97; c[3]=0x6B; Emm_Send(c,4);
}
void Emm_V5_Reset_CurPos_To_Zero(uint8_t addr) {
    static uint8_t c[16]={0};
    c[0]=addr; c[1]=0x0A; c[2]=0x6D; c[3]=0x6B; Emm_Send(c,4);
}
void Emm_V5_Reset_Clog_Pro(uint8_t addr) {
    static uint8_t c[16]={0};
    c[0]=addr; c[1]=0x0E; c[2]=0x52; c[3]=0x6B; Emm_Send(c,4);
}
void Emm_V5_Restore_Motor(uint8_t addr) {
    static uint8_t c[16]={0};
    c[0]=addr; c[1]=0x0F; c[2]=0x5F; c[3]=0x6B; Emm_Send(c,4);
}

/* ==================== 杩愬姩鎺у埗 ==================== */
void Emm_V5_Multi_Motor_Cmd(uint8_t addr) {
    uint16_t i=0,j=0,len=0; static uint8_t c[MMCL_LEN]={0};
    if(MMCL_count>0){
        len=MMCL_count+5;
        c[0]=addr; c[1]=0xAA; c[2]=(uint8_t)(len>>8); c[3]=(uint8_t)(len);
        for(i=0,j=4;i<MMCL_count;i++,j++)c[j]=MMCL_cmd[i];
        c[j]=0x6B;++j; Emm_Send(c,j); MMCL_count=0;
    }else{MMCL_count=0;}
}
void Emm_V5_En_Control(uint8_t addr, bool state, bool snF) {
    static uint8_t c[16]={0};
    c[0]=addr; c[1]=0xF3; c[2]=0xAB; c[3]=(uint8_t)state; c[4]=snF; c[5]=0x6B; Emm_Send(c,6);
}
void Emm_V5_Vel_Control(uint8_t addr, uint8_t dir, uint16_t vel, uint8_t acc, bool snF) {
    static uint8_t c[16]={0};
    c[0]=addr; c[1]=0xF6; c[2]=dir;
    c[3]=(uint8_t)(vel>>8); c[4]=(uint8_t)(vel); c[5]=acc; c[6]=snF; c[7]=0x6B; Emm_Send(c,8);
}
void Emm_V5_Pos_Control(uint8_t addr, uint8_t dir, uint16_t vel, uint8_t acc, uint32_t clk, bool raF, bool snF) {
    static uint8_t c[16]={0};
    c[0]=addr; c[1]=0xFD; c[2]=dir;
    c[3]=(uint8_t)(vel>>8); c[4]=(uint8_t)(vel); c[5]=acc;
    c[6]=(uint8_t)(clk>>24);c[7]=(uint8_t)(clk>>16);c[8]=(uint8_t)(clk>>8);c[9]=(uint8_t)(clk);
    c[10]=raF; c[11]=snF; c[12]=0x6B; Emm_Send(c,13);
    // gEmmBusyUntil = HAL_GetTick() + Emm_EstimateTime(clk, vel);
}
void Emm_V5_Stop_Now(uint8_t addr, bool snF) {
    static uint8_t c[16]={0};
    c[0]=addr; c[1]=0xFE; c[2]=0x98; c[3]=snF; c[4]=0x6B; Emm_Send(c,5);
}
void Emm_V5_Synchronous_motion(uint8_t addr) {
    static uint8_t c[16]={0};
    c[0]=addr; c[1]=0xFF; c[2]=0x66; c[3]=0x6B; Emm_Send(c,4);
}

/* ==================== 鍘熺偣鍥為浂 ==================== */
void Emm_V5_Origin_Set_O(uint8_t addr, bool svF) {
    static uint8_t c[16]={0};
    c[0]=addr; c[1]=0x93; c[2]=0x88; c[3]=svF; c[4]=0x6B; Emm_Send(c,5);
}
void Emm_V5_Origin_Trigger_Return(uint8_t addr, uint8_t o_mode, bool snF) {
    static uint8_t c[16]={0};
    c[0]=addr; c[1]=0x9A; c[2]=o_mode; c[3]=snF; c[4]=0x6B; Emm_Send(c,5);
}
void Emm_V5_Origin_Interrupt(uint8_t addr) {
    static uint8_t c[16]={0};
    c[0]=addr; c[1]=0x9C; c[2]=0x48; c[3]=0x6B; Emm_Send(c,4);
}
void Emm_V5_Origin_Read_Params(uint8_t addr) {
    static uint8_t c[16]={0};
    c[0]=addr; c[1]=0x22; c[2]=0x6B; Emm_Send(c,3);
}
void Emm_V5_Origin_Modify_Params(uint8_t addr, bool svF, uint8_t o_mode, uint8_t o_dir,
    uint16_t o_vel, uint32_t o_tm, uint16_t sl_vel, uint16_t sl_ma, uint16_t sl_ms, bool potF) {
    static uint8_t c[32]={0};
    c[0]=addr; c[1]=0x4C; c[2]=0xAE; c[3]=svF; c[4]=o_mode; c[5]=o_dir;
    c[6]=(uint8_t)(o_vel>>8);c[7]=(uint8_t)(o_vel);
    c[8]=(uint8_t)(o_tm>>24);c[9]=(uint8_t)(o_tm>>16);c[10]=(uint8_t)(o_tm>>8);c[11]=(uint8_t)(o_tm);
    c[12]=(uint8_t)(sl_vel>>8);c[13]=(uint8_t)(sl_vel);
    c[14]=(uint8_t)(sl_ma>>8);c[15]=(uint8_t)(sl_ma);
    c[16]=(uint8_t)(sl_ms>>8);c[17]=(uint8_t)(sl_ms);
    c[18]=potF; c[19]=0x6B; Emm_Send(c,20);
}

/* ==================== 璇诲彇绯荤粺鍙傛暟 ==================== */
static void build_AutoReturn(uint8_t addr, SysParams_t s, uint16_t time_ms, uint8_t *c, uint8_t *len) {
    uint8_t i=0;
    c[i]=addr;++i; c[i]=0x11;++i; c[i]=0x18;++i;
    switch(s){
        case S_VBUS:c[i]=0x24;++i;break;case S_CBUS:c[i]=0x26;++i;break;
        case S_CPHA:c[i]=0x27;++i;break;case S_ENCO:c[i]=0x29;++i;break;
        case S_CLKC:c[i]=0x30;++i;break;case S_ENCL:c[i]=0x31;++i;break;
        case S_CLKI:c[i]=0x32;++i;break;case S_TPOS:c[i]=0x33;++i;break;
        case S_SPOS:c[i]=0x34;++i;break;case S_VEL:c[i]=0x35;++i;break;
        case S_CPOS:c[i]=0x36;++i;break;case S_PERR:c[i]=0x37;++i;break;
        case S_VBAT:c[i]=0x38;++i;break;case S_TEMP:c[i]=0x39;++i;break;
        case S_FLAG:c[i]=0x3A;++i;break;case S_OFLAG:c[i]=0x3B;++i;break;
        case S_OAF:c[i]=0x3C;++i;break;case S_PIN:c[i]=0x3D;++i;break;
        default:break;
    }
    c[i]=(uint8_t)(time_ms>>8);++i;c[i]=(uint8_t)(time_ms);++i;c[i]=0x6B;++i;
    *len=i;
}
void Emm_V5_Auto_Return_Sys_Params_Timed(uint8_t addr, SysParams_t s, uint16_t time_ms) {
    static uint8_t c[16]={0}; uint8_t len;
    build_AutoReturn(addr,s,time_ms,(uint8_t*)c,&len); Emm_Send(c,len);
}
void Emm_V5_Read_Sys_Params(uint8_t addr, SysParams_t s) {
    uint8_t i=0; static uint8_t c[16]={0};
    c[i]=addr;++i;
    switch(s){
        case S_VBUS:c[i]=0x24;++i;break;case S_CBUS:c[i]=0x26;++i;break;
        case S_CPHA:c[i]=0x27;++i;break;case S_ENCO:c[i]=0x29;++i;break;
        case S_CLKC:c[i]=0x30;++i;break;case S_ENCL:c[i]=0x31;++i;break;
        case S_CLKI:c[i]=0x32;++i;break;case S_TPOS:c[i]=0x33;++i;break;
        case S_SPOS:c[i]=0x34;++i;break;case S_VEL:c[i]=0x35;++i;break;
        case S_CPOS:c[i]=0x36;++i;break;case S_PERR:c[i]=0x37;++i;break;
        case S_VBAT:c[i]=0x38;++i;break;case S_TEMP:c[i]=0x39;++i;break;
        case S_FLAG:c[i]=0x3A;++i;break;case S_OFLAG:c[i]=0x3B;++i;break;
        case S_OAF:c[i]=0x3C;++i;break;case S_PIN:c[i]=0x3D;++i;break;
        default:break;
    }
    c[i]=0x6B;++i; Emm_Send(c,i);
}

/* ==================== 璇诲啓椹卞姩鍙傛暟 ==================== */
void Emm_V5_Modify_Motor_ID(uint8_t addr, bool svF, uint8_t id) {
    static uint8_t c[16]={0};
    c[0]=addr;c[1]=0xAE;c[2]=0x4B;c[3]=svF;c[4]=id;c[5]=0x6B;Emm_Send(c,6);
}
void Emm_V5_Modify_MicroStep(uint8_t addr, bool svF, uint8_t mstep) {
    static uint8_t c[16]={0};
    c[0]=addr;c[1]=0x84;c[2]=0x8A;c[3]=svF;c[4]=mstep;c[5]=0x6B;Emm_Send(c,6);
}
void Emm_V5_Modify_PDFlag(uint8_t addr, bool pdf) {
    static uint8_t c[16]={0};
    c[0]=addr;c[1]=0x50;c[2]=pdf;c[3]=0x6B;Emm_Send(c,4);
}
void Emm_V5_Read_Opt_Param_Sta(uint8_t addr) {
    static uint8_t c[16]={0};
    c[0]=addr;c[1]=0x1A;c[2]=0x6B;Emm_Send(c,3);
}
void Emm_V5_Modify_Motor_Type(uint8_t addr, bool svF, bool mottype) {
    static uint8_t c[16]={0}; uint8_t t=mottype?25:50;
    c[0]=addr;c[1]=0xD7;c[2]=0x35;c[3]=svF;c[4]=t;c[5]=0x6B;Emm_Send(c,6);
}
void Emm_V5_Modify_Firmware_Type(uint8_t addr, bool svF, bool fwtype) {
    static uint8_t c[16]={0};
    c[0]=addr;c[1]=0xD5;c[2]=0x69;c[3]=svF;c[4]=fwtype;c[5]=0x6B;Emm_Send(c,6);
}
void Emm_V5_Modify_Ctrl_Mode(uint8_t addr, bool svF, bool ctrl_mode) {
    static uint8_t c[16]={0};
    c[0]=addr;c[1]=0x46;c[2]=0x69;c[3]=svF;c[4]=ctrl_mode;c[5]=0x6B;Emm_Send(c,6);
}
void Emm_V5_Modify_Motor_Dir(uint8_t addr, bool svF, bool dir) {
    static uint8_t c[16]={0};
    c[0]=addr;c[1]=0xD4;c[2]=0x60;c[3]=svF;c[4]=dir;c[5]=0x6B;Emm_Send(c,6);
}
void Emm_V5_Modify_Lock_Btn(uint8_t addr, bool svF, bool lock) {
    static uint8_t c[16]={0};
    c[0]=addr;c[1]=0xD0;c[2]=0xB3;c[3]=svF;c[4]=lock;c[5]=0x6B;Emm_Send(c,6);
}
void Emm_V5_Modify_S_Vel(uint8_t addr, bool svF, bool s_vel) {
    static uint8_t c[16]={0};
    c[0]=addr;c[1]=0x4F;c[2]=0x71;c[3]=svF;c[4]=s_vel;c[5]=0x6B;Emm_Send(c,6);
}
void Emm_V5_Modify_OM_mA(uint8_t addr, bool svF, uint16_t om_ma) {
    static uint8_t c[16]={0};
    c[0]=addr;c[1]=0x44;c[2]=0x33;c[3]=svF;
    c[4]=(uint8_t)(om_ma>>8);c[5]=(uint8_t)(om_ma);c[6]=0x6B;Emm_Send(c,7);
}
void Emm_V5_Modify_FOC_mA(uint8_t addr, bool svF, uint16_t foc_mA) {
    static uint8_t c[16]={0};
    c[0]=addr;c[1]=0x45;c[2]=0x66;c[3]=svF;
    c[4]=(uint8_t)(foc_mA>>8);c[5]=(uint8_t)(foc_mA);c[6]=0x6B;Emm_Send(c,7);
}
void Emm_V5_Read_PID_Params(uint8_t addr) {
    static uint8_t c[16]={0};
    c[0]=addr;c[1]=0x21;c[2]=0x6B;Emm_Send(c,3);
}
void Emm_V5_Modify_PID_Params(uint8_t addr, bool svF, uint32_t kp, uint32_t ki, uint32_t kd) {
    static uint8_t c[20]={0};
    c[0]=addr;c[1]=0x4A;c[2]=0xC3;c[3]=svF;
    c[4]=(uint8_t)(kp>>24);c[5]=(uint8_t)(kp>>16);c[6]=(uint8_t)(kp>>8);c[7]=(uint8_t)(kp);
    c[8]=(uint8_t)(ki>>24);c[9]=(uint8_t)(ki>>16);c[10]=(uint8_t)(ki>>8);c[11]=(uint8_t)(ki);
    c[12]=(uint8_t)(kd>>24);c[13]=(uint8_t)(kd>>16);c[14]=(uint8_t)(kd>>8);c[15]=(uint8_t)(kd);
    c[16]=0x6B;Emm_Send(c,17);
}
void Emm_V5_Read_DMX512_Params(uint8_t addr) {
    static uint8_t c[16]={0};
    c[0]=addr;c[1]=0x49;c[2]=0x78;c[3]=0x6B;Emm_Send(c,4);
}
void Emm_V5_Modify_DMX512_Params(uint8_t addr, bool svF, uint16_t tch, uint8_t nch,
    uint8_t mode, uint16_t vel, uint16_t acc, uint16_t vel_step, uint32_t pos_step) {
    static uint8_t c[32]={0};
    c[0]=addr;c[1]=0xD9;c[2]=0x90;c[3]=svF;
    c[4]=(uint8_t)(tch>>8);c[5]=(uint8_t)(tch);c[6]=nch;c[7]=mode;
    c[8]=(uint8_t)(vel>>8);c[9]=(uint8_t)(vel);
    c[10]=(uint8_t)(acc>>8);c[11]=(uint8_t)(acc);
    c[12]=(uint8_t)(vel_step>>8);c[13]=(uint8_t)(vel_step);
    c[14]=(uint8_t)(pos_step>>24);c[15]=(uint8_t)(pos_step>>16);
    c[16]=(uint8_t)(pos_step>>8);c[17]=(uint8_t)(pos_step);
    c[18]=0x6B;Emm_Send(c,19);
}
void Emm_V5_Read_Pos_Window(uint8_t addr) {
    static uint8_t c[16]={0};
    c[0]=addr;c[1]=0x41;c[2]=0x6B;Emm_Send(c,3);
}
void Emm_V5_Modify_Pos_Window(uint8_t addr, bool svF, uint16_t prw) {
    static uint8_t c[16]={0};
    c[0]=addr;c[1]=0xD1;c[2]=0x07;c[3]=svF;
    c[4]=(uint8_t)(prw>>8);c[5]=(uint8_t)(prw);c[6]=0x6B;Emm_Send(c,7);
}
void Emm_V5_Read_Otocp(uint8_t addr) {
    static uint8_t c[16]={0};
    c[0]=addr;c[1]=0x13;c[2]=0x6B;Emm_Send(c,3);
}
void Emm_V5_Modify_Otocp(uint8_t addr, bool svF, uint16_t otp, uint16_t ocp, uint16_t time_ms) {
    static uint8_t c[16]={0};
    c[0]=addr;c[1]=0xD3;c[2]=0x56;c[3]=svF;
    c[4]=(uint8_t)(otp>>8);c[5]=(uint8_t)(otp);c[6]=(uint8_t)(ocp>>8);c[7]=(uint8_t)(ocp);
    c[8]=(uint8_t)(time_ms>>8);c[9]=(uint8_t)(time_ms);c[10]=0x6B;Emm_Send(c,11);
}
void Emm_V5_Read_Heart_Protect(uint8_t addr) {
    static uint8_t c[16]={0};
    c[0]=addr;c[1]=0x16;c[2]=0x6B;Emm_Send(c,3);
}
void Emm_V5_Modify_Heart_Protect(uint8_t addr, bool svF, uint32_t hp) {
    static uint8_t c[16]={0};
    c[0]=addr;c[1]=0x68;c[2]=0x38;c[3]=svF;
    c[4]=(uint8_t)(hp>>24);c[5]=(uint8_t)(hp>>16);c[6]=(uint8_t)(hp>>8);c[7]=(uint8_t)(hp);
    c[8]=0x6B;Emm_Send(c,9);
}
void Emm_V5_Read_Integral_Limit(uint8_t addr) {
    static uint8_t c[16]={0};
    c[0]=addr;c[1]=0x23;c[2]=0x6B;Emm_Send(c,3);
}
void Emm_V5_Modify_Integral_Limit(uint8_t addr, bool svF, uint32_t il) {
    static uint8_t c[16]={0};
    c[0]=addr;c[1]=0x4B;c[2]=0x57;c[3]=svF;
    c[4]=(uint8_t)(il>>24);c[5]=(uint8_t)(il>>16);c[6]=(uint8_t)(il>>8);c[7]=(uint8_t)(il);
    c[8]=0x6B;Emm_Send(c,9);
}
void Emm_V5_Read_System_State_Params(uint8_t addr) {
    static uint8_t c[16]={0};
    c[0]=addr;c[1]=0x43;c[2]=0x7A;c[3]=0x6B;Emm_Send(c,4);
}
void Emm_V5_Read_Motor_Conf_Params(uint8_t addr) {
    static uint8_t c[16]={0};
    c[0]=addr;c[1]=0x42;c[2]=0x6C;c[3]=0x6B;Emm_Send(c,4);
}

/* ==================== 澶氱數鏈鸿杞界増鏈?(MMCL) ==================== */
#define MMCL_LOAD(n) for(uint8_t _i=0;_i<(n);_i++){MMCL_cmd[MMCL_count]=c[_i];++MMCL_count;}

void Emm_V5_MMCL_Trig_Encoder_Cal(uint8_t addr){
    uint8_t c[16]={0};c[0]=addr;c[1]=0x06;c[2]=0x45;c[3]=0x6B;MMCL_LOAD(4);
}
void Emm_V5_MMCL_Reset_Motor(uint8_t addr){
    uint8_t c[16]={0};c[0]=addr;c[1]=0x08;c[2]=0x97;c[3]=0x6B;MMCL_LOAD(4);
}
void Emm_V5_MMCL_Reset_CurPos_To_Zero(uint8_t addr){
    uint8_t c[16]={0};c[0]=addr;c[1]=0x0A;c[2]=0x6D;c[3]=0x6B;MMCL_LOAD(4);
}
void Emm_V5_MMCL_Reset_Clog_Pro(uint8_t addr){
    uint8_t c[16]={0};c[0]=addr;c[1]=0x0E;c[2]=0x52;c[3]=0x6B;MMCL_LOAD(4);
}
void Emm_V5_MMCL_Restore_Motor(uint8_t addr){
    uint8_t c[16]={0};c[0]=addr;c[1]=0x0F;c[2]=0x5F;c[3]=0x6B;MMCL_LOAD(4);
}
void Emm_V5_MMCL_En_Control(uint8_t addr, bool state, bool snF){
    uint8_t c[16]={0};c[0]=addr;c[1]=0xF3;c[2]=0xAB;c[3]=(uint8_t)state;c[4]=snF;c[5]=0x6B;MMCL_LOAD(6);
}
void Emm_V5_MMCL_Vel_Control(uint8_t addr, uint8_t dir, uint16_t vel, uint8_t acc, bool snF){
    uint8_t c[16]={0};c[0]=addr;c[1]=0xF6;c[2]=dir;
    c[3]=(uint8_t)(vel>>8);c[4]=(uint8_t)(vel);c[5]=acc;c[6]=snF;c[7]=0x6B;MMCL_LOAD(8);
}
void Emm_V5_MMCL_Pos_Control(uint8_t addr, uint8_t dir, uint16_t vel, uint8_t acc, uint32_t clk, bool raF, bool snF){
    uint8_t c[16]={0};c[0]=addr;c[1]=0xFD;c[2]=dir;
    c[3]=(uint8_t)(vel>>8);c[4]=(uint8_t)(vel);c[5]=acc;
    c[6]=(uint8_t)(clk>>24);c[7]=(uint8_t)(clk>>16);c[8]=(uint8_t)(clk>>8);c[9]=(uint8_t)(clk);
    c[10]=raF;c[11]=snF;c[12]=0x6B;MMCL_LOAD(13);
}
void Emm_V5_MMCL_Stop_Now(uint8_t addr, bool snF){
    uint8_t c[16]={0};c[0]=addr;c[1]=0xFE;c[2]=0x98;c[3]=snF;c[4]=0x6B;MMCL_LOAD(5);
}
void Emm_V5_MMCL_Synchronous_motion(uint8_t addr){
    uint8_t c[16]={0};c[0]=addr;c[1]=0xFF;c[2]=0x66;c[3]=0x6B;MMCL_LOAD(4);
}
void Emm_V5_MMCL_Origin_Set_O(uint8_t addr, bool svF){
    uint8_t c[16]={0};c[0]=addr;c[1]=0x93;c[2]=0x88;c[3]=svF;c[4]=0x6B;MMCL_LOAD(5);
}
void Emm_V5_MMCL_Origin_Trigger_Return(uint8_t addr, uint8_t o_mode, bool snF){
    uint8_t c[16]={0};c[0]=addr;c[1]=0x9A;c[2]=o_mode;c[3]=snF;c[4]=0x6B;MMCL_LOAD(5);
}
void Emm_V5_MMCL_Origin_Interrupt(uint8_t addr){
    uint8_t c[16]={0};c[0]=addr;c[1]=0x9C;c[2]=0x48;c[3]=0x6B;MMCL_LOAD(4);
}
void Emm_V5_MMCL_Origin_Modify_Params(uint8_t addr, bool svF, uint8_t o_mode, uint8_t o_dir,
    uint16_t o_vel, uint32_t o_tm, uint16_t sl_vel, uint16_t sl_ma, uint16_t sl_ms, bool potF){
    uint8_t c[32]={0};
    c[0]=addr;c[1]=0x4C;c[2]=0xAE;c[3]=svF;c[4]=o_mode;c[5]=o_dir;
    c[6]=(uint8_t)(o_vel>>8);c[7]=(uint8_t)(o_vel);
    c[8]=(uint8_t)(o_tm>>24);c[9]=(uint8_t)(o_tm>>16);c[10]=(uint8_t)(o_tm>>8);c[11]=(uint8_t)(o_tm);
    c[12]=(uint8_t)(sl_vel>>8);c[13]=(uint8_t)(sl_vel);
    c[14]=(uint8_t)(sl_ma>>8);c[15]=(uint8_t)(sl_ma);
    c[16]=(uint8_t)(sl_ms>>8);c[17]=(uint8_t)(sl_ms);
    c[18]=potF;c[19]=0x6B;MMCL_LOAD(20);
}
void Emm_V5_MMCL_Auto_Return_Sys_Params_Timed(uint8_t addr, SysParams_t s, uint16_t time_ms){
    uint8_t i=0,c[16]={0};
    c[i]=addr;++i;c[i]=0x11;++i;c[i]=0x18;++i;
    switch(s){
        case S_VBUS:c[i]=0x24;++i;break;case S_CBUS:c[i]=0x26;++i;break;
        case S_CPHA:c[i]=0x27;++i;break;case S_ENCO:c[i]=0x29;++i;break;
        case S_CLKC:c[i]=0x30;++i;break;case S_ENCL:c[i]=0x31;++i;break;
        case S_CLKI:c[i]=0x32;++i;break;case S_TPOS:c[i]=0x33;++i;break;
        case S_SPOS:c[i]=0x34;++i;break;case S_VEL:c[i]=0x35;++i;break;
        case S_CPOS:c[i]=0x36;++i;break;case S_PERR:c[i]=0x37;++i;break;
        case S_VBAT:c[i]=0x38;++i;break;case S_TEMP:c[i]=0x39;++i;break;
        case S_FLAG:c[i]=0x3A;++i;break;case S_OFLAG:c[i]=0x3B;++i;break;
        case S_OAF:c[i]=0x3C;++i;break;case S_PIN:c[i]=0x3D;++i;break;
        default:break;
    }
    c[i]=(uint8_t)(time_ms>>8);++i;c[i]=(uint8_t)(time_ms);++i;c[i]=0x6B;++i;MMCL_LOAD(i);
}
void Emm_V5_MMCL_Read_Sys_Params(uint8_t addr, SysParams_t s){
    uint8_t i=0,c[16]={0};
    c[i]=addr;++i;
    switch(s){
        case S_VBUS:c[i]=0x24;++i;break;case S_CBUS:c[i]=0x26;++i;break;
        case S_CPHA:c[i]=0x27;++i;break;case S_ENCO:c[i]=0x29;++i;break;
        case S_CLKC:c[i]=0x30;++i;break;case S_ENCL:c[i]=0x31;++i;break;
        case S_CLKI:c[i]=0x32;++i;break;case S_TPOS:c[i]=0x33;++i;break;
        case S_SPOS:c[i]=0x34;++i;break;case S_VEL:c[i]=0x35;++i;break;
        case S_CPOS:c[i]=0x36;++i;break;case S_PERR:c[i]=0x37;++i;break;
        case S_VBAT:c[i]=0x38;++i;break;case S_TEMP:c[i]=0x39;++i;break;
        case S_FLAG:c[i]=0x3A;++i;break;case S_OFLAG:c[i]=0x3B;++i;break;
        case S_OAF:c[i]=0x3C;++i;break;case S_PIN:c[i]=0x3D;++i;break;
        default:break;
    }
    c[i]=0x6B;++i;MMCL_LOAD(i);
}

/* ==================== 断点续传 ==================== */

static int32_t gPendTarget = 0;    
static uint8_t gPendAddr   = 0;
static bool    gPendActive = false; 

/* 带断点续传的位置控制: 打断后自动补完剩余距离 */
void Emm_V5_MoveOrResume(uint8_t addr, uint8_t dir, uint16_t vel,
                         uint8_t acc, uint32_t pulses)
{
    if (gPendActive && gPendAddr == addr) {
        int32_t cur = Emm_V5_GetCPOS(addr);
        int32_t remaining = gPendTarget - cur;
        if (remaining > 0) {
            printf("[EMM] Resume: remaining=%d (was %d)\r\n",
                   (int)remaining, (int)gPendTarget);
            Emm_V5_Pos_Control(addr, dir, vel, acc,
                               (uint32_t)remaining, false, false);
            gPendTarget = cur + remaining;
        } else {
            gPendActive = false;
        }
    } else {
        gPendTarget = (int32_t)pulses;
        gPendAddr   = addr;
        gPendActive = true;
        Emm_V5_Pos_Control(addr, dir, vel, acc, pulses, false, false);
    }
}

/* 停止并标记为可续传 (代替 Emm_V5_Stop_Now) */
void Emm_V5_StopAndPend(uint8_t addr)
{
    Emm_V5_Stop_Now(addr, false);
    printf("[EMM] Stopped, pending=%d\r\n", (int)gPendTarget);
}

/* 清除续传状态 (任务完成/放弃) */
void Emm_V5_ClearPend(void)
{
    gPendActive = false;
    gPendTarget = 0;
}
