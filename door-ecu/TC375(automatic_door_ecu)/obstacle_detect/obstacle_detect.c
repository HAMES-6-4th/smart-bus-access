#include "obstacle_detect.h"

#include "Ifx_Types.h"
#include "IfxScuCcu.h"
#include "Cpu/Irq/IfxCpu_Irq.h"
#include "Port/Std/IfxPort.h"
#include "IfxPort_PinMap.h"
#include "Scu/Std/IfxScuEru.h"
#include "Src/Std/IfxSrc.h"
#include "Stm/Std/IfxStm.h"

#define ULTRASONIC_TRIG_PIN                     IfxPort_P10_4
#define ULTRASONIC_ECHO_PIN                     IfxPort_P02_0
#define ULTRASONIC_ECHO_REQ_PIN                 IfxScu_REQ3C_P02_0_IN

#define ULTRASONIC_ECHO_INPUT_CHANNEL           IfxScuEru_InputChannel_3
#define ULTRASONIC_ECHO_TRIGGER_NODE            IfxScuEru_InputNodePointer_3
#define ULTRASONIC_ECHO_OUTPUT_CHANNEL          IfxScuEru_OutputChannel_3

#define OBSTACLE_DEBUG_LED_PIN                  IfxPort_P00_5

/*
 * 기존 UART priority:
 *   BLE   : 40 / 41 / 42
 *   slope : 43 / 44 / 45
 *
 * obstacle는 처음에는 UART보다 낮게 시작.
 */
#define OBSTACLE_STM_TRIG_ISR_PRIORITY          (31u)
#define OBSTACLE_ECHO_ISR_PRIORITY              (32u)

#define OBSTACLE_STM_COMPARATOR                 IfxStm_Comparator_1

#define OBSTACLE_THRESHOLD_CM                   (30.0f)
#define OBSTACLE_MEASURE_PERIOD_MS              (100u)
#define OBSTACLE_ECHO_TIMEOUT_US                (30000u)
#define OBSTACLE_TRIG_PULSE_US                  (10u)

typedef enum ObstacleDetectStateIdEnum
{
    OBSTACLE_STATE_IDLE = 0,
    OBSTACLE_STATE_TRIG_HIGH,
    OBSTACLE_STATE_WAIT_RISE,
    OBSTACLE_STATE_WAIT_FALL
} ObstacleDetectStateId;

typedef struct ObstacleDetectStateStruct
{
    volatile uint8_t initialized;

    volatile uint8_t detected;
    volatile uint8_t valid;
    volatile float last_distance_cm;

    volatile ObstacleDetectStateId state;

    volatile uint32_t last_kick_tick;
    volatile uint32_t measure_start_tick;
    volatile uint32_t rise_tick;
    volatile uint32_t pulse_ticks;
    volatile uint8_t pulse_ready;
} ObstacleDetectState;

static ObstacleDetectState g_obstacleDetectState;

static uint32_t g_obstacleTicksPerUs = 100u;
static uint32_t g_obstacleMeasurePeriodTicks = 10000000u;
static uint32_t g_obstacleTrigPulseTicks = 1000u;
static uint32_t g_obstacleEchoTimeoutTicks = 3000000u;

IFX_INTERRUPT(obstacleDetectTrigIsr, 0, OBSTACLE_STM_TRIG_ISR_PRIORITY);
IFX_INTERRUPT(obstacleDetectEchoIsr, 0, OBSTACLE_ECHO_ISR_PRIORITY);

static uint32_t obstacleDetectGetNowTick(void)
{
    return (uint32_t)IfxStm_getLower(&MODULE_STM0);
}

static void obstacleDetectSetDetected(uint8_t detected)
{
    g_obstacleDetectState.detected = detected;

    if (detected != 0u)
    {
        IfxPort_setPinHigh(OBSTACLE_DEBUG_LED_PIN.port, OBSTACLE_DEBUG_LED_PIN.pinIndex);
    }
    else
    {
        IfxPort_setPinLow(OBSTACLE_DEBUG_LED_PIN.port, OBSTACLE_DEBUG_LED_PIN.pinIndex);
    }
}

static void obstacleDetectInitPins(void)
{
    IfxPort_setPinModeOutput(
        ULTRASONIC_TRIG_PIN.port,
        ULTRASONIC_TRIG_PIN.pinIndex,
        IfxPort_OutputMode_pushPull,
        IfxPort_PadDriver_cmosAutomotiveSpeed1
    );

    IfxPort_setPinLow(ULTRASONIC_TRIG_PIN.port, ULTRASONIC_TRIG_PIN.pinIndex);

    IfxPort_setPinModeInput(
        ULTRASONIC_ECHO_PIN.port,
        ULTRASONIC_ECHO_PIN.pinIndex,
        IfxPort_InputMode_pullDown
    );

    IfxPort_setPinModeOutput(
        OBSTACLE_DEBUG_LED_PIN.port,
        OBSTACLE_DEBUG_LED_PIN.pinIndex,
        IfxPort_OutputMode_pushPull,
        IfxPort_OutputIdx_general
    );

    IfxPort_setPinLow(OBSTACLE_DEBUG_LED_PIN.port, OBSTACLE_DEBUG_LED_PIN.pinIndex);
}

