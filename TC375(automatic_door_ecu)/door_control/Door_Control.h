#ifndef DOOR_CONTROL_H
#define DOOR_CONTROL_H

#include "Ifx_Types.h"
#include "IfxPort.h"
#include "IfxCcu6_PinMap.h"

#define PCN_2_IDX 19
#define P2_IDX    2
#define PCN_1_IDX 11
#define P1_IDX    1

/* Servo pulse width as duty ratio @ 50 Hz */
#define DOOR_OPEN_DUTY    12.5f
#define DOOR_CLOSE_DUTY    2.5f
#define DOOR_DUTY_STEP                 0.1f
#define DOOR_MOVE_BASE_TIME_MS         ((uint32)((((DOOR_OPEN_DUTY) - (DOOR_CLOSE_DUTY)) / (DOOR_DUTY_STEP)) * (DOOR_SPEED_MS)))
#define DOOR_MOVE_MARGIN_MS            200U
#define DOOR_MOVE_ASSUMED_TIME_MS      (DOOR_MOVE_BASE_TIME_MS + DOOR_MOVE_MARGIN_MS)   /* 2000 ms */

#define DOOR_SPEED_MS     15U
#define DOOR_PWM_FREQ_HZ  50.0f

void doorInit(const IfxCcu6_Cc62_Out *servoPin, Ifx_P *btnPort, uint8 btnPin);
void driveDoorOpen(void);
void driveDoorClose(void);
void doorUpdate(void);

#endif /* DOOR_CONTROL_H */