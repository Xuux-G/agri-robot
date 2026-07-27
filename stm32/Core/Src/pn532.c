/**
 * @file    pn532.c
 * @brief   PN532 NFC 模块驱动 (UART 模式)
 *          支持 Mifare Classic S50 卡读写
 */

#include "pn532.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "spray_ctrl.h"
#include "Motor.h"

extern UART_HandleTypeDef huart7;

static bool gPn532Ready = false;
static bool gJustWritten  = false;
int  gLastCardNum  = 0;
uint8_t gDoneUid[10] = {0};
uint8_t gDoneUidLen  = 0;

/* ================================================================
 *  UART 底层收发 (阻塞模式)
 * ================================================================ */

/**
 * @brief  uart_write - 阻塞发送数据到 PN532
 * @param  data : 发送缓冲区指针
 * @param  len  : 发送字节数
 */
static void uart_write(const uint8_t *data, uint16_t len)
{
  HAL_UART_Transmit(&PN532_UART_HANDLE, (uint8_t *)data, len, 200);
}

/**
 * @brief  uart_read_byte - 阻塞接收单个字节
 * @param  byte   : 接收字节存放指针
 * @param  timeout: 超时时间 (ms)
 * @return HAL_OK=成功, 其它=失败/超时
 */
static int uart_read_byte(uint8_t *byte, uint32_t timeout)
{
  return HAL_UART_Receive(&PN532_UART_HANDLE, byte, 1, timeout);
}

/**
 * @brief  uart_flush - 清空接收缓冲区中的残留字节
 */
static void uart_flush(void)
{
  uint8_t dummy;
  while (HAL_UART_Receive(&PN532_UART_HANDLE, &dummy, 1, 10) == HAL_OK) {}
}

/* ================================================================
 *  PN532 帧协议 (UART)
 *  帧格式: 00 00 FF LEN LCS TFI DATA[0..LEN-1] DCS 00
 *  LEN = TFI(1) + 数据字节数 (范围 0~255)
 *  LCS = 0x100 - LEN (低字节)
 *  DCS = ~(TFI + SUM_DATA) + 1 (低字节)
 * ================================================================ */

/**
 * @brief  pn532_send_frame - 构建并发送标准帧
 * @param  data : 命令数据 (不含 TFI)
 * @param  len  : 命令数据字节数
 * @return 实际发送的总帧字节数
 */
static int pn532_send_frame(const uint8_t *data, uint8_t len)
{
  uint8_t frame[8 + PN532_RESP_MAX_LEN];
  uint8_t idx = 0, dcs = 0, frameLen = len + 1;

  /* Preamble + Start Code */
  frame[idx++] = PN532_PREAMBLE;
  frame[idx++] = PN532_STARTCODE1;
  frame[idx++] = PN532_STARTCODE2;

  /* LEN + LCS */
  frame[idx++] = frameLen;
  frame[idx++] = (uint8_t)(0x100 - frameLen);

  /* TFI + Data */
  frame[idx++] = PN532_HOSTTOPN532;
  dcs += PN532_HOSTTOPN532;
  for (uint8_t i = 0; i < len; i++) { frame[idx++] = data[i]; dcs += data[i]; }

  /* DCS + Postamble */
  frame[idx++] = (uint8_t)(~dcs + 1);
  frame[idx++] = PN532_POSTAMBLE;

  uart_write(frame, idx);
  return idx;
}

/**
 * @brief  pn532_read_frame - 接收并解析响应帧
 * @param  resp   : 响应数据结构体 (存放 TFI+数据)
 * @param  timeout: 帧首字节超时 (ms)
 * @return PN532_OK / PN532_ERR_TIMEOUT / PN532_ERR_CHKSUM / PN532_ERR_FRAME
 */
