#include "door_ecu_control.h"

#include "ble_uart_tc375_lite.h"
#include "slope_uart.h"
#include "slope_uart_tc375_lite.h"
#include "obstacle_detect.h"
#include "door_ecu_can.h"
#include "driver_with_door_protocol.h"
#include "IfxStm.h"
#include "ble_uart.h"
#include "Door_Control.h"

#define DOOR_ECU_STATUS_CAN_PERIOD_MS   ((uint32_t)100u)

static DoorECUCAN g_doorCan;
static uint8_t g_doorCanOpened = 0u;
static uint32_t g_doorEcuLastStatusTxMs = 0u;

/**
 * @brief 
 * GPIO를 통해 도어를 열도록 제어하는 내부 함수입니다.
 * 구현이 완료된 문 열기 코드를 여기에 작성하면 됩니다.
 */
static void doorEcuApplyDoorGpioOpen(void);

/**
 * @brief 
 * GPIO를 통해 도어를 닫도록 제어하는 내부 함수입니다.
 * 구현이 완료된 문 닫기 코드를 여기에 작성하면 됩니다.
 */
static void doorEcuApplyDoorGpioClose(void);
static void doorEcuControlPollStatusCan(void);
static void doorEcuControlPollBleCommand(void);
static uint32_t doorEcuGetNowMs(void);
static DoorEcuRequest doorEcuMapDoorCommand(uint8_t door_cmd);
static DoorEcuRequest doorEcuMapRampCommand(uint8_t ramp_cmd);
static DoorEcuSlopeControlMode doorEcuMapRampMode(bool ramp_manual);
static uint8_t doorEcuReadDoorOpenedSensor(void);
static uint8_t doorEcuReadJamSensor(void);
static uint8_t doorEcuMapDoorStateForCan(void);
static void doorEcuControlPollCanCommand(void);
static void doorEcuControlRefreshStatusShadow(void);
static void doorEcuControlHandleJamDuringClosing(void);
static void doorEcuControlIssuePendingSlopeCommand(void);
static uint8_t doorEcuControlDoorIsOpenedOrOpening(uint8_t door_open_about_to_happen);
static uint8_t doorEcuControlDoorIsClosing(uint8_t door_close_about_to_happen);
static void doorEcuControlRunPolicy(void);
static void doorEcuControlPollStatusCan(void);

static void doorEcuControlPollBleCommand(void)
{
    uint8_t cmd;
    uint8_t slope_req;

    bleUartTc375Poll();

    while (bleUartTc375ConsumeCmd(&cmd) != 0u)
    {
        slope_req = bleUartCmdGetSlope(cmd);

        if (slope_req == BLE_UART_REQ_OPEN)
        {
            doorEcuControlOnBleSlopeAutoRequest(DOOR_ECU_REQUEST_OPEN);
        }
    }
}

static uint32_t doorEcuGetNowMs(void)
{
    uint64 ticks;
    uint64 freq_hz;

    ticks = IfxStm_get(&MODULE_STM0);
    freq_hz = (uint64)IfxStm_getFrequency(&MODULE_STM0);

    if (freq_hz == 0u)
    {
        return 0u;
    }

    return (uint32_t)((ticks * 1000u) / freq_hz);
}

static DoorEcuRequest doorEcuMapDoorCommand(uint8_t door_cmd)
{
    switch (door_cmd)
    {
    case DOOR_CMD_OPEN:
        return DOOR_ECU_REQUEST_OPEN;
    case DOOR_CMD_CLOSE:
        return DOOR_ECU_REQUEST_CLOSE;
    default:
        return DOOR_ECU_REQUEST_NONE;
    }
}

static DoorEcuRequest doorEcuMapRampCommand(uint8_t ramp_cmd)
{
    switch (ramp_cmd)
    {
    case RAMP_CMD_DEPLOY:
        return DOOR_ECU_REQUEST_OPEN;
    case RAMP_CMD_STOW:
        return DOOR_ECU_REQUEST_CLOSE;
    default:
        return DOOR_ECU_REQUEST_NONE;
    }
}

