/*
******************************************************************************
You should make SlopeControl sc before using these functions. 
Then call SlopeInit(&sc, &htimX, TIM_CHANNEL_Y) to initialize it. 

In your main loop, call SlopeProcess(&sc) to update the servo position. 
Use SlopeOpen(&sc) and SlopeClose(&sc) to open and close the slope.
******************************************************************************
*/

#include "Slope_Control.h"

void SlopeInit(SlopeControl *sc, TIM_HandleTypeDef *htim, uint32_t channel) 
{
    sc->htim = htim;
    sc->channel = channel;
    sc->currentDuty = 1048;
    sc->targetDuty = 1048;
    sc->lastTick = 0;
    sc->interval = 10;     
    sc->state = SERVO_IDLE;

    HAL_TIM_PWM_Start(sc->htim, sc->channel);
    __HAL_TIM_SET_COMPARE(sc->htim, sc->channel, sc->currentDuty);
}

void SlopeMoveTo(SlopeControl *sc, uint32_t target) 
{
    sc->targetDuty = target;
    if (sc->targetDuty > sc->currentDuty) sc->state = SERVO_MOVING_UP;
    else if (sc->targetDuty < sc->currentDuty) sc->state = SERVO_MOVING_DOWN;
}

void SlopeProcess(SlopeControl *sc) 
{
    if (sc->state == SERVO_IDLE) return;

    uint32_t currentTick = HAL_GetTick();
    if (currentTick - sc->lastTick >= sc->interval) 
    {
        sc->lastTick = currentTick;

        if (sc->state == SERVO_MOVING_UP) 
        {
            sc->currentDuty += 20; 
            if (sc->currentDuty >= sc->targetDuty) 
            {
                sc->currentDuty = sc->targetDuty;
                sc->state = SERVO_IDLE;
            }
        } 
        else if (sc->state == SERVO_MOVING_DOWN) 
        {
            sc->currentDuty -= 20; 
            if (sc->currentDuty <= sc->targetDuty) 
            {
                sc->currentDuty = sc->targetDuty;
                sc->state = SERVO_IDLE;
            }
        }

        __HAL_TIM_SET_COMPARE(sc->htim, sc->channel, sc->currentDuty);
    }
}

void SlopeOpen(SlopeControl *sc) 
{
    SlopeMoveTo(sc, SERVO_POS_180);
}

void SlopeClose(SlopeControl *sc) 
{
    SlopeMoveTo(sc, SERVO_POS_0);
}