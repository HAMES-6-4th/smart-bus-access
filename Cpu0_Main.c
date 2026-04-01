#include "Ifx_Types.h"
#include "IfxCpu.h"
#include "IfxScuWdt.h"
#include "IfxPort.h"
#include "IfxPort_PinMap.h"
#include "UART_VCOM.h"
#include <stdio.h>

#include "Driver_Stm.h"

IFX_ALIGN(4) IfxCpu_syncEvent cpuSyncEvent = 0;

/* 초음파 센서 핀 정의 */
#define ULTRASONIC_TRIG_PIN   IfxPort_P02_6
#define ULTRASONIC_ECHO_PIN   IfxPort_P02_7

#define LED_1 IfxPort_P00_5

typedef enum
{
    OBSTACLE_DETECTED = 0,
    OBSTACLE_NOT_DETECTED
} ObstacleState;

volatile ObstacleState obstacleState = OBSTACLE_NOT_DETECTED;

void appTask1ms(void);
void appTask10ms(void);
void appTask100ms(void);
void appTask1000ms(void);
void appScheduling(void);

float measureDistance(void);
void triggerUltrasonic(void);
void delayMicroseconds(uint32 us);

void initGPIO();

void core0_main(void)
{
    IfxCpu_enableInterrupts();
    IfxScuWdt_disableCpuWatchdog(IfxScuWdt_getCpuWatchdogPassword());
    IfxScuWdt_disableSafetyWatchdog(IfxScuWdt_getSafetyWatchdogPassword());

    IfxCpu_emitEvent(&cpuSyncEvent);
    IfxCpu_waitEvent(&cpuSyncEvent, 1);

    //UART 초기화
    init_UART();
    // 타이머를 위한 STM 초기화
    Driver_Stm_Init();

    // GPIO 초기화
    initGPIO();

    while(1)
    {
        appScheduling();
    }
}

void initGPIO()
{
    // 초음파 Echo랑 Trig GPIO 초기화
    IfxPort_setPinModeOutput(ULTRASONIC_TRIG_PIN.port, ULTRASONIC_TRIG_PIN.pinIndex,
                             IfxPort_OutputMode_pushPull, IfxPort_PadDriver_cmosAutomotiveSpeed1);
    IfxPort_setPinModeInput(ULTRASONIC_ECHO_PIN.port, ULTRASONIC_ECHO_PIN.pinIndex,
                            IfxPort_InputMode_pullDown);

    // 보드 내장 LED1 초기화
    IfxPort_setPinModeOutput(LED_1.port, LED_1.pinIndex, IfxPort_OutputMode_pushPull, IfxPort_OutputIdx_general);
}

/* 마이크로초 단위 딜레이 */
void delayMicroseconds(uint32 us)
{
    uint32 t = us * 100; // CPU 클록 기준 조정 필요
    while(t--) { __nop(); }
}

/* 초음파 트리거 펄스 발생 */
void triggerUltrasonic(void)
{
    IfxPort_setPinHigh(ULTRASONIC_TRIG_PIN.port, ULTRASONIC_TRIG_PIN.pinIndex);
    delayMicroseconds(10); // 10us 펄스
    IfxPort_setPinLow(ULTRASONIC_TRIG_PIN.port, ULTRASONIC_TRIG_PIN.pinIndex);
}

/* 거리 측정 */
float measureDistance(void)
{
    uint32 startTime = 0, endTime = 0;
    uint32 pulseWidth = 0;

    /* Echo 핀 상승 대기 */
    while(!IfxPort_getPinState(ULTRASONIC_ECHO_PIN.port, ULTRASONIC_ECHO_PIN.pinIndex));
    startTime = STM0_TIM0.U;

    /* Echo 핀 하강 대기 */
    while(IfxPort_getPinState(ULTRASONIC_ECHO_PIN.port, ULTRASONIC_ECHO_PIN.pinIndex));
    endTime = STM0_TIM0.U;

    pulseWidth = endTime - startTime;  // STM 카운터 차이

    /* 100MHz 기준: 1 카운트 = 10ns -> 1us = 100 카운트 */
    float pulse_us = (float)pulseWidth / 100.0f;

    return pulse_us / 58.0f;  // cm 단위 계산
}

void appTask1ms(void)
{

}

void appTask10ms(void)
{

}

void appTask100ms(void)
{
    triggerUltrasonic();
    float distance = measureDistance();

    char buffer[64];
    sprintf(buffer, "Distance: %.2f cm\r\n", distance);  // 거리 문자열 생성
    if(distance > 30.0) // 30cm 미만 ( 장애물 감지시 ) 내장 LED_1 ON
    {
        obstacleState = OBSTACLE_NOT_DETECTED; // 장애물 감지 상태
        IfxPort_setPinHigh(LED_1.port, LED_1.pinIndex);
    } else            // 장애물 미감지시 LED_1 OFF
    {
        obstacleState = OBSTACLE_DETECTED;    // 장애물 미감지 상태
        IfxPort_setPinLow(LED_1.port, LED_1.pinIndex);
    }

    send_UART_message(buffer); // 거리 측정 디버깅을 위한 UART 출력 (실제 환경에서는 주석 추가 바람)

}

void appTask1000ms(void)
{
}

void appScheduling(void)
{
    if(stSchedulingInfo.u8nuScheduling1msFlag)
    {
        stSchedulingInfo.u8nuScheduling1msFlag = 0;
        appTask1ms();

        if(stSchedulingInfo.u8nuScheduling10msFlag)
        {
            stSchedulingInfo.u8nuScheduling10msFlag = 0;
            appTask10ms();
        }

        if(stSchedulingInfo.u8nuScheduling100msFlag)
        {
            stSchedulingInfo.u8nuScheduling100msFlag = 0;
            appTask100ms();
        }

        if(stSchedulingInfo.u8nuScheduling1000msFlag)
        {
            stSchedulingInfo.u8nuScheduling1000msFlag = 0;
            appTask1000ms();
        }
    }
}
