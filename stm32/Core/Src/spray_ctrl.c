/**
 * @file    spray_ctrl.c
 * @brief   撒药流程: Z升顶 → 摄像头引导靠近 → Z下降扫荡 → 配药 → Z上升喷药 → Y收臂
 *          X=旋转(1), Z=升降(2), Y=伸缩(3)
 */

#include "spray_ctrl.h"
#include "emm_v5.h"
#include "tim.h"
#include "camera.h"
#include "pstwo.h"
#include <stdio.h>

#define AXIS_X   1  /* 底部旋转轴 */
#define AXIS_Z   2  /* 竖直升降轴 */
#define AXIS_Y   3  /* 前后伸缩轴 */

/* Y轴手臂伸缩 */
#define Y_ARM_SPEED      100
#define Y_ARM_ACC        100
#define Y_MOVE_TIME(pulses) ((pulses) * 60000UL / ((Y_ARM_SPEED) * 6400))

/* 摄像头动作量到驱动脉冲的换算 */
#define Y_PULSES_PER_CM     1111UL
#define Y_APPROACH_FIXED_CM   12U
#define Y_APPROACH_TARGET_PULSES ((uint32_t)Y_APPROACH_FIXED_CM * Y_PULSES_PER_CM)
#define Y_DRIVER_RETRY_INTERVAL_MS 100U
#define Y_DRIVER_RETRY_MAX_COUNT     4U
#define X_ALIGN_RPM           2U
#define X_ALIGN_RPM_MIN        1U
#define X_ALIGN_DEADZONE       1U
#define X_MAX_COMMAND_UNIT    60U
#define X_DIR_INVERT           1U
#define APPROACH_SPEED       100U
#define APPROACH_ACC          30U
#define X_ALIGN_ACC           20U

#define CAMERA_FLAG_MOVE       0U
#define CAMERA_FLAG_ARRIVED    1U
#define CAMERA_FLAG_LOST       2U
#define CAMERA_FLAG_TOO_CLOSE  3U
#define CAMERA_FLAG_TASK_DONE  4U

/* Z轴升降参数 */
#define Z_DOWN_SPEED    25
#define Z_DOWN_SWINGS   4

#define Z_UP_SPEED      100
#define Z_UP_SWINGS     6

/* X轴摆动: 速度模式 */
#define X_DOWN_SPEED    3
#define X_DOWN_ACC      10
#define Z_DOWN_MS       2600

#define X_UP_SPEED      6
#define X_UP_ACC        10
#define Z_UP_MS         1600

/* Z轴升降脉冲 */
#define Z_UP_PULSES     41000
#define Z_DOWN_PULSES   41000

#define Z_DOWN_TIME     ((Z_DOWN_PULSES) * 60000UL / ((Z_DOWN_SPEED) * 6400))
#define Z_UP_TIME       ((Z_UP_PULSES) * 60000UL / ((Z_UP_SPEED) * 6400))

#define Z_RISE_TIME     4100

volatile PestLevel gPestLevel = PEST_NONE;

static SprayState gState    = SPRAY_IDLE;
static uint32_t   gPhaseMs  = 0;
static int        gSwingCnt = 0;
static PestLevel  gLevel    = PEST_NONE;
static uint32_t   gSwingMs  = 0;
static int        gSwingDir = 0;
static bool       gXStopped = false;
static bool       gSwingInitDone = false;
static bool       _sent8 = false;
static bool       gZAtTop    = false;
static bool       gRisingZ   = false;
static int        gDoneStep  = 0;
static uint32_t   gYCommandedPulses = 0;
static uint32_t   gYRetractPulses = 0;
static uint32_t   gYRetractTime = 0;
static bool       gYApproachActive = false;
static bool       gYStepRunning = false;
static uint32_t   gYStepStartMs = 0;
static uint32_t   gYStepMoveTime = 0;
static uint32_t   gYLastDriverCommandMs = 0;
static uint8_t    gYDriverCommandCount = 0;
static CameraCommand gPreparedCommand;
static bool       gPreparedCommandValid = false;
static bool       gZDescending = false;
static bool       gZAtBottom    = false;
static bool       gMixingStarted = false;
static uint32_t   gMixStartMs = 0;
static int        gXDirection = 0;   /* 0=喷药泵朝右, 1=朝左 */
static int        gNfcSide    = 0;   /* 1=右NFC触发, 2=左NFC触发 */
#define X_180_PULSES  13500   /* X轴180度旋转脉冲数 */

