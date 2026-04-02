#include "Ifx_Types.h"
#include "IfxCpu.h"
#include "IfxScuWdt.h"
#include "Ifx_Cfg_Ssw.h"
#include "ble_uart_tc375_lite.h"
#include <stdint.h>
#include <stdbool.h>
#include "door_ecu_control.h"
#include "obstacle_detect.h"

IFX_ALIGN(4) IfxCpu_syncEvent cpuSyncEvent = 0;

static uint32_t g_status_report_divider = 0u;

void core0_main(void)
{
    IfxCpu_enableInterrupts();
    IfxScuWdt_disableCpuWatchdog(IfxScuWdt_getCpuWatchdogPassword());

    IfxCpu_emitEvent(&cpuSyncEvent);
    IfxCpu_waitEvent(&cpuSyncEvent, 1);

    doorEcuControlInit();

    while (1)
    {
        doorEcuControlPoll();
    }
}
