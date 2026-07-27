#ifndef __SPRAY_CTRL_H__
#define __SPRAY_CTRL_H__

#include "main.h"
#include <stdbool.h>

/* 撒药状态 */
typedef enum {
    SPRAY_IDLE = 0,
    SPRAY_PREPARE,        /* Z 轴从初始位置上升到扫描高度 */
    SPRAY_APPROACH,       /* Y 轴靠近树木，摄像头可随时要求停车 */
    SPRAY_SCAN,           /* 摆动扫描 (摄像头) */
    SPRAY_MIX,            /* 配药 (水泵+药泵) */
    SPRAY_SPRAYING,       /* 喷药 (上升+摆动+喷泵) */
    SPRAY_DONE            /* 完成, 回 IDLE */
} SprayState;

/* 虫害等级 (摄像头识别) */
typedef enum { PEST_NONE=0, PEST_LOW=1, PEST_MED=2, PEST_HIGH=3 } PestLevel;

void Spray_Init(void);
void Spray_Update(void);
void Spray_Start(PestLevel level);
void Spray_SetNfcSide(int side);
bool Spray_IsBusy(void);
void Spray_CameraStepperTestUpdate(void);
void Spray_PumpTest(void);
void Spray_PS2PumpTest(void);
void Pump_Water(bool on);
void Pump_Spray(bool on);
void Pump_Chem1(bool on);
void Pump_Chem2(bool on);
void Pump_Chem3(bool on);
void Pump_Chem4(bool on);

extern volatile PestLevel gPestLevel;
extern volatile int gNfcCardNum;

#endif