/* ── Z轴 ── */
static void Z_Up(void)   { Emm_V5_Pos_Control(AXIS_Z, 0, Z_UP_SPEED,   20, Z_UP_PULSES,   false, false); HAL_Delay(10); }
static void Z_Down(void) { Emm_V5_Pos_Control(AXIS_Z, 1, Z_DOWN_SPEED, 20, Z_DOWN_PULSES, false, false); HAL_Delay(10); }

/* Y 轴回绝对零点 */
static uint32_t Y_Retract(void)
{
    uint32_t retract_pulses = gYCommandedPulses;
    Emm_V5_Pos_Control(AXIS_Y, 0, Y_ARM_SPEED, Y_ARM_ACC, 0, true, false);
    gYRetractTime = Y_MOVE_TIME(Y_APPROACH_TARGET_PULSES);
    gYCommandedPulses = 0;
    gYRetractPulses = 0;
    gYStepRunning = false;
    gYApproachActive = false;
    gYLastDriverCommandMs = 0;
    gYDriverCommandCount = 0;
    HAL_Delay(10);
    return retract_pulses;
}

/* ── X轴摆动 ── */
static void Swing_Go(int dir, int rpm, uint8_t acc) {
    Emm_V5_Vel_Control(AXIS_X, 0, 0, acc, false);
    HAL_Delay(10);
    Emm_V5_Vel_Control(AXIS_X, (uint8_t)dir, (uint16_t)rpm, acc, false);
    HAL_Delay(10);
}

static uint8_t MapXDir(uint8_t camera_dir)
{
    uint8_t dir = (camera_dir != 0) ? 1U : 0U;
    if (X_DIR_INVERT) dir = (dir == 0U) ? 1U : 0U;
    return dir;
}

static void StartXAlignment(const CameraCommand *command)
{
    uint8_t motor_dir = MapXDir(command->x_dir);
    uint16_t unit = command->x_dist;
    if (unit > X_MAX_COMMAND_UNIT) unit = X_MAX_COMMAND_UNIT;

    Emm_V5_Vel_Control(AXIS_X, motor_dir, X_ALIGN_RPM, X_ALIGN_ACC, false);
    HAL_Delay(10);
    printf("[X] align cam_dir=%u motor_dir=%u unit=%u rpm=%u\n",
           command->x_dir, motor_dir, unit, X_ALIGN_RPM);
}

static void Y_RecordInterruptedStep(uint32_t now) { (void)now; gYStepRunning = false; }

static void Y_ApproachUpdate(uint32_t now)
{
    if (!gYApproachActive) return;
    if (!gYStepRunning) return;
    if (now - gYStepStartMs < gYStepMoveTime) return;
    gYStepRunning = false;
}

static void Y_SendApproachTarget(uint32_t now, bool retry)
{
    Emm_V5_Pos_Control(AXIS_Y, 1, APPROACH_SPEED, APPROACH_ACC,
                       Y_APPROACH_TARGET_PULSES, false, false);
    gYLastDriverCommandMs = now;
    gYDriverCommandCount++;
    HAL_Delay(10);
    printf("[Spray] Y approach %s, pulses=%lu\n",
           retry ? "retry" : "start", (unsigned long)Y_APPROACH_TARGET_PULSES);
}

static void Y_StartApproach(uint32_t now)
{
    if (gYApproachActive || gYStepRunning) {
        return;  /* 已在前进, 相对模式不重发 */
    }
    printf("[Y] START approach pulses=%lu\n", (unsigned long)Y_APPROACH_TARGET_PULSES);
    gYDriverCommandCount = 0;
    Y_SendApproachTarget(now, false);
    gYCommandedPulses = Y_APPROACH_TARGET_PULSES;
    gYStepStartMs = now;
    gYStepMoveTime = Y_MOVE_TIME(Y_APPROACH_TARGET_PULSES);
    gYStepRunning = true;
    gYApproachActive = true;
}

static void Approach_Stop(void)
{
    Y_RecordInterruptedStep(HAL_GetTick());
    Emm_V5_Stop_Now(AXIS_Y, false);
    HAL_Delay(10);
    Emm_V5_Stop_Now(AXIS_X, false);
    HAL_Delay(10);
    gYApproachActive = false;
    gYDriverCommandCount = 0;
}

