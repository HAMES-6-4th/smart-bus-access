#ifndef DOOR_CONTROL_H
#define DOOR_CONTROL_H

#define PCN_2_IDX 19
#define P2_IDX 2
#define PCN_1_IDX 11
#define P1_IDX 1

#define DOOR_OPEN_DUTY   12.5f
#define DOOR_CLOSE_DUTY  2.5f
#define DOOR_SPEED_MS    15

#define PWM_PERIOD 15625

void doorInit(void);
void driveDoorOpen(void);
void driveDoorClose(void);
void doorControl(void);

#endif /* DOOR_CONTROL_H */