static DoorEcuSlopeControlMode doorEcuMapRampMode(bool ramp_manual)
{
    return ramp_manual ? DOOR_ECU_SLOPE_CONTROL_MANUAL
                       : DOOR_ECU_SLOPE_CONTROL_AUTO;
}

static void doorEcuControlPollCanCommand(void)
{
    bool updated = false;
    uint32_t now_ms;
    const DoorECUCANCommandSnapshot *cmd;
    DoorEcuRequest door_request;
    DoorEcuRequest slope_request;
    DoorEcuSlopeControlMode slope_mode;

    if (g_doorCanOpened == 0u)
    {
        return;
    }

    now_ms = doorEcuGetNowMs();

    if (doorECUCANPollCommand(&g_doorCan, now_ms, &updated) != CAN_STATUS_OK)
    {
        return;
    }

    if (!updated)
    {
        return;
    }

    cmd = doorECUCANGetCommand(&g_doorCan);
    if (cmd == 0)
    {
        return;
    }

    if (cmd->emergency_stop)
    {
        /* 필요하면 여기서 stop/fault 정책 추가 */
    }

    if (cmd->reset_fault)
    {
        /* 필요하면 여기서 reset 정책 추가 */
    }

    door_request = doorEcuMapDoorCommand(cmd->door_command);
    slope_request = doorEcuMapRampCommand(cmd->ramp_command);
    slope_mode = doorEcuMapRampMode(cmd->ramp_manual);

    doorEcuControlOnDriverCanCombinedRequest(
        door_request,
        slope_request,
        slope_mode
    );
}

typedef struct DoorEcuControlStateStruct
{
    uint8_t doorManualOpenPending;
    uint8_t doorManualClosePending;

    uint8_t slopeManualOpenPending;
    uint8_t slopeManualClosePending;

    uint8_t slopeAutoOpenStocked;
    uint8_t slopeAutoCloseStocked;

    uint8_t slopeTxPending;      /* SLOPE_UART_REQ_NONE / OPEN / CLOSE */
    uint8_t slopeOpenEstimated;  /* slope UART ACK 완료 기준 추정 상태 */

    uint32_t doorMotionStartMs;
    uint8_t  doorOpenEstimated;

    DoorEcuRequest lastDoorActuation;
    DoorEcuStatus status;
} DoorEcuControlState;

static DoorEcuControlState g_doorEcuControlState;

/* ------------------------------------------------------------------------- */
/* TODO HOOKS: 실제 하드웨어 / 기존 CAN 코드에 맞게 여기를 연결하면 됨 */
/* ------------------------------------------------------------------------- */

volatile uint32_t g_door_flag = 0;
volatile uint32_t g_slope_flag = 0;
static void doorEcuApplyDoorGpioOpen(void)
{
    driveDoorOpen();
    g_door_flag = 1;
}

static void doorEcuApplyDoorGpioClose(void)
{
    driveDoorClose();
    g_door_flag = 0;
}

static uint8_t doorEcuReadDoorOpenedSensor(void)
{
    /* TODO:
     * 전동문 상태 파악 센서(조도센서 등) 읽기
     * 열림이면 1, 아니면 0 반환
     */
    return 0u;
}

static uint8_t doorEcuReadJamSensor(void)
{
    return ObstacleDetect_IsDetected();
}

static uint8_t doorEcuMapDoorStateForCan(void)
{
    switch (g_doorEcuControlState.status.door_motion)
    {
    case DOOR_ECU_DOOR_MOTION_OPENED:
        return DOOR_STATE_OPENED;
    case DOOR_ECU_DOOR_MOTION_OPENING:
    case DOOR_ECU_DOOR_MOTION_CLOSING:
        return DOOR_STATE_MOVING;
    case DOOR_ECU_DOOR_MOTION_CLOSED:
        return DOOR_STATE_CLOSED;
    default:
        return DOOR_STATE_CLOSED;
    }
}