static void Start_Down_Scan(uint32_t now)
{
    Approach_Stop();
    gYRetractPulses = gYCommandedPulses;
    gYRetractTime = Y_MOVE_TIME(Y_APPROACH_TARGET_PULSES);
    gState = SPRAY_SCAN;
    gPhaseMs = now;
    gSwingCnt = 0;
    gSwingMs = 0;
    gSwingDir = 0;
    gXStopped = false;
    gZAtTop = false;
    printf("[Spray] Arrived, scan down\n");
    Z_Down();
}

static void Finish_Current_Tree(uint32_t now)
{
    Emm_V5_Stop_Now(AXIS_Z, false);
    HAL_Delay(10);
    Approach_Stop();
    Pump_Water(false); Pump_Spray(false);
    Pump_Chem1(false); Pump_Chem2(false); Pump_Chem3(false);
    Y_Retract();
    Z_Up();
    gZAtTop = true;
    if (gYRetractTime < Z_RISE_TIME) gYRetractTime = Z_RISE_TIME;
    gState = SPRAY_DONE;
    gPhaseMs = now;
    gDoneStep = 1;
    gYApproachActive = false;
    printf("[Spray] Tree done, retracting\n");
}

static void Execute_Approach_Command(const CameraCommand *command, uint32_t now)
{
    if (command->flag == CAMERA_FLAG_ARRIVED ||
        command->flag == CAMERA_FLAG_TOO_CLOSE) {
        Start_Down_Scan(now);
        return;
    }
    if (command->flag == CAMERA_FLAG_LOST) {
        Approach_Stop();
        printf("[Spray] Target lost\n");
        return;
    }
    if (command->flag != CAMERA_FLAG_MOVE) return;
    if (command->x_dist > 0) StartXAlignment(command);
    else { Emm_V5_Stop_Now(AXIS_X, false); HAL_Delay(10); }
    if (command->y_dir == 1 && (command->y_dist > 0 || command->x_dist == 0))
        Y_StartApproach(now);
}

static void Buffer_Prepare_Command(const CameraCommand *command)
{
    if (command->flag == CAMERA_FLAG_ARRIVED ||
        command->flag == CAMERA_FLAG_TOO_CLOSE) {
        return;  /* Z上升期间ARRIVED无意义，Y还没伸出 */
    }
    if (command->flag == CAMERA_FLAG_LOST) { gPreparedCommandValid = false; return; }
    if (command->flag != CAMERA_FLAG_MOVE) return;
    /* Z 上升期间先缓存第一条靠近指令，等进入摄像头控制阶段后立刻执行。 */
    if (!gPreparedCommandValid || gPreparedCommand.flag != 0) {
        gPreparedCommand = *command;
        gPreparedCommand.flag = 0;
        gPreparedCommandValid = true;
        return;
    }
    /* 后续只补充 Y 前进信号，避免把更早看到的横向纠偏覆盖掉。 */
    if ((gPreparedCommand.x_dist == 0) && (command->x_dist > 0)) {
        gPreparedCommand.x_dir = command->x_dir;
        gPreparedCommand.x_dist = command->x_dist;
    }
    if (command->y_dir == 1) {
        gPreparedCommand.y_dir = 1;
        gPreparedCommand.y_dist = command->y_dist;
    }
}

/* ── NFC卡号 → 配药比例 水:药1:药2 (PUMP1:PUMP3:PUMP4) ── */
volatile int gNfcCardNum = 0;
static uint32_t gWaterTime  = 0;
static uint32_t gChem1Time  = 0;
static uint32_t gChem2Time  = 0;
static uint32_t gChem3Time  = 0;
static uint32_t gChem4Time  = 0;

static void Spray_SetRecipe(int card) {
    /* 每张卡: 各泵独立定时(ms), 自己改 */
    switch (card) {
        case 1: gWaterTime=4000; gChem1Time=0; gChem2Time=3500; gChem3Time=0;    gChem4Time=0;    break;
        case 2: gWaterTime=4000; gChem1Time=3500; gChem2Time=0;    gChem3Time=0;    gChem4Time=0;    break;
        case 3: gWaterTime=4000; gChem1Time=3000; gChem2Time=5000; gChem3Time=0; gChem4Time=0;    break;
        case 4: gWaterTime=4000; gChem1Time=0; gChem2Time=0;    gChem3Time=3500;    gChem4Time=0;    break;
        case 5: gWaterTime=4000; gChem1Time=0; gChem2Time=0; gChem3Time=0;    gChem4Time=4000;    break;
        default: gWaterTime=0;    gChem1Time=0;    gChem2Time=0;    gChem3Time=0;    gChem4Time=0;    break;
    }
}