static int pn532_read_frame(PN532_Response *resp, uint32_t timeout)
{
  uint8_t  byte;
  uint16_t len;
  uint8_t  dcs = 0;
  uint32_t tStart = HAL_GetTick();
  resp->len = 0;

  /* 同步到前导码 00 00 FF */
  for (int i = 0; i < 3; ) {
    if (HAL_GetTick() - tStart > timeout) return PN532_ERR_TIMEOUT;
    if (uart_read_byte(&byte, 50) != HAL_OK) continue;
    if (byte == 0x00 && i < 2) { i++; continue; }
    if (byte == 0xFF && i == 2) { i++; break;   }
    if (byte != 0x00) i = 0;
  }

  /* LEN + LCS */
  if (uart_read_byte(&byte, 50) != HAL_OK) return PN532_ERR_TIMEOUT;
  len = byte;
  if (uart_read_byte(&byte, 50) != HAL_OK) return PN532_ERR_TIMEOUT;
  if ((uint8_t)(len + byte) != 0x00) return PN532_ERR_CHKSUM;
  if (len > PN532_RESP_MAX_LEN) return PN532_ERR_FRAME;

  /* 读取 TFI + 数据 */
  for (uint16_t i = 0; i < len; i++) {
    if (uart_read_byte(&byte, 100) != HAL_OK) return PN532_ERR_TIMEOUT;
    resp->buf[i] = byte; dcs += byte;
  }
  resp->len = len;

  /* DCS + Postamble */
  if (uart_read_byte(&byte, 50) != HAL_OK) return PN532_ERR_TIMEOUT;
  if ((uint8_t)(dcs + byte) != 0x00) return PN532_ERR_CHKSUM;
  if (uart_read_byte(&byte, 50) != HAL_OK) return PN532_ERR_TIMEOUT;

  return PN532_OK;
}

/* ================================================================
 *  底层帧收发 (公开)
 * ================================================================ */

/**
 * @brief  PN532_SendCommand - 发送命令帧并等待 ACK
 *         ACK = 00 00 FF 00 FF 00
 *         内部先清空接收缓冲, 再发送帧, 然后在字节流中搜索 ACK 模式
 * @param  cmd    : 命令数据 (不含 TFI)
 * @param  cmdLen : 命令字节数
 * @return PN532_OK / PN532_ERR_TIMEOUT / PN532_ERR_ACK
 */
int PN532_SendCommand(const uint8_t *cmd, uint8_t cmdLen)
{
  uart_flush();
  pn532_send_frame(cmd, cmdLen);

  uint8_t buf[24];
  int     cnt = 0;
  uint32_t tStart = HAL_GetTick();

  while (cnt < 24) {
    if (HAL_GetTick() - tStart > 200) return PN532_ERR_TIMEOUT;
    if (uart_read_byte(&buf[cnt], 50) != HAL_OK) continue;
    cnt++;
    if (cnt >= 6) {
      uint8_t *p = &buf[cnt - 6];
      if (p[0]==0x00 && p[1]==0x00 && p[2]==0xFF &&
          p[3]==0x00 && p[4]==0xFF && p[5]==0x00) return PN532_OK;
    }
  }
  return PN532_ERR_ACK;
}

/**
 * @brief  PN532_ReadResponse - 读取 PN532 响应帧并校验帧标识
 * @param  resp   : 响应数据结构体 (buf 中存放 TFI+数据, len 为字节数)
 * @param  timeout: 帧头超时 (ms)
 * @return PN532_OK / PN532_ERR_TIMEOUT / PN532_ERR_CHKSUM / PN532_ERR_FRAME
 */
int PN532_ReadResponse(PN532_Response *resp, uint32_t timeout)
{
  int ret = pn532_read_frame(resp, timeout);
  if (ret != PN532_OK) return ret;
  if (resp->len < 2 || resp->buf[0] != PN532_PN532TOHOST) return PN532_ERR_FRAME;
  return PN532_OK;
}

/**
 * @brief  PN532_WaitForAck - 单独等待 ACK 帧 (6 字节精确匹配)
 * @param  timeout: 总超时 (ms)
 * @return PN532_OK / PN532_ERR_TIMEOUT / PN532_ERR_ACK
 */
int PN532_WaitForAck(uint32_t timeout)
{
  uint8_t ack[6], cnt = 0;
  uint32_t tStart = HAL_GetTick();
  while (cnt < 6) {
    if (HAL_GetTick() - tStart > timeout) return PN532_ERR_TIMEOUT;
    if (uart_read_byte(&ack[cnt], 50) == HAL_OK) cnt++;
  }
  if (ack[0]!=0x00 || ack[1]!=0x00 || ack[2]!=0xFF ||
      ack[3]!=0x00 || ack[4]!=0xFF || ack[5]!=0x00) return PN532_ERR_ACK;
  return PN532_OK;
}

