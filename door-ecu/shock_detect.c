#include "shock_detect.h"

void initShockISR()
{
    uint16 password = IfxScuWdt_getSafetyWatchdogPasswordInline();
    IfxScuWdt_clearSafetyEndinitInline(password);

    /*일반 하차벨 버튼 인터럽트 설정 */
    //Port 02.1을 Pull-Down Input으로 Set
    MODULE_P02.IOCR0.B.PC1 = 0x01;

    /* EICR.EXIS 레지스터 설정 : ERS2, 1번 신호 */
    MODULE_SCU.EICR[1].B.EXIS0 = 1;

    /* rising, falling edge 트리거 설정 */
    MODULE_SCU.EICR[1].B.REN0 = 0;
    MODULE_SCU.EICR[1].B.FEN0 = 1;

    // Channel 2의 Trigger Event 활성화
    MODULE_SCU.EICR[1].B.EIEN0 = 1;

    // OGU 0으로 ETL2(TR) Trigger Event 보낼거임
    MODULE_SCU.EICR[1].B.INP0 = 0;

    // OGU0은 IGCR[0]/IGP0임 Pattern 고려 없이
    MODULE_SCU.IGCR[0].B.IGP0 = 1;


    //Interrupt를 위해 SCU의 SRC설정
    volatile Ifx_SRC_SRCR *src;
    src = (volatile Ifx_SRC_SRCR*) (&MODULE_SRC.SCU.SCUERU[0]);
    src->B.SRPN = SHOCK_ISR_PRIORITY;
    src->B.TOS = 0;
    src->B.CLRR = 1;
    src->B.SRE = 1;

    IfxScuWdt_setSafetyEndinitInline(password);
}