/* ── 泵 ── */
void Pump_Water(bool on) { HAL_GPIO_WritePin(PUMP1_GPIO_Port, PUMP1_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET); }
void Pump_Spray(bool on) { HAL_GPIO_WritePin(PUMP2_GPIO_Port, PUMP2_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET); }
void Pump_Chem1(bool on) { HAL_GPIO_WritePin(PUMP3_GPIO_Port, PUMP3_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET); }
void Pump_Chem2(bool on) { HAL_GPIO_WritePin(PUMP4_GPIO_Port, PUMP4_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET); }
void Pump_Chem3(bool on) { HAL_GPIO_WritePin(PUMP5_GPIO_Port, PUMP5_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET); }
void Pump_Chem4(bool on) { HAL_GPIO_WritePin(PUMP6_GPIO_Port, PUMP6_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET); }


static uint32_t MixTime(void) {
    uint32_t t = gWaterTime;
    if (gChem1Time > t) t = gChem1Time;
    if (gChem2Time > t) t = gChem2Time;
    if (gChem3Time > t) t = gChem3Time;
    if (gChem4Time > t) t = gChem4Time;
    return t;
}

/* ================================================================ */

void Spray_Init(void)
{
    Pump_Water(false); Pump_Spray(false);
    Pump_Chem1(false); Pump_Chem2(false); Pump_Chem3(false); Pump_Chem4(false);
    gYApproachActive = false;
    gYStepRunning = false;
    gYLastDriverCommandMs = 0;
    gYDriverCommandCount = 0;
    gYCommandedPulses = 0;
    gYRetractPulses = 0;
    gYRetractTime = 0;
    printf("  Spray init OK\n");
}

void Spray_PumpTest(void)
{
    for (int card = 1; card <= 5; card++) {
        Spray_SetRecipe(card);
        uint32_t swingMs = Z_UP_SWINGS * Z_UP_MS;
        uint32_t sprayMs = (Z_UP_TIME > swingMs ? Z_UP_TIME : swingMs) + 5500;
        printf("=== CARD %d: W=%d C1=%d C2=%d C3=%d C4=%d ===\n",
               card, (int)gWaterTime,
               (int)gChem1Time, (int)gChem2Time, (int)gChem3Time, (int)gChem4Time);

        /* 阶段1: 水 + 药泵 */
        if (gWaterTime>0) Pump_Water(true);
        if (gChem1Time>0) Pump_Chem1(true);
        if (gChem2Time>0) Pump_Chem2(true);
        if (gChem3Time>0) Pump_Chem3(true);
        if (gChem4Time>0) Pump_Chem4(true);

        uint32_t t = MixTime();
        if (t > 0) {
            uint32_t t0 = HAL_GetTick();
            while (HAL_GetTick()-t0 < t) {
                uint32_t e = HAL_GetTick()-t0;
                if (gWaterTime>0 && e>=gWaterTime) Pump_Water(false);
                if (gChem1Time>0 && e>=gChem1Time) Pump_Chem1(false);
                if (gChem2Time>0 && e>=gChem2Time) Pump_Chem2(false);
                if (gChem3Time>0 && e>=gChem3Time) Pump_Chem3(false);
                if (gChem4Time>0 && e>=gChem4Time) Pump_Chem4(false);
                HAL_Delay(50);
            }
        }
        Pump_Water(false);
        Pump_Chem1(false); Pump_Chem2(false); Pump_Chem3(false); Pump_Chem4(false);
        printf("[MIX] done\n");

        /* 阶段2: 喷药 */
        printf("[SPRAY] %lums\n", (unsigned long)sprayMs);
        Pump_Spray(true);
        HAL_Delay(sprayMs);
        Pump_Spray(false);
        printf("[SPRAY] done\n");

        printf("[DONE] Card %d, wait 10s...\n", card);
        HAL_Delay(10000);
    }
    printf("=== ALL DONE ===\n");
}

/* PS2 按钮控制水泵测试:
   CROSS=水泵  CIRCLE=喷泵  SQUARE=药1  TRIANGLE=药2  R1=药3  L1=药4  */
