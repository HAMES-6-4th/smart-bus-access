#ifndef __SLOPE_CONTROL_H
#define __SLOPE_CONTROL_H

#include "main.h"

#define DOOR_OPEN_DUTY   12.5f
#define SERVO_POS_0      1048  
#define SERVO_POS_180    5242

typedef enum 
{
    SERVO_IDLE,     
    SERVO_MOVING_UP,
    SERVO_MOVING_DOWN
} ServoState;

typedef struct 
{
    TIM_HandleTypeDef *htim; 
    uint32_t channel;    
    uint32_t currentDuty; 
    uint32_t targetDuty;  
    uint32_t lastTick;     
    uint32_t interval;       
    ServoState state;     
} SlopeControl;

void SlopeInit(SlopeControl *sc, TIM_HandleTypeDef *htim, uint32_t channel);
void SlopeMoveTo(SlopeControl *sc, uint32_t target);
void SlopeProcess(SlopeControl *sc);

void SlopeOpen(SlopeControl *sc);  
void SlopeClose(SlopeControl *sc); 

#endif