/* ================================================================
 *  初始化 / 唤醒
 * ================================================================ */

/**
 * @brief  pn532_wakeup - 唤醒 PN532 并配置为正常模式
 *         发送波特率对齐字节 + 内嵌 SAMConfig(Normal Mode) 帧
 *         唤醒后 PN532 即处于可读写卡状态
 */
static void pn532_wakeup(void)
{
  uint8_t wake[] = {
    0x55, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x03, 0xFD, 0xD4, 0x14, 0x01, 0x17, 0x00
  };
  uart_write(wake, sizeof(wake));
  HAL_Delay(100);
  uart_flush();
}

/**
 * @brief  PN532_Init - 初始化 PN532 模块
 *         唤醒 → 读取固件版本 (最多重试 5 次) → 标记就绪
 * @return PN532_OK=成功, 其它=通信失败
 */
int PN532_Init(void)
{
  uint8_t ic, ver, rev, support;
  int ret;

  printf("NFC: init...\n");
  pn532_wakeup();

  for (int i = 0; i < 5; i++) {
    ret = PN532_GetFirmwareVersion(&ic, &ver, &rev, &support);
    if (ret == PN532_OK) break;
    HAL_Delay(50);
  }
  if (ret != PN532_OK) {
    printf("NFC: FAIL (ret=%d)\n", ret);
    gPn532Ready = false;
    return ret;
  }

  printf("NFC: OK (IC=0x%02X v%d.%d)\n", ic, ver, rev);

  /* RF 最大功率配置, 提升读卡距离 */
  {
    PN532_Response rsp;
    uint8_t rfCfg[] = {0x32, 0x01, 0x02};  /* RFConfiguration: 自动射频, 最大调制 */
    PN532_SendCommand(rfCfg, 3);
    PN532_ReadResponse(&rsp, 100);

    uint8_t rfCfg2[] = {0x32, 0x02, 0x0A, 0x59, 0x01, 0x00, 0x12, 0x12};
    PN532_SendCommand(rfCfg2, 8);
    PN532_ReadResponse(&rsp, 100);
  }

  gPn532Ready = true;
  return PN532_OK;
}

/**
 * @brief  PN532_IsReady - 查询模块是否初始化成功
 * @return true=就绪, false=未初始化
 */
bool PN532_IsReady(void)   { return gPn532Ready; }
bool PN532_JustWritten(void) { bool v=gJustWritten; gJustWritten=false; return v; }

/* ================================================================
 *  PN532 标准命令
 * ================================================================ */

/**
 * @brief  PN532_GetFirmwareVersion - 获取固件版本
 * @param  ic     : [出] 芯片型号 (0x32=PN532)
 * @param  ver    : [出] 主版本号
 * @param  rev    : [出] 次版本号
 * @param  support: [出] 功能支持位图
 * @return PN532_OK=成功
 */
int PN532_GetFirmwareVersion(uint8_t *ic, uint8_t *ver, uint8_t *rev, uint8_t *support)
{
  PN532_Response resp; int ret;
  ret = PN532_SendCommand((const uint8_t *)"\x02", 1);
  if (ret) return ret;
  ret = PN532_ReadResponse(&resp, 200);
  if (ret) return ret;
  if (resp.len < 6) return PN532_ERR_FRAME;
  *ic = resp.buf[2]; *ver = resp.buf[3]; *rev = resp.buf[4]; *support = resp.buf[5];
  return PN532_OK;
}

/**
 * @brief  PN532_SAMConfig - 配置 SAM (安全访问模块)
 * @param  mode   : 工作模式 (0x01=普通 0x02=虚拟卡 0x03=有线卡 0x04=双卡)
 * @param  timeout: 超时 ×50ms (0=不超时)
 * @param  irq    : 0=不用 IRQ, 1=使用 IRQ
 * @return PN532_OK=成功
 */
