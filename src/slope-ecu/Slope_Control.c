/*
***************************************************************
You should use initSlopeControl() to initialize the GPIO pins for the stepper motor control.
ex)
Init_Sliding_Door(GPIOA, GPIO_PIN_6, 
                    GPIOA, GPIO_PIN_7, 
                    GPIOA, GPIO_PIN_8, 
                    GPIOA, GPIO_PIN_9);

You should call updateSlope() in the main loop to update the stepper motor control.
***************************************************************
*/

#include "Slope_Control.h"

uint8_t stepSequence[4][4] = {
    {1, 1, 0, 0}, 
    {0, 1, 1, 0},
    {0, 0, 1, 1}, 
    {1, 0, 0, 1}  
};

uint8_t isSlopeMoving = 0;     
uint32_t currentStepCount = 0;  
uint32_t targetSteps = 1024;    
uint32_t lastTick = 0;      
int stepIndex = 0;          

static GPIO_TypeDef* mPorts[4];
static uint16_t mPins[4];

void initSlopeControl(GPIO_TypeDef* port1, uint16_t pin1,
                       GPIO_TypeDef* port2, uint16_t pin2,
                       GPIO_TypeDef* port3, uint16_t pin3,
                       GPIO_TypeDef* port4, uint16_t pin4) {
    mPorts[0] = port1; mPins[0] = pin1;
    mPorts[1] = port2; mPins[1] = pin2;
    mPorts[2] = port3; mPins[2] = pin3;
    mPorts[3] = port4; mPins[3] = pin4;
}

void driveSlopeOpen(void) {
    if(isSlopeMoving == 0) {  // If the door is not currently moving
        isSlopeMoving = 1;
        currentStepCount = 0;
        lastTick = HAL_GetTick(); 
    }
}

void driveSlopeClose(void) {
    if(isSlopeMoving == 0) {  // If the door is not currently moving
        isSlopeMoving = 1;
        currentStepCount = 0;
        lastTick = HAL_GetTick(); 
    }
}

void updateSlope(void) {
    if (isSlopeMoving == 1) {
        if (HAL_GetTick() - lastTick >= 3) {
            lastTick = HAL_GetTick(); 

            HAL_GPIO_WritePin(mPorts[0], mPins[0], stepSequence[stepIndex][0] ? GPIO_PIN_SET : GPIO_PIN_RESET); 
            HAL_GPIO_WritePin(mPorts[1], mPins[1], stepSequence[stepIndex][1] ? GPIO_PIN_SET : GPIO_PIN_RESET); 
            HAL_GPIO_WritePin(mPorts[2], mPins[2], stepSequence[stepIndex][2] ? GPIO_PIN_SET : GPIO_PIN_RESET); 
            HAL_GPIO_WritePin(mPorts[3], mPins[3], stepSequence[stepIndex][3] ? GPIO_PIN_SET : GPIO_PIN_RESET); 

            stepIndex++; 
            if(stepIndex > 3) stepIndex = 0; 

            currentStepCount++; 
            
            if (currentStepCount >= targetSteps) {
                isSlopeMoving = 0; 
                HAL_GPIO_WritePin(mPorts[0], mPins[0], GPIO_PIN_RESET);
                HAL_GPIO_WritePin(mPorts[1], mPins[1], GPIO_PIN_RESET);
                HAL_GPIO_WritePin(mPorts[2], mPins[2], GPIO_PIN_RESET);
                HAL_GPIO_WritePin(mPorts[3], mPins[3], GPIO_PIN_RESET);
            }
        }
    }
}