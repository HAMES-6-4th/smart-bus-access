    /*
    *******************************************************************************
    doorInit() should be called once at the start of the program to set up the door
    control system.

    doorUpdate() should be called in the main loop to smoothly transition the door
    to the target position when driveDoorOpen() or driveDoorClose() is called.
    *******************************************************************************
    */

    #include "Ifx_Types.h"
    #include "IfxCpu.h"
    #include "IfxPort.h"
    #include "IfxStm.h"
    #include "IfxCcu6_PinMap.h"
    #include "IfxCcu6_PwmHl.h"
    #include "IfxCcu6_TimerWithTrigger.h"
    #include "Bsp.h"
    #include "IfxStdIf_Timer.h"

    #include "Door_Control.h"

    /* ------------------------------------------------------------------------- */
    /* Internal state                                                            */
    /* ------------------------------------------------------------------------- */

    static IfxCcu6_TimerWithTrigger g_doorTimer;
    static IfxCcu6_PwmHl            g_doorPwmHl;

    static boolean g_doorReady = FALSE;

    static float32 currentDuty = DOOR_CLOSE_DUTY;
    static float32 targetDuty  = DOOR_CLOSE_DUTY;

    static uint64 lastUpdateTick = 0;
    static uint64 tickInterval   = 0;

    /* ------------------------------------------------------------------------- */
    /* Helpers                                                                   */
    /* ------------------------------------------------------------------------- */

    static void initDoorGPIO(Ifx_P *btnPort, uint8 btnPin)
    {
        IfxPort_setPinModeInput(btnPort, btnPin, IfxPort_InputMode_pullUp);
    }

    static float32 clampDuty(float32 dutyPercent)
    {
        if (dutyPercent < 0.0f)
        {
            return 0.0f;
        }

        if (dutyPercent > 100.0f)
        {
            return 100.0f;
        }

        return dutyPercent;
    }

    static void doorApplyDuty(float32 dutyPercent)
    {
        Ifx_TimerValue tOn[3] = {0, 0, 0};
        Ifx_TimerValue period;
        float32 duty;

        if (!g_doorReady)
        {
            return;
        }

        duty   = clampDuty(dutyPercent);
        period = IfxCcu6_TimerWithTrigger_getPeriod(&g_doorTimer);

        /* CC62 is the 3rd T12 compare channel => index 2 */
        tOn[2] = (Ifx_TimerValue)(((float32)period * duty) / 100.0f);

        IfxCcu6_PwmHl_setOnTime(&g_doorPwmHl, tOn);
        IfxCcu6_TimerWithTrigger_applyUpdate(&g_doorTimer);
    }

    static boolean initDoorPWM(const IfxCcu6_Cc62_Out *servoPin)
    {
        boolean interruptState;
        IfxCcu6_TimerWithTrigger_Config timerCfg;
        IfxCcu6_PwmHl_Config            pwmHlCfg;
        boolean activeCh[6] = {FALSE, FALSE, FALSE, FALSE, TRUE, FALSE};
        boolean stuckSt[6]  = {FALSE, FALSE, FALSE, FALSE, FALSE, FALSE};

        interruptState = IfxCpu_disableInterrupts();

        /* ---------------- Timer ---------------- */
        IfxCcu6_TimerWithTrigger_initConfig(&timerCfg, &MODULE_CCU60);
        timerCfg.base.frequency = DOOR_PWM_FREQ_HZ;
        timerCfg.base.countDir  = IfxStdIf_Timer_CountDir_upAndDown;

        if (!IfxCcu6_TimerWithTrigger_init(&g_doorTimer, &timerCfg))
        {
            IfxCpu_restoreInterrupts(interruptState);
            return FALSE;
        }

        /* --------------- PWM HL ---------------- */
        IfxCcu6_PwmHl_initConfig(&pwmHlCfg);
        pwmHlCfg.timer = &g_doorTimer;

        /*
        * We only use CC62 (3rd channel), but PwmHl is structured as cc0/cc1/cc2.
        * So keep channelCount = 3 and drive only index 2.
        */
        pwmHlCfg.base.channelCount = 3;
        pwmHlCfg.cc2 = (IfxCcu6_Cc62_Out *)servoPin;

        if (!IfxCcu6_PwmHl_init(&g_doorPwmHl, &pwmHlCfg))
        {
            IfxCpu_restoreInterrupts(interruptState);
            return FALSE;
        }

        IfxCcu6_PwmHl_setMode(&g_doorPwmHl, Ifx_Pwm_Mode_centerAligned);

        /*
        * Enable only phase 2 top output (CC62).
        * Layout:
        * [0]=phase0 top, [1]=phase0 bottom,
        * [2]=phase1 top, [3]=phase1 bottom,
        * [4]=phase2 top, [5]=phase2 bottom
        */
        IfxCcu6_PwmHl_setupChannels(&g_doorPwmHl, activeCh, stuckSt);

        /* Apply initial duty before running */
        doorApplyDuty(DOOR_CLOSE_DUTY);
        IfxCcu6_TimerWithTrigger_run(&g_doorTimer);

        IfxCpu_restoreInterrupts(interruptState);
        return TRUE;
    }

    /* ------------------------------------------------------------------------- */
    /* Public API                                                                */
    /* ------------------------------------------------------------------------- */

    void doorInit(const IfxCcu6_Cc62_Out *servoPin, Ifx_P *btnPort, uint8 btnPin)
    {
        if (btnPort != NULL_PTR)
        {
            initDoorGPIO(btnPort, btnPin);
        }

        currentDuty = DOOR_CLOSE_DUTY;
        targetDuty  = DOOR_CLOSE_DUTY;

        g_doorReady = initDoorPWM(servoPin);

        tickInterval   = IfxStm_getTicksFromMilliseconds(BSP_DEFAULT_TIMER, DOOR_SPEED_MS);
        lastUpdateTick = IfxStm_get(BSP_DEFAULT_TIMER);
    }

    void driveDoorOpen(void)
    {
        targetDuty = DOOR_OPEN_DUTY;
    }

    void driveDoorClose(void)
    {
        targetDuty = DOOR_CLOSE_DUTY;
    }

    void doorUpdate(void)
    {
        const float32 eps = 0.0001f;
        uint64 currentTick;

        if (!g_doorReady)
        {
            return;
        }

        if ((currentDuty > targetDuty + eps) || (currentDuty < targetDuty - eps))
        {
            currentTick = IfxStm_get(BSP_DEFAULT_TIMER);

            if ((currentTick - lastUpdateTick) >= tickInterval)
            {
                lastUpdateTick = currentTick;

                if (currentDuty < targetDuty)
                {
                    currentDuty += 0.1f;
                    if (currentDuty > targetDuty - 0.05f)
                    {
                        currentDuty = targetDuty;
                    }
                }
                else
                {
                    currentDuty -= 0.1f;
                    if (currentDuty < targetDuty + 0.05f)
                    {
                        currentDuty = targetDuty;
                    }
                }

                doorApplyDuty(currentDuty);
            }
        }
    }