void Spray_PS2PumpTest(void)
{
    static bool p1, p2, p3, p4, p5, p6;
    int key = PS2_LastRelease;
    if (key) PS2_LastRelease = 0;  /* 消费掉, PS2_ModeHandler已注释 */
    switch (key) {
    case PSB_CROSS:    p1 = !p1; Pump_Water(p1);
        printf("[PS2] PUMP1 Water %s\n", p1 ? "ON" : "OFF"); break;
    case PSB_CIRCLE:   p2 = !p2; Pump_Spray(p2);
        printf("[PS2] PUMP2 Spray %s\n", p2 ? "ON" : "OFF"); break;
    case PSB_SQUARE:   p3 = !p3; Pump_Chem1(p3);
        printf("[PS2] PUMP3 Chem1 %s\n", p3 ? "ON" : "OFF"); break;
    case PSB_TRIANGLE: p4 = !p4; Pump_Chem2(p4);
        printf("[PS2] PUMP4 Chem2 %s\n", p4 ? "ON" : "OFF"); break;
    case PSB_R1:       p5 = !p5; Pump_Chem3(p5);
        printf("[PS2] PUMP5 Chem3 %s\n", p5 ? "ON" : "OFF"); break;
    case PSB_L1:       p6 = !p6; Pump_Chem4(p6);
        printf("[PS2] PUMP6 Chem4 %s\n", p6 ? "ON" : "OFF"); break;
    }
}

void Spray_SetNfcSide(int side) { gNfcSide = side; }

void Spray_Start(PestLevel level)
{
    if (gState != SPRAY_IDLE) return;
    if (level == PEST_NONE)   return;

    /* 根据NFC侧旋转喷药泵: 1=右边=方向0, 2=左边=方向1 */
    {
        int targetDir = (gNfcSide == 1) ? 1 : 0;  /* nfcTrig=1(左)→朝左, nfcTrig=2(右)→朝右 */
        if (targetDir != gXDirection) {
            printf("[Spray] X rotate %s\n", targetDir ? "LEFT" : "RIGHT");
            Emm_V5_Pos_Control(AXIS_X, 1-targetDir, 20, 20,
                               X_180_PULSES, false, false);
            HAL_Delay(4000);  /* 等电机转完: 13500脉冲@200RPM, 加加速度余量 */
            gXDirection = targetDir;
            /* 新方向设为零点, 喷药结束回零即为当前方向 */
            Emm_V5_Reset_CurPos_To_Zero(AXIS_X);
            HAL_Delay(10);
        }
    }
    Spray_SetRecipe(gNfcCardNum);
    gLevel    = level;
    Camera_ClearCommand();
    Emm_V5_Reset_CurPos_To_Zero(AXIS_Y);
    HAL_Delay(10);
    gYCommandedPulses = 0;
    gYRetractPulses = 0;
    gYRetractTime = 0;

    gYStepRunning = false;
    gYLastDriverCommandMs = 0;
    gYDriverCommandCount = 0;
    gYApproachActive = false;
    gZDescending = false;
    gZAtBottom = false;
    gMixingStarted = false;
    gMixStartMs = 0;
    gPreparedCommandValid = false;
    gState    = SPRAY_PREPARE;
    gPhaseMs  = HAL_GetTick();
    gSwingCnt = 0;
    gSwingMs  = 0;
    gSwingDir = 0;
    gXStopped = false;
    gSwingInitDone = false;
    _sent8 = false;
    printf("[Spray] Start, pest=%d\n", level);
}

bool Spray_IsBusy(void) { return gState != SPRAY_IDLE; }

void Spray_CameraStepperTestUpdate(void)
{
    CameraCommand command;
    uint32_t now = HAL_GetTick();
    Y_ApproachUpdate(now);
    if (!Camera_GetCommand(&command)) return;
    if (command.flag == CAMERA_FLAG_TASK_DONE) {
        Approach_Stop();
        gYRetractPulses = gYCommandedPulses;
        gYRetractTime = Y_MOVE_TIME(Y_APPROACH_TARGET_PULSES);
        Y_Retract();
        return;
    }
    if (command.flag == CAMERA_FLAG_ARRIVED ||
        command.flag == CAMERA_FLAG_LOST ||
        command.flag == CAMERA_FLAG_TOO_CLOSE) {
        Approach_Stop();
        return;
    }
    if (command.flag == CAMERA_FLAG_MOVE)
        Execute_Approach_Command(&command, now);
}