static void obstacleDetectInitStmCompare(void)
{
    IfxStm_CompareConfig stm_config;

    IfxStm_initCompareConfig(&stm_config);

    stm_config.comparator = OBSTACLE_STM_COMPARATOR;
    stm_config.triggerPriority = OBSTACLE_STM_TRIG_ISR_PRIORITY;
    stm_config.typeOfService = IfxSrc_Tos_cpu0;
    stm_config.ticks = obstacleDetectGetNowTick() + g_obstacleMeasurePeriodTicks;

    IfxStm_initCompare(&MODULE_STM0, &stm_config);
    IfxStm_clearCompareFlag(&MODULE_STM0, OBSTACLE_STM_COMPARATOR);
}

/*
 * iLLD 버전에 따라 enum 이름이나 gating pattern enum 이름이 약간 다를 수 있다.
 * 컴파일 에러가 나면 아래 매크로/enum 이름만 네 iLLD에 맞게 바꾸면 된다.
 */
static void obstacleDetectInitEchoInterrupt(void)
{
    volatile Ifx_SRC_SRCR *src;

    IfxScuEru_initReqPin(&ULTRASONIC_ECHO_REQ_PIN, IfxPort_InputMode_pullDown);

    IfxScuEru_disableAutoClear(ULTRASONIC_ECHO_INPUT_CHANNEL);
    IfxScuEru_enableRisingEdgeDetection(ULTRASONIC_ECHO_INPUT_CHANNEL);
    IfxScuEru_enableFallingEdgeDetection(ULTRASONIC_ECHO_INPUT_CHANNEL);

    IfxScuEru_enableTriggerPulse(ULTRASONIC_ECHO_INPUT_CHANNEL);
    IfxScuEru_connectTrigger(
        ULTRASONIC_ECHO_INPUT_CHANNEL,
        ULTRASONIC_ECHO_TRIGGER_NODE
    );

    IfxScuEru_setFlagPatternDetection(
        ULTRASONIC_ECHO_OUTPUT_CHANNEL,
        ULTRASONIC_ECHO_INPUT_CHANNEL,
        TRUE
    );
    IfxScuEru_enablePatternDetectionTrigger(ULTRASONIC_ECHO_OUTPUT_CHANNEL);
    IfxScuEru_setInterruptGatingPattern(
        ULTRASONIC_ECHO_OUTPUT_CHANNEL,
        IfxScuEru_InterruptGatingPattern_alwaysActive
    );

    src = &MODULE_SRC.SCU.SCUERU[(int)ULTRASONIC_ECHO_OUTPUT_CHANNEL % 4];
    IfxSrc_init(src, IfxSrc_Tos_cpu0, OBSTACLE_ECHO_ISR_PRIORITY);
    IfxSrc_enable(src);
}

static void obstacleDetectArmTrigCompare(uint32_t compare_tick)
{
    MODULE_STM0.CMP[OBSTACLE_STM_COMPARATOR].U = compare_tick;
    IfxStm_clearCompareFlag(&MODULE_STM0, OBSTACLE_STM_COMPARATOR);
}

static void obstacleDetectProcessPulse(void)
{
    uint32_t pulse_ticks;
    float pulse_us;
    float distance_cm;

    pulse_ticks = g_obstacleDetectState.pulse_ticks;
    g_obstacleDetectState.pulse_ready = 0u;

    pulse_us = (float)pulse_ticks / (float)g_obstacleTicksPerUs;
    distance_cm = pulse_us / 58.0f;

    g_obstacleDetectState.last_distance_cm = distance_cm;
    g_obstacleDetectState.valid = 1u;

    if (distance_cm <= OBSTACLE_THRESHOLD_CM)
    {
        obstacleDetectSetDetected(1u);
    }
    else
    {
        obstacleDetectSetDetected(0u);
    }

    g_obstacleDetectState.state = OBSTACLE_STATE_IDLE;
}

void obstacleDetectTrigIsr(void)
{
    IfxStm_clearCompareFlag(&MODULE_STM0, OBSTACLE_STM_COMPARATOR);

    IfxPort_setPinLow(ULTRASONIC_TRIG_PIN.port, ULTRASONIC_TRIG_PIN.pinIndex);

    if (g_obstacleDetectState.state == OBSTACLE_STATE_TRIG_HIGH)
    {
        g_obstacleDetectState.state = OBSTACLE_STATE_WAIT_RISE;
    }
}

