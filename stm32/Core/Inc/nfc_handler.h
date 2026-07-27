#ifndef __NFC_HANDLER_H__
#define __NFC_HANDLER_H__

#include "main.h"

/* 主循环调用: 新卡写入 BLOCKx, 旧卡读取并打印 */
void NFC_CardHandler(void);

/* 逐卡写入编号 1~7 (阻塞, 在初始化时调用) */
void NFC_WriteCards1to7(void);

#endif
