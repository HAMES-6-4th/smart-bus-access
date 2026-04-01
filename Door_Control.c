#include "Ifx_Types.h"
#include "IfxCpu.h"
#include "IfxScuWdt.h"
#include "IfxPort.h"
#include "IfxCcu6_PinMap.h"
#include "Bsp.h"

#include "Door_Control.h"

void initDoorGPIO(void) {
    P02_IOCR0.U &= ~(0x1F << PCN_1_IDX);
    P02_IOCR0.U |= 0x02 << PCN_1_IDX;

    P10_IOCR0.U &= ~(0x1F << PCN_1_IDX);
    P10_IOCR0.U |= 0x10 << PCN_1_IDX;

    P10_IOCR0.U &= ~(0x1F << PCN_2_IDX);
    P10_IOCR0.U |= 0x10 << PCN_2_IDX;
}

void initDoorPWM(void) {
    uint16 password = IfxScuWdt_getCpuWatchdogPassword();
    IfxScuWdt_clearCpuEndinit(password);
    MODULE_CCU60.CLC.U = 0x00000000;
    IfxScuWdt_setCpuEndinit(password);

    IfxPort_setPinModeOutput(IfxCcu60_CC60_P02_6_OUT.pin.port,
                             IfxCcu60_CC60_P02_6_OUT.pin.pinIndex,
                             IfxPort_OutputMode_pushPull,
                             IfxCcu60_CC60_P02_6_OUT.select);

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

void doorInit(void){
    initDoorPWM();
    initDoorGPIO();
    setServoDutyCycle(DOOR_CLOSE_DUTY);
}

void driveDoorOpen(void){
    for(float duty = DOOR_CLOSE_DUTY; duty <= DOOR_OPEN_DUTY; duty += 0.1f) {
        setServoDutyCycle(duty);
        waitTime(IfxStm_getTicksFromMilliseconds(BSP_DEFAULT_TIMER, DOOR_SPEED_MS));
    }
}

void driveDoorClose(void){
    for(float duty = DOOR_OPEN_DUTY; duty >= DOOR_CLOSE_DUTY; duty -= 0.1f) {
        setServoDutyCycle(duty);
        waitTime(IfxStm_getTicksFromMilliseconds(BSP_DEFAULT_TIMER, DOOR_SPEED_MS));
    }
}