void obstacleDetectEchoIsr(void)
{
    uint32_t now_tick;

    now_tick = obstacleDetectGetNowTick();

    if (IfxPort_getPinState(ULTRASONIC_ECHO_PIN.port, ULTRASONIC_ECHO_PIN.pinIndex) != 0u)
    {
        if (g_obstacleDetectState.state == OBSTACLE_STATE_WAIT_RISE)
        {
            g_obstacleDetectState.rise_tick = now_tick;
            g_obstacleDetectState.state = OBSTACLE_STATE_WAIT_FALL;
        }
    }
    else
    {
        if (g_obstacleDetectState.state == OBSTACLE_STATE_WAIT_FALL)
        {
            g_obstacleDetectState.pulse_ticks = now_tick - g_obstacleDetectState.rise_tick;
            g_obstacleDetectState.pulse_ready = 1u;
        }
    }
}

void ObstacleDetect_Init(void)
{
    uint32_t stm_frequency;

    stm_frequency = (uint32_t)IfxScuCcu_getStmFrequency();
    if (stm_frequency == 0u)
    {
        stm_frequency = 100000000u;
    }

    g_obstacleTicksPerUs = stm_frequency / 1000000u;
    if (g_obstacleTicksPerUs == 0u)
    {
        g_obstacleTicksPerUs = 100u;
    }

    g_obstacleMeasurePeriodTicks = g_obstacleTicksPerUs * 1000u * OBSTACLE_MEASURE_PERIOD_MS;
    g_obstacleTrigPulseTicks = g_obstacleTicksPerUs * OBSTACLE_TRIG_PULSE_US;
    g_obstacleEchoTimeoutTicks = g_obstacleTicksPerUs * OBSTACLE_ECHO_TIMEOUT_US;

    g_obstacleDetectState.initialized = 1u;
    g_obstacleDetectState.detected = 0u;
    g_obstacleDetectState.valid = 0u;
    g_obstacleDetectState.last_distance_cm = 0.0f;
    g_obstacleDetectState.state = OBSTACLE_STATE_IDLE;
    g_obstacleDetectState.last_kick_tick = obstacleDetectGetNowTick();
    g_obstacleDetectState.measure_start_tick = 0u;
    g_obstacleDetectState.rise_tick = 0u;
    g_obstacleDetectState.pulse_ticks = 0u;
    g_obstacleDetectState.pulse_ready = 0u;

    obstacleDetectInitPins();
    obstacleDetectInitStmCompare();
    obstacleDetectInitEchoInterrupt();
}

void ObstacleDetect_Service(void)
{
    uint32_t now_tick;

    if (g_obstacleDetectState.initialized == 0u)
    {
        return;
    }

    now_tick = obstacleDetectGetNowTick();

    if (g_obstacleDetectState.pulse_ready != 0u)
    {
        obstacleDetectProcessPulse();
    }

    if ((g_obstacleDetectState.state == OBSTACLE_STATE_TRIG_HIGH) ||
        (g_obstacleDetectState.state == OBSTACLE_STATE_WAIT_RISE) ||
        (g_obstacleDetectState.state == OBSTACLE_STATE_WAIT_FALL))
    {
        if ((uint32_t)(now_tick - g_obstacleDetectState.measure_start_tick) > g_obstacleEchoTimeoutTicks)
        {
            IfxPort_setPinLow(ULTRASONIC_TRIG_PIN.port, ULTRASONIC_TRIG_PIN.pinIndex);

            g_obstacleDetectState.valid = 0u;
            obstacleDetectSetDetected(0u);
            g_obstacleDetectState.pulse_ready = 0u;
            g_obstacleDetectState.state = OBSTACLE_STATE_IDLE;
        }
    }

    if (g_obstacleDetectState.state != OBSTACLE_STATE_IDLE)
    {
        return;
    }

    if ((uint32_t)(now_tick - g_obstacleDetectState.last_kick_tick) < g_obstacleMeasurePeriodTicks)
    {
        return;
    }

    g_obstacleDetectState.last_kick_tick = now_tick;
    g_obstacleDetectState.measure_start_tick = now_tick;
    g_obstacleDetectState.rise_tick = 0u;
    g_obstacleDetectState.pulse_ticks = 0u;
    g_obstacleDetectState.pulse_ready = 0u;
    g_obstacleDetectState.state = OBSTACLE_STATE_TRIG_HIGH;

    IfxPort_setPinHigh(ULTRASONIC_TRIG_PIN.port, ULTRASONIC_TRIG_PIN.pinIndex);
    obstacleDetectArmTrigCompare(now_tick + g_obstacleTrigPulseTicks);
}

uint8_t ObstacleDetect_IsDetected(void)
{
    return g_obstacleDetectState.detected;
}

uint8_t ObstacleDetect_IsValid(void)
{
    return g_obstacleDetectState.valid;
}

float ObstacleDetect_GetLastDistanceCm(void)
{
    return g_obstacleDetectState.last_distance_cm;
}