void Spray_Update(void)
{
    if (gState == SPRAY_IDLE) return;
    uint32_t now = HAL_GetTick();

    switch (gState) {

    case SPRAY_PREPARE:
        if (!gZAtTop && !gRisingZ && now - gPhaseMs > 500) {
            printf("[Spray] Z rising to scan height...\n");
            Z_Up();
            gRisingZ = true;
            gPhaseMs = now;
        }
        if ((gZAtTop && now - gPhaseMs > 500) ||
            (gRisingZ && now - gPhaseMs > Z_RISE_TIME)) {
            gZAtTop = true;
            gRisingZ = false;
            gState = SPRAY_SCAN;
            gPhaseMs = now;
            gZAtTop = false;
            gZDescending = false;
            Camera_ClearCommand();
            {
                uint8_t go[] = {0xAA,0xAA, 0x00,0x00, 0x00,0x00, 0x00,0x00, 0x05, 0xFF,0xFF};
                __disable_irq();
                HAL_UART_Transmit(&CAM_UART, go, sizeof(go), 100);
                __enable_irq();
            }
            printf("[Spray] Camera controls X & Y, waiting for approach...\n");
        }
        break;

    /* ── 摄像头引导: X对准 + Y前进, 到位后Z下降+X自摆 ── */
    case SPRAY_SCAN: {
        static bool _ret = false;
        CameraCommand cmd;
        uint32_t now2 = HAL_GetTick();

        /* 摄像头控制 X 对准 + Y 前进/停止 */
        if (Camera_GetCommand(&cmd)) {
            printf("[CAM] flag=%u xd=%u xdist=%u yd=%u ydist=%u\n",
                   cmd.flag, cmd.x_dir, cmd.x_dist, cmd.y_dir, cmd.y_dist);
            /* TASK_DONE 在下降/喷药时不处理, 等完成后再收尾 */
            if (cmd.flag == CAMERA_FLAG_ARRIVED || cmd.flag == CAMERA_FLAG_TOO_CLOSE) {
                Y_RecordInterruptedStep(now2);
                Emm_V5_Stop_Now(AXIS_Y, false);
                HAL_Delay(10);
                gYApproachActive = false;
                if (!gZDescending) {
                    Z_Down(); gZDescending = true; gPhaseMs = now2;
                    gSwingCnt = 0; gSwingMs = now2; gSwingDir = 0; gXStopped = false; gSwingInitDone = false; _ret = false;
                    Swing_Go(0, X_DOWN_SPEED, X_DOWN_ACC);
                }
                printf("[Spray] Y arrived, Z down + X swing\n");
                if (gNfcCardNum == 1) {
                    if (!_sent8) {
                        _sent8 = true;
                        uint8_t f8[] = {0xAA,0xAA, 0x00,0x00, 0x00,0x00, 0x00,0x00, 0x08, 0xFF,0xFF};
                        __disable_irq();
                        HAL_UART_Transmit(&CAM_UART, f8, sizeof(f8), 100);
                        __enable_irq();
                        printf("[Spray] Sent flag=8 to CAM\n");
                    }
                }
            } else if (cmd.flag == CAMERA_FLAG_LOST) {
                Emm_V5_Stop_Now(AXIS_X, false);
                HAL_Delay(10);
                Emm_V5_Stop_Now(AXIS_Y, false);
                HAL_Delay(10);
                gYApproachActive = false;
            } else if (cmd.flag == CAMERA_FLAG_MOVE) {
                if (cmd.x_dist >= X_ALIGN_DEADZONE) {
                    StartXAlignment(&cmd);
                } else {
                    Emm_V5_Stop_Now(AXIS_X, false); HAL_Delay(10);
                }
                if (cmd.y_dir == 1) Y_StartApproach(now2);
            }
        }
        Y_ApproachUpdate(now2);

        if (gNfcCardNum == 1 && !_sent8 && gYApproachActive && !gYStepRunning) {
            _sent8 = true;
            uint8_t f8[] = {0xAA,0xAA, 0x00,0x00, 0x00,0x00, 0x00,0x00, 0x08, 0xFF,0xFF};
            __disable_irq();
            HAL_UART_Transmit(&CAM_UART, f8, sizeof(f8), 100);
            __enable_irq();
            printf("[Spray] Y 12cm done, sent flag=8\n");
        }

        if (!gZDescending) break;  /* Y还没到位 */

        /* X 自动来回摆动: 首段半程偏移 → 全程交替 → 末段半程归中 */
        if (!gXStopped) {
            uint32_t seg_ms;
            if (!gSwingInitDone)
                seg_ms = Z_DOWN_MS / 2;
            else if (_ret)
                seg_ms = Z_DOWN_MS / 2;  /* 归中半程 */
            else
                seg_ms = Z_DOWN_MS;

            if (now - gSwingMs > seg_ms) {
                gSwingMs = now;
                if (!gSwingInitDone) {
                    gSwingInitDone = true;
                    gSwingDir = 1;  /* 半程走完, 换向 */
                    Swing_Go(gSwingDir, X_DOWN_SPEED, X_DOWN_ACC);
                } else if (_ret) {
                    /* 归中半程走完, 停止 */
                    Emm_V5_Vel_Control(AXIS_X, 0, 0, X_DOWN_ACC, false);
                    HAL_Delay(10);
                    gXStopped = true; _ret = false;
                    printf("[Spray] X stop at center\n");
                } else {
                    gSwingDir = !gSwingDir;
                    gSwingCnt++;
                    if (gSwingCnt >= Z_DOWN_SWINGS) {
                        _ret = true;
                        /* 用刚换的方向走半程归中 */
                    }
                    Swing_Go(gSwingDir, X_DOWN_SPEED, X_DOWN_ACC);
                }
            }
        }

        /* Z 走到底 → 配药 */
        #define Z_SCAN_TIME ((Z_DOWN_TIME) > (Z_DOWN_SWINGS * Z_DOWN_MS) ? (Z_DOWN_TIME) : (Z_DOWN_SWINGS * Z_DOWN_MS))
        if (gZAtBottom || now - gPhaseMs > Z_SCAN_TIME + 500) {
            if (!gZAtBottom) {
                Emm_V5_Stop_Now(AXIS_Z, false);
                Emm_V5_Vel_Control(AXIS_X, 0, 0, X_DOWN_ACC, false);
                HAL_Delay(100);
                gZAtBottom = true;
                gPhaseMs = now;  /* 从此刻开始配药计时 */
                printf("[Spray] Z at bottom\n");
                {
                    uint8_t done[] = {0xAA,0xAA, 0x00,0x00, 0x00,0x00, 0x00,0x00, 0x06, 0xFF,0xFF};
                    __disable_irq();
                    HAL_UART_Transmit(&CAM_UART, done, sizeof(done), 100);
                    __enable_irq();
                }
                /* 到底即刻启动配药 */
                if (MixTime() > 0) {
                    printf("[Spray] Mixing W:%d C1:%d C2:%d C3:%d C4:%d\n",
                           (int)gWaterTime, (int)gChem1Time, (int)gChem2Time, (int)gChem3Time, (int)gChem4Time);
                    if (gWaterTime > 0) Pump_Water(true);
                    if (gChem1Time > 0) Pump_Chem1(true);
                    if (gChem2Time > 0) Pump_Chem2(true);
                    if (gChem3Time > 0) Pump_Chem3(true);
                    if (gChem4Time > 0) Pump_Chem4(true);
                }
            }

            /* 配药计时及关泵 */
            {
                uint32_t elap = now - gPhaseMs;
                if (gWaterTime > 0 && elap >= gWaterTime) Pump_Water(false);
                if (gChem1Time > 0 && elap >= gChem1Time) Pump_Chem1(false);
                if (gChem2Time > 0 && elap >= gChem2Time) Pump_Chem2(false);
                if (gChem3Time > 0 && elap >= gChem3Time) Pump_Chem3(false);
                if (gChem4Time > 0 && elap >= gChem4Time) Pump_Chem4(false);
                if (elap < MixTime()) break;  /* 配药未完成，继续等 */
            }
            Pump_Water(false); Pump_Chem1(false); Pump_Chem2(false); Pump_Chem3(false); Pump_Chem4(false);

            /* 配药完成 → 喷药或结束 */
            if (MixTime() == 0) {
                Y_Retract();
                gState = SPRAY_DONE; gPhaseMs = now;
                Z_Up(); gZAtTop = true;
            } else {
                gState    = SPRAY_SPRAYING;
                gPhaseMs  = now;
                gSwingCnt = 0;
                gSwingMs  = now;
                gSwingDir = 0;
                gXStopped = false;
                Swing_Go(0, X_UP_SPEED, X_UP_ACC);
                printf("[Spray] Z up, spraying...\n");
                Pump_Spray(true);
                Emm_V5_Pos_Control(AXIS_Y, 1, Y_ARM_SPEED, Y_ARM_ACC, 3000, false, false);
                HAL_Delay(10);
                Z_Up(); gZAtTop = true;
            }
            gMixingStarted = false;
            gZAtBottom = false;
        }
        break; }

    case SPRAY_MIX: {
        uint32_t elap = now - gPhaseMs;
        if (gWaterTime > 0 && elap >= gWaterTime) Pump_Water(false);
        if (gChem1Time > 0 && elap >= gChem1Time) Pump_Chem1(false);
        if (gChem2Time > 0 && elap >= gChem2Time) Pump_Chem2(false);
        if (gChem3Time > 0 && elap >= gChem3Time) Pump_Chem3(false);
        if (gChem4Time > 0 && elap >= gChem4Time) Pump_Chem4(false);
        if (elap >= MixTime()) {
            Pump_Water(false); Pump_Chem1(false); Pump_Chem2(false); Pump_Chem3(false); Pump_Chem4(false);
            gState    = SPRAY_SPRAYING;
            gPhaseMs  = now;
            gSwingCnt = 0;
            gSwingMs  = now;
            gSwingDir = 0;
            gXStopped = false;
            Swing_Go(0, X_UP_SPEED, X_UP_ACC);
            printf("[Spray] Z up, spraying...\n");
            Pump_Spray(true);
            Emm_V5_Pos_Control(AXIS_Y, 1, Y_ARM_SPEED, Y_ARM_ACC, 3000, false, false);
            HAL_Delay(10);
            Z_Up(); gZAtTop = true;
        }
        break;
    }

    case SPRAY_SPRAYING: {
        if (!gXStopped) {
            static bool _upInit = false, _upRet = false;
            uint32_t seg;
            if (!_upInit)       seg = Z_UP_MS / 2;
            else if (_upRet)    seg = Z_UP_MS / 2;
            else                seg = Z_UP_MS;

            if (now - gSwingMs > seg) {
                gSwingMs = now;
                if (!_upInit) {
                    _upInit = true;
                    gSwingDir = 0;
                }
                if (_upRet) {
                    Emm_V5_Vel_Control(AXIS_X, 0, 0, X_UP_ACC, false);
                    HAL_Delay(10);
                    gXStopped = true; _upInit = false; _upRet = false;
                    printf("[Spray] X stop at center\n");
                } else {
                    gSwingDir = !gSwingDir;
                    gSwingCnt++;
                    if (gSwingCnt >= Z_UP_SWINGS) {
                        _upRet = true;
                    }
                }
                if (!gXStopped) {
                    Swing_Go(gSwingDir, X_UP_SPEED, X_UP_ACC);
                    printf("[Spray] X swing #%d dir=%d\n", gSwingCnt, gSwingDir);
                }
            }
        }
        #define Z_SPRAY_TIME ((Z_UP_TIME) > (Z_UP_SWINGS * Z_UP_MS) ? (Z_UP_TIME) : (Z_UP_SWINGS * Z_UP_MS))
        uint32_t _extra = (gNfcCardNum == 1) ? 3000 : 0;
        if (now - gPhaseMs > Z_SPRAY_TIME + 5500 + _extra) {
            Pump_Spray(false);
            Emm_V5_Vel_Control(AXIS_X, 0, 0, X_UP_ACC, false);
            HAL_Delay(10);
            gState   = SPRAY_DONE;
            gPhaseMs = now;
            printf("[Spray] Pause 2s...\n");
        }
        break; }

    case SPRAY_DONE:
        if (gDoneStep == 0) {
            if (now - gPhaseMs > 2000) {
                Y_Retract();
                gPhaseMs = now;
                gDoneStep = 1;
                printf("[Spray] Y retracting...\n");
            }
        } else {
            if (now - gPhaseMs > gYRetractTime + 700) {
                Emm_V5_Vel_Control(AXIS_X, 0, 0, 20, false);
                HAL_Delay(10);
                Emm_V5_Pos_Control(AXIS_X, 0, 200, 20, 0, true, false);
                HAL_Delay(10);
                gState     = SPRAY_IDLE;
                gPhaseMs   = 0;
                gPestLevel = PEST_NONE;
                gDoneStep  = 0;
                Emm_V5_Reset_CurPos_To_Zero(AXIS_Y);
                HAL_Delay(10);
                printf("[Spray] Done\n");
            }
        }
        break;

    case SPRAY_IDLE:
    default:
        break;
    }
}