static uint8_t doorEcuMapRampStateForCan(void)
{
    if (g_doorEcuControlState.slopeTxPending != SLOPE_UART_REQ_NONE)
    {
        return RAMP_STATE_MOVING;
    }

    if (g_doorEcuControlState.slopeOpenEstimated != 0u)
    {
        return RAMP_STATE_DEPLOYED;
    }

    return RAMP_STATE_STOWED;
}

static void doorEcuCanSendStatus(uint8_t door_opened, uint8_t jam_detected, uint8_t slope_opened, uint8_t slope_opened_valid
)
{
    uint8_t door_state;
    uint8_t ramp_state;

    (void)door_opened;
    (void)slope_opened;
    (void)slope_opened_valid;

    if (g_doorCanOpened == 0u)
    {
        return;
    }

    door_state = doorEcuMapDoorStateForCan();
    ramp_state = doorEcuMapRampStateForCan();

    (void)doorECUCANPublishStatus(
        &g_doorCan,
        (jam_detected != 0u),
        door_state,
        ramp_state,
        false
    );
}

/* ------------------------------------------------------------------------- */

static void doorEcuControlRefreshStatusShadow(void)
{
    g_doorEcuControlState.status.slope_opened = g_doorEcuControlState.slopeOpenEstimated;
    g_doorEcuControlState.status.slope_opened_valid = 0u; /* 아직 slope 실센서 없음 */
    g_doorEcuControlState.status.slope_auto_open_stocked = g_doorEcuControlState.slopeAutoOpenStocked;
    g_doorEcuControlState.status.slope_auto_close_stocked = g_doorEcuControlState.slopeAutoCloseStocked;
    g_doorEcuControlState.status.slope_tx_pending = g_doorEcuControlState.slopeTxPending;
}

static void doorEcuControlResetState(void)
{
    g_doorEcuControlState.doorManualOpenPending = 0u;
    g_doorEcuControlState.doorManualClosePending = 0u;

    g_doorEcuControlState.slopeManualOpenPending = 0u;
    g_doorEcuControlState.slopeManualClosePending = 0u;

    g_doorEcuControlState.slopeAutoOpenStocked = 0u;
    g_doorEcuControlState.slopeAutoCloseStocked = 0u;

    g_doorEcuControlState.slopeTxPending = SLOPE_UART_REQ_NONE;
    g_doorEcuControlState.slopeOpenEstimated = 0u;

    g_doorEcuControlState.doorMotionStartMs = 0u;
    g_doorEcuControlState.doorOpenEstimated = 0u;
    g_doorEcuControlState.lastDoorActuation = DOOR_ECU_REQUEST_NONE;

    g_doorEcuControlState.status.door_opened = 0u;
    g_doorEcuControlState.status.jam_detected = 0u;
    g_doorEcuControlState.status.slope_opened = 0u;
    g_doorEcuControlState.status.slope_opened_valid = 0u;
    g_doorEcuControlState.status.door_motion = DOOR_ECU_DOOR_MOTION_UNKNOWN;
    g_doorEcuControlState.status.slope_auto_open_stocked = 0u;
    g_doorEcuControlState.status.slope_auto_close_stocked = 0u;
    g_doorEcuControlState.status.slope_tx_pending = SLOPE_UART_REQ_NONE;
}

static void doorEcuControlCancelOpenSideRequests(void)
{
    g_doorEcuControlState.doorManualOpenPending = 0u;
    g_doorEcuControlState.slopeManualOpenPending = 0u;
    g_doorEcuControlState.slopeAutoOpenStocked = 0u;

    if (g_doorEcuControlState.slopeTxPending == SLOPE_UART_REQ_OPEN)
    {
        g_doorEcuControlState.slopeTxPending = SLOPE_UART_REQ_NONE;
    }
}

static void doorEcuControlApplyDoorOpen(void)
{
    doorEcuApplyDoorGpioOpen();
    g_doorEcuControlState.lastDoorActuation = DOOR_ECU_REQUEST_OPEN;
    g_doorEcuControlState.doorMotionStartMs = doorEcuGetNowMs();
    g_doorEcuControlState.status.door_motion = DOOR_ECU_DOOR_MOTION_OPENING;
}

