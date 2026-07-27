#ifndef __PN532_H__
#define __PN532_H__

#include "main.h"
#include <stdbool.h>

/* ---- 串口句柄 (CubeMX 中已配置 USART2) ---- */
#define PN532_UART_HANDLE   huart2

/* ---- 写入卡片的中文内容 (UTF-8, 最长 ~100 汉字) ---- */
#define NFC_WRITE_TEXT  "黄叶病 严重"

/* ---- 帧常量 ---- */
#define PN532_PREAMBLE      0x00
#define PN532_STARTCODE1    0x00
#define PN532_STARTCODE2    0xFF
#define PN532_POSTAMBLE     0x00
#define PN532_HOSTTOPN532   0xD4    /* 主机 → PN532 */
#define PN532_PN532TOHOST   0xD5    /* PN532 → 主机 */

/* ---- PN532 命令码 ---- */
#define PN532_CMD_DIAGNOSE              0x00  /* 自检诊断 */
#define PN532_CMD_GETFIRMWAREVERSION    0x02  /* 获取固件版本 */
#define PN532_CMD_GETGENERALSTATUS      0x04  /* 获取通用状态 */
#define PN532_CMD_READREGISTER          0x06  /* 读寄存器 */
#define PN532_CMD_WRITEREGISTER         0x08  /* 写寄存器 */
#define PN532_CMD_READGPIO              0x0C  /* 读 GPIO */
#define PN532_CMD_WRITEGPIO             0x0E  /* 写 GPIO */
#define PN532_CMD_SETSERIALBAUDRATE     0x10  /* 设置串口波特率 */
#define PN532_CMD_SETPARAMETERS         0x12  /* 设置参数 */
#define PN532_CMD_SAMCONFIGURATION      0x14  /* SAM 配置 */
#define PN532_CMD_POWERDOWN             0x16  /* 掉电 */
#define PN532_CMD_RFCONFIGURATION       0x32  /* RF 配置 */
#define PN532_CMD_RFREGULATIONTEST      0x58  /* RF 调校测试 */
#define PN532_CMD_INJUMPFORDEP          0x56  /* 跳转 DEP 模式 */
#define PN532_CMD_INJUMPFORPSL          0x46  /* 跳转 PSL 模式 */
#define PN532_CMD_INLISTPASSIVETARGET   0x4A  /* 寻卡 */
#define PN532_CMD_INATR                 0x50  /* 发送 ATR */
#define PN532_CMD_INPSL                 0x4E  /* 发送 PSL */
#define PN532_CMD_INDATAEXCHANGE        0x40  /* 数据交换 */
#define PN532_CMD_INDESELECT            0x44  /* 取消选择 */
#define PN532_CMD_INRELEASE             0x52  /* 释放 */
#define PN532_CMD_INSELECT              0x54  /* 选择 */
#define PN532_CMD_INAUTOPOLL            0x60  /* 自动轮询 */
#define PN532_CMD_TGINITASTARGET        0x8C  /* 初始化为卡模式 */
#define PN532_CMD_TGSETGENERALBYTES     0x92  /* 设置通用字节 */
#define PN532_CMD_TGGETDATA             0x86  /* 获取数据 */
#define PN532_CMD_TGSETDATA             0x8E  /* 设置数据 */
#define PN532_CMD_TGSETMETADATA         0x94  /* 设置元数据 */
#define PN532_CMD_TGGETINITIATORCOMMAND 0x88  /* 获取读卡器命令 */
#define PN532_CMD_TGRESPONSETOINITIATOR 0x90  /* 响应读卡器 */
#define PN532_CMD_TGGETTARGETSTATUS     0x8A  /* 获取目标状态 */

