#ifndef DOOR_ECU_CONTROL_H
#define DOOR_ECU_CONTROL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum DoorEcuRequestEnum
{
    DOOR_ECU_REQUEST_NONE = 0,
    DOOR_ECU_REQUEST_OPEN,
    DOOR_ECU_REQUEST_CLOSE
} DoorEcuRequest;

typedef enum DoorEcuSlopeControlModeEnum
{
    DOOR_ECU_SLOPE_CONTROL_NONE = 0,
    DOOR_ECU_SLOPE_CONTROL_AUTO,
    DOOR_ECU_SLOPE_CONTROL_MANUAL
} DoorEcuSlopeControlMode;

typedef enum DoorEcuDoorMotionEnum
{
    DOOR_ECU_DOOR_MOTION_UNKNOWN = 0,
    DOOR_ECU_DOOR_MOTION_CLOSED,
    DOOR_ECU_DOOR_MOTION_OPENING,
    DOOR_ECU_DOOR_MOTION_OPENED,
    DOOR_ECU_DOOR_MOTION_CLOSING
} DoorEcuDoorMotion;

typedef struct DoorEcuStatusStruct
{
    uint8_t door_opened;
    uint8_t jam_detected;
    uint8_t slope_opened;
    uint8_t slope_opened_valid;

    uint8_t door_motion;
    uint8_t slope_auto_open_stocked;
    uint8_t slope_auto_close_stocked;
    uint8_t slope_tx_pending;
} DoorEcuStatus;

void doorEcuControlInit(void);
void doorEcuControlPoll(void);

/* BLE slope request는 automatic request 로 해석한다. */
void doorEcuControlOnBleSlopeAutoRequest(DoorEcuRequest slope_request);

/* 기존 open-only hook과의 호환용 wrapper */
void doorEcuControlOnBleSlopeOpenRequest(void);

/* CAN 수동 door request */
void doorEcuControlOnDriverCanDoorManualRequest(DoorEcuRequest door_request);

/* CAN slope request (auto/manual 지정) */
void doorEcuControlOnDriverCanSlopeRequest(
    DoorEcuRequest slope_request,
    DoorEcuSlopeControlMode slope_mode
);

/* CAN 에서 door + slope를 한 번에 받는 경우 */
void doorEcuControlOnDriverCanCombinedRequest(
    DoorEcuRequest door_request,
    DoorEcuRequest slope_request,
    DoorEcuSlopeControlMode slope_mode
);

const DoorEcuStatus* doorEcuControlGetStatus(void);

#ifdef __cplusplus
}
#endif

#endif