static void doorEcuControlApplyDoorClose(void)
{
    doorEcuApplyDoorGpioClose();
    g_doorEcuControlState.lastDoorActuation = DOOR_ECU_REQUEST_CLOSE;
    g_doorEcuControlState.doorMotionStartMs = doorEcuGetNowMs();
    g_doorEcuControlState.status.door_motion = DOOR_ECU_DOOR_MOTION_CLOSING;
}

static void doorEcuControlUpdateDoorEstimate(void)
{
    uint32_t now_ms = doorEcuGetNowMs();
    uint32_t elapsed = (uint32_t)(now_ms - g_doorEcuControlState.doorMotionStartMs);

    switch (g_doorEcuControlState.lastDoorActuation)
    {
    case DOOR_ECU_REQUEST_OPEN:
        if (elapsed < DOOR_MOVE_ASSUMED_TIME_MS)
        {
            g_doorEcuControlState.status.door_motion = DOOR_ECU_DOOR_MOTION_OPENING;
        }
        else
        {
            g_doorEcuControlState.doorOpenEstimated = 1u;
            g_doorEcuControlState.status.door_motion = DOOR_ECU_DOOR_MOTION_OPENED;
            g_doorEcuControlState.lastDoorActuation = DOOR_ECU_REQUEST_NONE;
        }
        break;

    case DOOR_ECU_REQUEST_CLOSE:
        if (elapsed < DOOR_MOVE_ASSUMED_TIME_MS)
        {
            g_doorEcuControlState.status.door_motion = DOOR_ECU_DOOR_MOTION_CLOSING;
        }
        else
        {
            g_doorEcuControlState.doorOpenEstimated = 0u;
            g_doorEcuControlState.status.door_motion = DOOR_ECU_DOOR_MOTION_CLOSED;
            g_doorEcuControlState.lastDoorActuation = DOOR_ECU_REQUEST_NONE;
        }
        break;

    default:
        g_doorEcuControlState.status.door_motion =
            (g_doorEcuControlState.doorOpenEstimated != 0u)
            ? DOOR_ECU_DOOR_MOTION_OPENED
            : DOOR_ECU_DOOR_MOTION_CLOSED;
        break;
    }

    g_doorEcuControlState.status.door_opened = g_doorEcuControlState.doorOpenEstimated;
}

static void doorEcuControlUpdateSensors(void)
{
    g_doorEcuControlState.status.jam_detected = doorEcuReadJamSensor();
    doorEcuControlUpdateDoorEstimate();
    doorEcuControlRefreshStatusShadow();
}

static void doorEcuControlHandleSlopeTxSuccess(void)
{
    uint8_t cmd;
    uint8_t slope_req;

    if (slopeUartTc375ConsumeTxSuccess(&cmd) == 0u)
    {
        return;
    }

    slope_req = slopeUartCmdGetSlope(cmd);

    if (slope_req == SLOPE_UART_REQ_OPEN)
    {
        g_doorEcuControlState.slopeOpenEstimated = 1u;
    }
    else if (slope_req == SLOPE_UART_REQ_CLOSE)
    {
        g_doorEcuControlState.slopeOpenEstimated = 0u;
    }

    if (((slope_req == SLOPE_UART_REQ_OPEN) &&
         (g_doorEcuControlState.slopeTxPending == SLOPE_UART_REQ_OPEN)) ||
        ((slope_req == SLOPE_UART_REQ_CLOSE) &&
         (g_doorEcuControlState.slopeTxPending == SLOPE_UART_REQ_CLOSE)))
    {
        g_doorEcuControlState.slopeTxPending = SLOPE_UART_REQ_NONE;
    }

    doorEcuControlRefreshStatusShadow();
}

static void doorEcuControlQueueSlopeOpenAuto(void)
{
    if (g_doorEcuControlState.slopeOpenEstimated == 0u)
    {
        g_doorEcuControlState.slopeTxPending = SLOPE_UART_REQ_OPEN;
    }
}