int PN532_SAMConfig(uint8_t mode, uint8_t timeout, uint8_t irq)
{
  PN532_Response resp; int ret;
  uint8_t cmd[] = { 0x14, mode, timeout, irq };
  ret = PN532_SendCommand(cmd, sizeof(cmd)); if (ret) return ret;
  ret = PN532_ReadResponse(&resp, 200);
  return ret;
}

/**
 * @brief  PN532_InListPassiveTarget - 寻卡 (搜索场内的无源目标)
 * @param  cardType: 卡片类型 (PN532_CARD_ISO14443A=0x00 等)
 * @param  uid     : [出] 卡片 UID 缓冲区 (至少 10 字节)
 * @param  uidLen  : [出] UID 实际长度 (4 或 7 字节)
 * @param  atqa    : [出] ATQA 应答 (2 字节, 可为 NULL)
 * @param  sak     : [出] SAK 选择确认 (1 字节, 可为 NULL)
 * @return PN532_OK=找到卡, PN532_ERR_TIMEOUT=无卡
 */
int PN532_InListPassiveTarget(uint8_t cardType, uint8_t *uid, uint8_t *uidLen,
                              uint8_t *atqa, uint8_t *sak)
{
  PN532_Response resp; int ret;
  uint8_t cmd[] = { 0x4A, 1, cardType };
  ret = PN532_SendCommand(cmd, sizeof(cmd)); if (ret) return ret;
  ret = PN532_ReadResponse(&resp, 200); if (ret) return ret;
  if (resp.len < 3 || resp.buf[1] != 0x4B) return PN532_ERR_FRAME;

  uint8_t nbTg = resp.buf[2];
  if (nbTg == 0) { *uidLen = 0; return PN532_ERR_TIMEOUT; }

  uint8_t off = 3, nbResp = resp.buf[off++];
  if (atqa && nbResp >= 2) { atqa[0]=resp.buf[off++]; atqa[1]=resp.buf[off++]; nbResp-=2; }
  else { off+=2; nbResp-=2; }
  if (sak && nbResp >= 1) { *sak=resp.buf[off++]; nbResp--; }
  else { off++; nbResp--; }

  *uidLen = resp.buf[off++];
  for (uint8_t i = 0; i < *uidLen && i < 10; i++) uid[i] = resp.buf[off++];
  return PN532_OK;
}

/**
 * @brief  PN532_InDataExchange - 与已激活的卡片交换数据
 *         封装 Mifare 的读/写/认证等底层命令
 * @param  txData : 发送数据 (Mifare 命令 + 参数)
 * @param  txLen  : 发送数据字节数
 * @param  rxData : [出] 接收数据缓冲区
 * @param  rxLen  : [入/出] 入=缓冲区大小, 出=实际数据长度
 * @return PN532_OK=成功, 非0=Pn532或卡片返回的错误码
 */
int PN532_InDataExchange(const uint8_t *txData, uint8_t txLen,
                         uint8_t *rxData, uint8_t *rxLen)
{
  PN532_Response resp; int ret;
  uint8_t *cmd = (uint8_t *)malloc(txLen + 2);
  if (!cmd) return PN532_ERR_FRAME;
  cmd[0]=0x40; cmd[1]=0x01; memcpy(cmd+2, txData, txLen);
  ret = PN532_SendCommand(cmd, txLen+2); free(cmd); if (ret) return ret;
  ret = PN532_ReadResponse(&resp, 1500); if (ret) return ret;
  if (resp.len < 3) return PN532_ERR_FRAME;
  if (resp.buf[2] != 0x00) return resp.buf[2];
  uint8_t n = resp.len - 3;
  if (n > *rxLen) n = *rxLen; *rxLen = n;
  if (n) memcpy(rxData, &resp.buf[3], n);
  return PN532_OK;
}

/**
 * @brief  PN532_InDeselect - 取消选择目标设备
 * @param  target : 目标编号 (通常为 1)
 * @return PN532_OK=成功
 */
int PN532_InDeselect(uint8_t target) {
  PN532_Response resp; int ret;
  uint8_t cmd[]={0x44,target};
  ret=PN532_SendCommand(cmd,2); if(ret) return ret;
  return PN532_ReadResponse(&resp,200);
}

