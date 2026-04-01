/*
*******************************************************************************
doorInit() should be called once at the start of the program to set up the door control system. 
doorupdate() should be called in the main loop to smoothly transition the door to the target position when driveDoorOpen() or driveDoorClose() is called.
*****************************************************************************
*/ 

#include "Ifx_Types.h"
#include "IfxCpu.h"
#include "IfxScuWdt.h"
#include "IfxPort.h"
#include "IfxCcu6_PinMap.h"
#include "Bsp.h"

#include "Door_Control.h"

static float currentDuty = DOOR_CLOSE_DUTY;
static float targetDuty = DOOR_CLOSE_DUTY;
static uint64 lastUpdateTick = 0;
static uint64 tickInterval = 0; 

void initDoorGPIO(Ifx_P *btnPort, uint8 btnPin) {
    IfxPort_setPinModeInput(btnPort, btnPin, IfxPort_InputMode_pullUp);
}

void initDoorPWM(const IfxCcu6_Cc60_Out *servoPin) {
    uint16 password = IfxScuWdt_getCpuWatchdogPassword();
    IfxScuWdt_clearCpuEndinit(password);
    MODULE_CCU60.CLC.U = 0x00000000;
    IfxScuWdt_setCpuEndinit(password);

    IfxPort_setPinModeOutput(servoPin->pin.port,
                             servoPin->pin.pinIndex,
                             IfxPort_OutputMode_pushPull,
                             servoPin->select);

    MODULE_CCU60.TCTR0.B.T12CLK = 7;
    MODULE_CCU60.TCTR0.B.T12PRE = 0;
    MODULE_CCU60.TCTR2.B.T12SSC = 0;

    MODULE_CCU60.T12PR.U = PWM_PERIOD - 1;

    MODULE_CCU60.T12MSEL.B.MSEL60 = 1;
    MODULE_CCU60.CC60R.U = PWM_PERIOD;

    MODULE_CCU60.MODCTR.B.T12MODEN = 1;
    MODULE_CCU60.PSLR.U = 0x00000000;

    MODULE_CCU60.TCTR4.B.T12RS = 1;
}

void setServoDutyCycle(float dutyPercent) {
    uint32 compareValue = (uint32)((PWM_PERIOD * (100.0f - dutyPercent)) / 100.0f);

    MODULE_CCU60.CC60SR.U = compareValue;
    MODULE_CCU60.TCTR4.B.T12STR = 1;
}

void doorInit(const IfxCcu6_Cc60_Out *servoPin, Ifx_P *btnPort, uint8 btnPin){
    initDoorPWM(servoPin);
    initDoorGPIO(btnPort, btnPin);

    currentDuty = DOOR_CLOSE_DUTY;
    targetDuty = DOOR_CLOSE_DUTY;
    setServoDutyCycle(currentDuty);

    tickInterval = IfxStm_getTicksFromMilliseconds(BSP_DEFAULT_TIMER, DOOR_SPEED_MS);
    lastUpdateTick = IfxStm_get(BSP_DEFAULT_TIMER);
}

void driveDoorOpen(void){
    targetDuty = DOOR_OPEN_DUTY;
}

void driveDoorClose(void){
    targetDuty = DOOR_CLOSE_DUTY;
}

void doorUpdate(void){
    if (currentDuty != targetDuty) {
        uint64 currentTick = IfxStm_get(BSP_DEFAULT_TIMER); 
        
        if ((currentTick - lastUpdateTick) >= tickInterval) {
            lastUpdateTick = currentTick; 

            if (currentDuty < targetDuty) {
                currentDuty += 0.1f;
                if (currentDuty > targetDuty - 0.05f) currentDuty = targetDuty; 
            } 
            else if (currentDuty > targetDuty) {
                currentDuty -= 0.1f;
                if (currentDuty < targetDuty + 0.05f) currentDuty = targetDuty;
            }
            setServoDutyCycle(currentDuty); 
        }
    }
}