static void doorEcuControlQueueSlopeOpenManual(void)
{
    g_doorEcuControlState.slopeTxPending = SLOPE_UART_REQ_OPEN;
}

static void doorEcuControlQueueSlopeCloseForced(void)
{
    g_doorEcuControlState.slopeTxPending = SLOPE_UART_REQ_CLOSE;
}

static void doorEcuControlQueueSlopeCloseAuto(void)
{
    if ((g_doorEcuControlState.slopeOpenEstimated != 0u) ||
        (g_doorEcuControlState.slopeTxPending == SLOPE_UART_REQ_OPEN))
    {
        g_doorEcuControlState.slopeTxPending = SLOPE_UART_REQ_CLOSE;
    }
}

static void doorEcuControlCancelCloseSideRequests(void)
{
    g_doorEcuControlState.doorManualClosePending = 0u;
    g_doorEcuControlState.slopeManualClosePending = 0u;
    g_doorEcuControlState.slopeAutoCloseStocked = 0u;

    if (g_doorEcuControlState.slopeTxPending == SLOPE_UART_REQ_CLOSE)
    {
        g_doorEcuControlState.slopeTxPending = SLOPE_UART_REQ_NONE;
    }
}

static void doorEcuControlHandleJamDuringClosing(void)
{
    if (g_doorEcuControlState.status.jam_detected == 0u)
    {
        return;
    }

    if ((g_doorEcuControlState.status.door_motion != DOOR_ECU_DOOR_MOTION_CLOSING) &&
        (g_doorEcuControlState.doorManualClosePending == 0u))
    {
        return;
    }

    doorEcuControlCancelCloseSideRequests();
    doorEcuControlApplyDoorOpen();
}

static void doorEcuControlIssuePendingSlopeCommand(void)
{
    if (g_doorEcuControlState.slopeTxPending == SLOPE_UART_REQ_NONE)
    {
        return;
    }

    if (slopeUartTc375TxIsBusy() != 0u)
    {
        return;
    }

    if (g_doorEcuControlState.slopeTxPending == SLOPE_UART_REQ_OPEN)
    {
        slopeUartTc375RequestOpen();
    }
    else if (g_doorEcuControlState.slopeTxPending == SLOPE_UART_REQ_CLOSE)
    {
        slopeUartTc375RequestClose();
    }
}

static uint8_t doorEcuControlDoorIsOpenedOrOpening(uint8_t door_open_about_to_happen)
{
    return (uint8_t)(
        (g_doorEcuControlState.status.door_opened != 0u) ||
        (g_doorEcuControlState.status.door_motion == DOOR_ECU_DOOR_MOTION_OPENING) ||
        (g_doorEcuControlState.status.door_motion == DOOR_ECU_DOOR_MOTION_OPENED) ||
        (door_open_about_to_happen != 0u)
    );
}

static uint8_t doorEcuControlDoorIsClosing(uint8_t door_close_about_to_happen)
{
    return (uint8_t)(
        (g_doorEcuControlState.status.door_motion == DOOR_ECU_DOOR_MOTION_CLOSING) ||
        (door_close_about_to_happen != 0u)
    );
}

