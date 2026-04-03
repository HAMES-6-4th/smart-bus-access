#ifndef __SLOPE_CONTROL_H
#define __SLOPE_CONTROL_H

#include "main.h"

void initSlopeControl(GPIO_TypeDef* port1, uint16_t pin1, 
                       GPIO_TypeDef* port2, uint16_t pin2,
                       GPIO_TypeDef* port3, uint16_t pin3,
                       GPIO_TypeDef* port4, uint16_t pin4);

void slopeControl(void);
void updateSlope(void);

#endif /* __SLOPE_CONTROL_H */