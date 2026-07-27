/**
 * @file    nfc_handler.c
 * @brief   NFC 卡片管理: 新卡写入 BLOCKx, 旧卡读取打印
 *          第一张卡 → BLOCK1, 第二张 → BLOCK2, ...
 *          每张卡只写一次, 之后每次靠近都读取并打印已有数据
 */

#include "nfc_handler.h"
#include "pn532.h"
#include "pn532_2.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern UART_HandleTypeDef huart7;

#define MAX_CARDS   20
#define DATA_BLOCK  4

static uint8_t  gUids[MAX_CARDS][7];
static uint8_t  gUidLens[MAX_CARDS];
static int      gCardCount = 0;
static int      gNextBlock = 1;

static int FindUid(const uint8_t *uid, uint8_t len)
{
    for (int i = 0; i < gCardCount; i++) {
        if (gUidLens[i] == len && !memcmp(gUids[i], uid, len))
            return i;
    }
    return -1;
}

/* module: 0=左路 PN532, 1=右路 PN532_2 */
static void HandleCard(int module, const uint8_t *uid, uint8_t uLen)
{
    int idx = FindUid(uid, uLen);

    if (idx < 0) {
        /* ── 新卡: 写入 BLOCKx ── */
        if (gCardCount >= MAX_CARDS) {
            printf("[NFC] Full, max=%d\n", MAX_CARDS);
            return;
        }

        int ret_auth, ret_write;
        if (module == 0) {
            ret_auth  = PN532_MifareAuthBlock(DATA_BLOCK, uid, uLen);
            char data[16];
            snprintf(data, 16, "BLOCK%-2d", gNextBlock);
            ret_write = PN532_MifareWriteBlock(DATA_BLOCK, (uint8_t*)data);
        } else {
            ret_auth  = PN532_2_MifareAuthBlock(DATA_BLOCK, uid, uLen);
            char data[16];
            snprintf(data, 16, "BLOCK%-2d", gNextBlock);
            ret_write = PN532_2_MifareWriteBlock(DATA_BLOCK, (uint8_t*)data);
        }

        if (ret_auth != 0 || ret_write != 0) {
            printf("[NFC] Write BLOCK%d fail\n", gNextBlock);
            return;
        }

        memcpy(gUids[gCardCount], uid, uLen);
        gUidLens[gCardCount] = uLen;
        gCardCount++;
        printf("[NFC] NEW → BLOCK%d  (card #%d)\n", gNextBlock, gCardCount);
        gNextBlock++;
    } else {
        /* ── 旧卡: 读取已有的数据 ── */
        int ret_auth, ret_read;
        uint8_t data[16];
        if (module == 0) {
            ret_auth = PN532_MifareAuthBlock(DATA_BLOCK, uid, uLen);
            ret_read = PN532_MifareReadBlock(DATA_BLOCK, data);
        } else {
            ret_auth = PN532_2_MifareAuthBlock(DATA_BLOCK, uid, uLen);
            ret_read = PN532_2_MifareReadBlock(DATA_BLOCK, data);
        }

        if (ret_auth != 0 || ret_read != 0) {
            printf("[NFC] Read card #%d fail\n", idx + 1);
            return;
        }
        data[15] = '\0';
        printf("[NFC] Card #%d → %s\n", idx + 1, (char*)data);
    }
}

void NFC_CardHandler(void)
{
    uint8_t uid[10], uLen;

    if (PN532_JustWritten()) {
        if (PN532_InListPassiveTarget(PN532_CARD_ISO14443A, uid, &uLen, NULL, NULL) == PN532_OK) {
            HandleCard(0, uid, uLen);
        }
    }

    if (PN532_2_JustWritten()) {
        if (PN532_2_InListPassiveTarget(PN532_2_CARD_ISO14443A, uid, &uLen, NULL, NULL) == PN532_OK) {
            HandleCard(1, uid, uLen);
        }
    }
}

/* 持续读卡, 号码 1~7 通过 UART7 发送 AA AA [x] FF FF */
void NFC_WriteCards1to7(void)
{
    uint8_t prevUid[10] = {0};
    uint8_t prevLen = 0;

    while (1) {
        uint8_t uid[10], uLen;
        int ret = PN532_InListPassiveTarget(PN532_CARD_ISO14443A, uid, &uLen, NULL, NULL);

        if (ret != PN532_OK) { HAL_Delay(200); continue; }

        if (uLen == prevLen && !memcmp(uid, prevUid, uLen)) { HAL_Delay(200); continue; }

        HAL_Delay(100);

        uint8_t data[16];
        bool ok = false;
        for (int t = 0; t < 5; t++) {
            if (t > 0) {
                HAL_Delay(50);
                ret = PN532_InListPassiveTarget(PN532_CARD_ISO14443A, uid, &uLen, NULL, NULL);
                if (ret != PN532_OK) break;
            }
            if (PN532_MifareAuthBlock(DATA_BLOCK, uid, uLen) != PN532_OK) continue;
            if (PN532_MifareReadBlock(DATA_BLOCK, data) == PN532_OK) { ok = true; break; }
        }

        if (ok) {
            int cardNum = atoi((char*)data);
            if (cardNum >= 1 && cardNum <= 7) {
                uint8_t frame[] = {0xAA, 0xAA, (uint8_t)cardNum, 0xFF, 0xFF};
                HAL_UART_Transmit(&huart7, frame, 5, 100);
                printf("UART7: AA AA 0%d FF FF\n", cardNum);
            }
            PN532_InRelease(1);
            memcpy(prevUid, uid, uLen); prevLen = uLen;
            HAL_Delay(500);
            while (1) {
                ret = PN532_InListPassiveTarget(PN532_CARD_ISO14443A, uid, &uLen, NULL, NULL);
                if (ret != PN532_OK) break;
                HAL_Delay(300);
            }
            prevLen = 0;
        }
    }
}