static void doorEcuControlRunPolicy(void)
{
    uint8_t door_open_about_to_happen = 0u;
    uint8_t door_close_about_to_happen = 0u;

    /* ------------------------------------------------------------------ */
    /* 1) door manual close 최우선                                        */
    /* door close 는 항상 slope close 를 동반한다.                       */
    /* ------------------------------------------------------------------ */
    if (g_doorEcuControlState.doorManualClosePending != 0u)
    {
        doorEcuControlApplyDoorClose();
        door_close_about_to_happen = 1u;
        g_doorEcuControlState.doorManualClosePending = 0u;

        doorEcuControlCancelOpenSideRequests();

        /* slope가 아직 안 닫혔거나, OPEN 이 pending/inflight 일 수 있으므로
         * close intention 은 stocked 로 유지한다.
         */
        g_doorEcuControlState.slopeAutoCloseStocked = 1u;
        doorEcuControlQueueSlopeCloseForced();
        return;
    }

    /* ------------------------------------------------------------------ */
    /* 2) slope manual close                                              */
    /* 문과 무관하게 강제로 닫는다.                                       */
    /* ------------------------------------------------------------------ */
    if (g_doorEcuControlState.slopeManualClosePending != 0u)
    {
        g_doorEcuControlState.slopeManualClosePending = 0u;
        g_doorEcuControlState.slopeAutoOpenStocked = 0u;
        g_doorEcuControlState.slopeManualOpenPending = 0u;

        doorEcuControlQueueSlopeCloseForced();
        return;
    }

    /* ------------------------------------------------------------------ */
    /* 3) slope manual open                                               */
    /* 문과 무관하게 강제로 연다.                                         */
    /* ------------------------------------------------------------------ */
    if (g_doorEcuControlState.slopeManualOpenPending != 0u)
    {
        g_doorEcuControlState.slopeManualOpenPending = 0u;
        g_doorEcuControlState.slopeAutoCloseStocked = 0u;
        g_doorEcuControlState.slopeManualClosePending = 0u;

        doorEcuControlQueueSlopeOpenManual();
        return;
    }

    /* ------------------------------------------------------------------ */
    /* 4) door manual open                                                */
    /* slope auto open stock 이 있다면 같은 tick 에 release 될 수 있다.  */
    /* ------------------------------------------------------------------ */
    if (g_doorEcuControlState.doorManualOpenPending != 0u)
    {
        doorEcuControlApplyDoorOpen();
        door_open_about_to_happen = 1u;
        g_doorEcuControlState.doorManualOpenPending = 0u;
    }

    /* ------------------------------------------------------------------ */
    /* 5) auto slope open                                                 */
    /* door 가 열려 있거나, 열리는 중이거나, 지금 열기로 했을 때만 release */
    /* ------------------------------------------------------------------ */
    if (g_doorEcuControlState.slopeAutoOpenStocked != 0u)
    {
        if (doorEcuControlDoorIsOpenedOrOpening(door_open_about_to_happen) != 0u)
        {
            g_doorEcuControlState.slopeAutoOpenStocked = 0u;
            doorEcuControlQueueSlopeOpenAuto();
            return;
        }
    }

    /* ------------------------------------------------------------------ */
    /* 6) auto slope close                                                */
    /* slope 단독 close 는 금지. door close 가 진행 중일 때만 release     */
    /* ------------------------------------------------------------------ */
    if (g_doorEcuControlState.slopeAutoCloseStocked != 0u)
    {
        uint8_t slope_open_or_opening = (uint8_t)(
            (g_doorEcuControlState.slopeOpenEstimated != 0u) ||
            (g_doorEcuControlState.slopeTxPending == SLOPE_UART_REQ_OPEN) ||
            (slopeUartTc375TxIsBusy() != 0u)
        );

        if (slope_open_or_opening == 0u)
        {
            g_doorEcuControlState.slopeAutoCloseStocked = 0u;
        }
        else if (doorEcuControlDoorIsClosing(door_close_about_to_happen) != 0u)
        {
            g_doorEcuControlState.slopeAutoCloseStocked = 0u;
            doorEcuControlQueueSlopeCloseAuto();
            return;
        }
    }
}

void doorEcuControlInit(void)
{
    DoorECUCANConfig can_cfg;

    doorEcuControlResetState();

    bleUartTc375Init();
    slopeUartTc375TxInit();
    ObstacleDetect_Init();
    doorInit(&IfxCcu60_CC62_P02_4_OUT, NULL_PTR, 0);

    doorECUCANInit(&g_doorCan);
    doorECUCANInitConfig(&can_cfg);
    can_cfg.port_name = "can0";
    can_cfg.nominal_bitrate = 500000U;

    if (doorECUCANOpen(&g_doorCan, &can_cfg) == CAN_STATUS_OK)
    {
        g_doorCanOpened = 1u;
    }

    g_doorEcuLastStatusTxMs = doorEcuGetNowMs();
}