/**
 * @brief  PN532_InRelease - 释放目标设备
 * @param  target : 目标编号 (通常为 1)
 * @return PN532_OK=成功
 */
int PN532_InRelease(uint8_t target) {
  PN532_Response resp; int ret;
  uint8_t cmd[]={0x52,target};
  ret=PN532_SendCommand(cmd,2); if(ret) return ret;
  return PN532_ReadResponse(&resp,200);
}

/* ================================================================
 *  Mifare Classic 专用操作 (S50 / 1K)
 *  扇区结构: 每扇区 4 块, 前 3 块为数据块, 第 4 块为密钥尾块
 *  尾块格式: KeyA(6) + AccessBits(3) + UserByte(1) + KeyB(6)
 * ================================================================ */

/**
 * @brief  PN532_MifareAuthBlock - Mifare Classic 扇区认证 (Key A)
 *         认证目标块所在扇区, 成功后该扇区内所有块可读写
 * @param  block  : 要认证的块号 (0~63 for S50, 尾块不可用于认证)
 * @param  uid    : 卡片 UID (4 或 7 字节)
 * @param  uidLen : UID 字节长度
 * @return PN532_OK=认证成功, 非0=失败 (0x14=密钥错误, 0xFFFFFFFF=超时)
 * @note   默认使用 Key A = FF FF FF FF FF FF
 */
int PN532_MifareAuthBlock(uint8_t block, const uint8_t *uid, uint8_t uidLen)
{
  uint8_t key[6] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };
  uint8_t tx[12], rx[4], rxLen=4, uid4[4]={0};
  tx[0]=0x60; tx[1]=block;
  memcpy(tx+2, key, 6);
  for (uint8_t i=0; i<uidLen && i<4; i++) uid4[i]=uid[i];
  memcpy(tx+8, uid4, 4);
  return PN532_InDataExchange(tx, 12, rx, &rxLen);
}

/**
 * @brief  PN532_MifareReadBlock - Mifare Classic 读块 (16 字节)
 * @param  block : 块号 (0~63, 必须先认证该块所在扇区)
 * @param  data  : [出] 16 字节数据缓冲区
 * @return PN532_OK=成功
 */
int PN532_MifareReadBlock(uint8_t block, uint8_t *data)
{
  uint8_t tx[]={0x30,block}, rxLen=18;
  int ret=PN532_InDataExchange(tx,2,data,&rxLen);
  if(ret) return ret;
  return (rxLen==16) ? PN532_OK : PN532_ERR_FRAME;
}

/**
 * @brief  PN532_MifareWriteBlock - Mifare Classic 写块 (16 字节)
 * @param  block : 块号 (0~63, 必须先认证该块所在扇区)
 * @param  data  : 要写入的 16 字节数据
 * @return PN532_OK=写入成功
 * @warning 不要写入扇区尾块 (3/7/11/.../63), 否则扇区可能永久锁死
 */
int PN532_MifareWriteBlock(uint8_t block, const uint8_t *data)
{
  uint8_t tx[18], rx[4], rxLen=4;
  tx[0]=0xA0; tx[1]=block;
  memcpy(tx+2, data, 16);
  return PN532_InDataExchange(tx, 18, rx, &rxLen);
}

/* ================================================================
 *  后台轮询 (主循环调用)
 * ================================================================ */

static void printHex(const uint8_t *d, int n) {
  for(int i=0;i<n;i++) printf("%02X ",d[i]); printf("\n");
}

/**
 * @brief  PN532_Worker - NFC 后台轮询 + 自动写入
 *         每 300ms 寻卡一次, 新卡: 读 Block0 → 检测白卡 → 写入 → 验证
 * @note   白卡 = Block4 数据全 0x00, 写入递增序号+UID+校验
 */
