#ifndef OBSTACLE_DETECT_H
#define OBSTACLE_DETECT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void ObstacleDetect_Init(void);
void ObstacleDetect_Service(void);

uint8_t ObstacleDetect_IsDetected(void);
uint8_t ObstacleDetect_IsValid(void);
float ObstacleDetect_GetLastDistanceCm(void);

#ifdef __cplusplus
}
#endif

#endif