void doorEcuControlPoll(void)
{
    doorEcuControlPollBleCommand();
    doorEcuControlPollCanCommand();

    ObstacleDetect_Service();
    doorEcuControlUpdateSensors();
    doorEcuControlHandleJamDuringClosing();

    slopeUartTc375TxPoll();
    doorEcuControlHandleSlopeTxSuccess();

    doorEcuControlRunPolicy();

    doorUpdate();

    doorEcuControlIssuePendingSlopeCommand();
    doorEcuControlRefreshStatusShadow();
    doorEcuControlPollStatusCan();
}

void doorEcuControlOnBleSlopeAutoRequest(DoorEcuRequest slope_request)
{
    if (slope_request == DOOR_ECU_REQUEST_OPEN)
    {
        g_doorEcuControlState.slopeAutoOpenStocked = 1u;
        g_doorEcuControlState.slopeAutoCloseStocked = 0u;
    }
    else if (slope_request == DOOR_ECU_REQUEST_CLOSE)
    {
        g_doorEcuControlState.slopeAutoCloseStocked = 1u;
        g_doorEcuControlState.slopeAutoOpenStocked = 0u;
    }
}

void doorEcuControlOnBleSlopeOpenRequest(void)
{
    doorEcuControlOnBleSlopeAutoRequest(DOOR_ECU_REQUEST_OPEN);
}

void doorEcuControlOnDriverCanDoorManualRequest(DoorEcuRequest door_request)
{
    if (door_request == DOOR_ECU_REQUEST_OPEN)
    {
        g_doorEcuControlState.doorManualOpenPending = 1u;
    }
    else if (door_request == DOOR_ECU_REQUEST_CLOSE)
    {
        g_doorEcuControlState.doorManualClosePending = 1u;
    }
}

void doorEcuControlOnDriverCanSlopeRequest(
    DoorEcuRequest slope_request,
    DoorEcuSlopeControlMode slope_mode
)
{
    if (slope_mode == DOOR_ECU_SLOPE_CONTROL_MANUAL)
    {
        if (slope_request == DOOR_ECU_REQUEST_OPEN)
        {
            g_doorEcuControlState.slopeManualOpenPending = 1u;
        }
        else if (slope_request == DOOR_ECU_REQUEST_CLOSE)
        {
            g_doorEcuControlState.slopeManualClosePending = 1u;
        }
    }
    else if (slope_mode == DOOR_ECU_SLOPE_CONTROL_AUTO)
    {
        if (slope_request == DOOR_ECU_REQUEST_OPEN)
        {
            g_doorEcuControlState.slopeAutoOpenStocked = 1u;
        }
        else if (slope_request == DOOR_ECU_REQUEST_CLOSE)
        {
            g_doorEcuControlState.slopeAutoCloseStocked = 1u;
        }
    }
}

void doorEcuControlOnDriverCanCombinedRequest(
    DoorEcuRequest door_request,
    DoorEcuRequest slope_request,
    DoorEcuSlopeControlMode slope_mode
)
{
    doorEcuControlOnDriverCanDoorManualRequest(door_request);
    doorEcuControlOnDriverCanSlopeRequest(slope_request, slope_mode);
}

static void doorEcuControlPollStatusCan(void)
{
    uint32_t now_ms;

    if (g_doorCanOpened == 0u)
    {
        return;
    }

    now_ms = doorEcuGetNowMs();

    if ((uint32_t)(now_ms - g_doorEcuLastStatusTxMs) < DOOR_ECU_STATUS_CAN_PERIOD_MS)
    {
        return;
    }

    g_doorEcuLastStatusTxMs = now_ms;

    doorEcuCanSendStatus(
        g_doorEcuControlState.status.door_opened,
        g_doorEcuControlState.status.jam_detected,
        g_doorEcuControlState.status.slope_opened,
        g_doorEcuControlState.status.slope_opened_valid
    );
}

const DoorEcuStatus* doorEcuControlGetStatus(void)
{
    return &g_doorEcuControlState.status;
}