/* ---- 卡片类型 (InListPassiveTarget) ---- */
#define PN532_CARD_ISO14443A     0x00  /* Mifare / NFC-A */
#define PN532_CARD_FELICA_212    0x01  /* FeliCa 212kbps */
#define PN532_CARD_FELICA_424    0x02  /* FeliCa 424kbps */
#define PN532_CARD_ISO14443B     0x03  /* NFC-B */
#define PN532_CARD_INNOVISION    0x04  /* Innovision Jewel */

/* ---- 波特率 ---- */
#define PN532_BR_106     0x00  /* 106 kbps */
#define PN532_BR_212     0x01  /* 212 kbps */
#define PN532_BR_424     0x02  /* 424 kbps */

/* ---- SAM 工作模式 ---- */
#define PN532_SAM_NORMAL_MODE    0x01  /* 普通模式 - 读写卡 */
#define PN532_SAM_VIRTUAL_CARD   0x02  /* 虚拟卡模式 */
#define PN532_SAM_WIRED_CARD     0x03  /* 有线卡模式 */
#define PN532_SAM_DUAL_CARD      0x04  /* 双卡模式 */

/* ---- 状态码 ---- */
#define PN532_OK          0         /* 成功 */
#define PN532_ERR_TIMEOUT (-1)      /* 超时 */
#define PN532_ERR_FRAME   (-2)      /* 帧格式错误 */
#define PN532_ERR_CHKSUM  (-3)      /* 校验和错误 */
#define PN532_ERR_ACK     (-4)      /* ACK 错误 */
#define PN532_ERR_NAK     (-5)      /* NAK 错误 */

/* ---- 响应缓冲区 ---- */
#define PN532_RESP_MAX_LEN  264

typedef struct {
  uint8_t  buf[PN532_RESP_MAX_LEN];
  uint16_t len;
} PN532_Response;

/* ---- 公开接口 ---- */
int  PN532_Init(void);                                           /* 初始化: 唤醒 + 读版本 + SAM 配置 */
bool PN532_IsReady(void);                                        /* 返回模块是否就绪 */
int  PN532_GetFirmwareVersion(uint8_t *ic, uint8_t *ver,        /* 读取固件版本 */
                              uint8_t *rev, uint8_t *support);
int  PN532_SAMConfig(uint8_t mode, uint8_t timeout, uint8_t irq); /* SAM 配置 */
int  PN532_InListPassiveTarget(uint8_t cardType,                 /* 寻卡: 返回 UID/ATQA/SAK */
                               uint8_t *uid, uint8_t *uidLen,
                               uint8_t *atqa, uint8_t *sak);
int  PN532_InDataExchange(const uint8_t *txData, uint8_t txLen,  /* 数据交换 */
                          uint8_t *rxData, uint8_t *rxLen);
int  PN532_InDeselect(uint8_t target);                            /* 取消选择 */
int  PN532_InRelease(uint8_t target);                             /* 释放目标 */
int  PN532_MifareAuthBlock(uint8_t block, const uint8_t *uid,     /* Mifare Classic 块认证 (Key A, 默认 FF..FF) */
                           uint8_t uidLen);
int  PN532_MifareReadBlock(uint8_t block, uint8_t *data);          /* Mifare Classic 读一个块 (16 字节) */
int  PN532_MifareWriteBlock(uint8_t block, const uint8_t *data);   /* Mifare Classic 写一个块 (16 字节) */
void PN532_Worker(void);                                          /* 后台处理 (主循环中调用) */
bool PN532_JustWritten(void);                                      /* 刚写入过卡片? (读取后自动清零) */

/* ---- 底层帧收发 ---- */
int  PN532_SendCommand(const uint8_t *cmd, uint8_t cmdLen);       /* 发送命令帧并等待 ACK */
int  PN532_ReadResponse(PN532_Response *resp, uint32_t timeout);  /* 读取响应帧 */
int  PN532_WaitForAck(uint32_t timeout);                          /* 仅等待 ACK */

extern uint8_t gDoneUid[10];
extern uint8_t gDoneUidLen;

#endif /* __PN532_H__ */