void PN532_Worker(void)
{
  static uint8_t  prevUid[10], prevLen=0;
  static uint32_t lastPoll=0, lastSeen=0;
  static uint32_t writeCount=0;

  if(!gPn532Ready) return;
  if(HAL_GetTick()-lastPoll < 20) return;
  lastPoll = HAL_GetTick();

  /* 同卡 2 秒后可以重新检测 */
  if(prevLen && HAL_GetTick()-lastSeen > 2000)
    { prevLen=0; }

  uint8_t uid[10], uLen, atqa[2], sak;
  int ret = PN532_InListPassiveTarget(PN532_CARD_ISO14443A, uid, &uLen, atqa, &sak);

  if(ret == PN532_OK) {
    bool same = (uLen==prevLen && !memcmp(uid,prevUid,uLen));
    memcpy(prevUid, uid, uLen); prevLen = uLen;
    lastSeen = HAL_GetTick();

    if (same && !Motor_NfcStopped()) return;

    /* 已读过的卡不触发 */
    if (!Motor_NfcStopped() && gDoneUidLen == uLen && !memcmp(gDoneUid, uid, uLen))
        return;

    if (!same || Motor_NfcStopped()) {
      gJustWritten = true;
      printf("\n[NFC] UID(%d): ", uLen);
      printHex(uid, uLen);
      /* 行驶中: 只发UID就返回; 停车后: 继续读数据 */
      if (!Motor_NfcStopped()) return;
    }

    /* ── 停车后读卡: 等 150ms 振动消退 → 重新寻卡 → 每轮最多重试 ── */
    { static uint32_t _sm=0; static bool _sw=false; static int _rc=0;
      if(!Motor_NfcStopped()){_sw=false;_rc=0;return;}
      if(!_sw){_sm=HAL_GetTick();_sw=true;_rc=0;printf("[NFC] stabilize 150ms...\n");return;}
      if(HAL_GetTick()-_sm<150)return;
      _rc++;
      if(_rc>20){printf("[NFC] read timeout after %d rounds\n",_rc);_sw=false;_rc=0;return;} }

    /* 重新确认卡在场, 不在场不放弃, 下一轮继续 */
    HAL_Delay(50);
    if(PN532_InListPassiveTarget(PN532_CARD_ISO14443A,uid,&uLen,NULL,NULL)!=PN532_OK)
      {printf("[NFC] card lost, retry next round\n");return;}

    uint8_t hdr[16];

    /* ─── 认证+读取, 重试 8 次 ─── */
    HAL_Delay(30);
    bool ok = false;
    for (int t = 0; t < 8; t++) {
      if (t > 0) {
        HAL_Delay(50);
        if (PN532_InListPassiveTarget(PN532_CARD_ISO14443A, uid, &uLen, NULL, NULL) != PN532_OK)
          continue;  /* 不放弃, 继续尝试 */
      }
      if (PN532_MifareAuthBlock(4, uid, uLen) != PN532_OK) continue;
      if (PN532_MifareReadBlock(4, hdr) == PN532_OK) { ok = true; break; }
    }

    if (ok) {
      bool blank = true;
      for (int i = 0; i < 16; i++) { if (hdr[i] != 0) { blank = false; break; } }
      if (!blank) {
        hdr[15] = '\0';
        printf("       data: %s\n", (char*)hdr);
        int cardNum = 0;
        for (int _i = 0; hdr[_i]; _i++)
            if (hdr[_i] >= '1' && hdr[_i] <= '6') { cardNum = hdr[_i] - '0'; break; }
        if (cardNum >= 1 && cardNum <= 6) {
          if (gDoneUidLen == uLen && !memcmp(gDoneUid, uid, uLen)) {
            printf("       (same card, skip)\n");
          } else {
            printf("\n=== NFC CARD=%d ===\n\n", cardNum);
            gNfcCardNum = cardNum;
            memcpy(gDoneUid, uid, uLen); gDoneUidLen = uLen;
            {
              uint8_t frame[] = {0xAA, 0xAA, (uint8_t)cardNum, 0xFF, 0xFF};
              for (int tx = 0; tx < 3; tx++) {
                if (HAL_UART_Transmit(&huart7, frame, 5, 100) == HAL_OK) break;
                HAL_Delay(5);
              }
              printf("       UART7: AA AA 0%d FF FF\n", cardNum);
            }
          }
        }
      } else {
        printf("       (blank)\n");
      }
    } else {
      printf("       read err\n");
    }
  } else if(ret == PN532_ERR_TIMEOUT) {
    if(prevLen) { printf("[NFC] removed\n"); prevLen=0; }
  }
}
