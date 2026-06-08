#include "slope_uart_tc375_lite.h"
#include "slope_uart.h"

#include "Asclin/Asc/IfxAsclin_Asc.h"
#include "Cpu/Irq/IfxCpu_Irq.h"
#include "Port/Std/IfxPort.h"
#include "Src/Std/IfxSrc.h"
#include "Stm/Std/IfxStm.h"
#include "IfxScuCcu.h"

static uint32_t g_slopeUartTicksPerMs = 0u;

typedef struct SlopeUartTc375DebugCountersStruct
{
    volatile uint32_t tx_request_count;
    volatile uint32_t tx_frame_write_count;
    volatile uint32_t tx_ack_ok_count;
    volatile uint32_t tx_resend_count;
    volatile uint32_t tx_giveup_count;

    volatile uint32_t rx_byte_count;
    volatile uint32_t rx_parse_ok_count;
    volatile uint32_t rx_parse_error_count;
    volatile uint32_t rx_ack_frame_count;

    volatile uint8_t  last_tx_cmd;
    volatile uint8_t  last_tx_seq;
    volatile uint8_t  last_ack_seq;
} SlopeUartTc375DebugCounters;

volatile SlopeUartTc375DebugCounters g_slope_uart_dbg = {0};

#define SLOPE_UART_TC375_ASC_BAUDRATE            ((uint32_t)9600u)

#define SLOPE_UART_TC375_ASC_TX_ISR_PRIORITY     (43u)
#define SLOPE_UART_TC375_ASC_RX_ISR_PRIORITY     (44u)
#define SLOPE_UART_TC375_ASC_ER_ISR_PRIORITY     (45u)

#define SLOPE_UART_TC375_ASC_TX_BUFFER_SIZE      (128u)
#define SLOPE_UART_TC375_ASC_RX_BUFFER_SIZE      (128u)

/* TC375 Lite Kit S2G1 UART */
#define SLOPE_UART_TC375_ASC_MODULE              (&MODULE_ASCLIN2)
#define SLOPE_UART_TC375_ASC_RX_PIN              IfxAsclin2_RXE_P33_8_IN
#define SLOPE_UART_TC375_ASC_TX_PIN              IfxAsclin2_TX_P33_9_OUT

static IfxAsclin_Asc g_slopeUartTc375Asc;

static uint8 g_slopeUartTc375AscTxBuffer[SLOPE_UART_TC375_ASC_TX_BUFFER_SIZE + sizeof(Ifx_Fifo) + 8];
static uint8 g_slopeUartTc375AscRxBuffer[SLOPE_UART_TC375_ASC_RX_BUFFER_SIZE + sizeof(Ifx_Fifo) + 8];

static SlopeUartSender g_slopeUartSender;
static SlopeUartParser g_slopeUartAckParser;

static uint8_t g_slopeUartInitialized = 0u;
static uint8_t g_desiredCmd = SLOPE_UART_REQ_NONE;
static uint8_t g_lastQueuedCmd = 0xFFu;

static uint8_t g_txSuccessPending = 0u;
static uint8_t g_txSuccessCmd = SLOPE_UART_REQ_NONE;
static uint8_t g_inflightCmd = SLOPE_UART_REQ_NONE;

IFX_INTERRUPT(slopeUartTc375AscTxIsr, 0, SLOPE_UART_TC375_ASC_TX_ISR_PRIORITY);
IFX_INTERRUPT(slopeUartTc375AscRxIsr, 0, SLOPE_UART_TC375_ASC_RX_ISR_PRIORITY);
IFX_INTERRUPT(slopeUartTc375AscErIsr, 0, SLOPE_UART_TC375_ASC_ER_ISR_PRIORITY);

void slopeUartTc375AscTxIsr(void)
{
    IfxAsclin_Asc_isrTransmit(&g_slopeUartTc375Asc);
}

void slopeUartTc375AscRxIsr(void)
{
    g_slope_uart_dbg.rx_byte_count++;
    IfxAsclin_Asc_isrReceive(&g_slopeUartTc375Asc);
}

void slopeUartTc375AscErIsr(void)
{
    IfxAsclin_Asc_isrError(&g_slopeUartTc375Asc);
}

static void slopeUartTc375WriteFrame(const SlopeUartFrame* frame)
{
    Ifx_SizeT count = SLOPE_UART_FRAME_SIZE;
    (void)IfxAsclin_Asc_write(&g_slopeUartTc375Asc, (void*)frame->bytes, &count, TIME_INFINITE);
}

static void slopeUartTc375ProcessRx(void)
{
    uint8 byte;
    Ifx_SizeT count;
    SlopeUartDecodedFrame decoded;
    int rc;

    for (;;)
    {
        count = 1u;
        if (IfxAsclin_Asc_read(&g_slopeUartTc375Asc, &byte, &count, 0u) != TRUE)
        {
            break;
        }

        rc = slopeUartParserFeed(&g_slopeUartAckParser, byte, &decoded);
        if (rc == SLOPE_UART_PARSE_OK)
        {
            if (slopeUartSenderOnFrame(&g_slopeUartSender, &decoded) != 0)
            {
                g_txSuccessPending = 1u;
                g_txSuccessCmd = g_inflightCmd;

                /* 전송 성공 후 내부 sender 상태는 idle(NONE)로 복귀 */
                g_desiredCmd = slopeUartMakeCmd(SLOPE_UART_REQ_NONE);
                g_lastQueuedCmd = slopeUartMakeCmd(SLOPE_UART_REQ_NONE);
            }
        }
    }
}

static uint32_t slopeUartTc375GetTickMs(void)
{
    uint32_t stm_ticks;

    stm_ticks = IfxStm_getLower(&MODULE_STM0);

    return (stm_ticks / g_slopeUartTicksPerMs);
}

static void slopeUartTc375PollRetry(void)
{
    SlopeUartFrame frame;
    int rc;

    uint32_t now_ms;

    now_ms = slopeUartTc375GetTickMs();
    rc = slopeUartSenderPoll(&g_slopeUartSender, now_ms, &frame);
    
    if (rc == SLOPE_UART_SENDER_POLL_RESEND)
    {
        slopeUartTc375WriteFrame(&frame);
    }
    else if (rc == SLOPE_UART_SENDER_POLL_GIVEUP)
    {
        g_lastQueuedCmd = 0xFFu;
    }
}

static void slopeUartTc375TrySendDesired(void)
{
    SlopeUartFrame frame;
    uint32_t now_ms;

    if (slopeUartSenderIsBusy(&g_slopeUartSender) != 0u)
    {
        return;
    }

    if (g_desiredCmd == g_lastQueuedCmd)
    {
        return;
    }

    now_ms = slopeUartTc375GetTickMs();

    if (slopeUartSenderStart(&g_slopeUartSender, g_desiredCmd, now_ms, &frame) != 0)
    {
        g_inflightCmd = g_desiredCmd;
        slopeUartTc375WriteFrame(&frame);
        g_lastQueuedCmd = g_desiredCmd;
    }
}

void slopeUartTc375TxInit(void)
{
    IfxAsclin_Asc_Config asc_config;

    IfxAsclin_Asc_initModuleConfig(&asc_config, SLOPE_UART_TC375_ASC_MODULE);

    asc_config.baudrate.baudrate = SLOPE_UART_TC375_ASC_BAUDRATE;
    asc_config.interrupt.txPriority = SLOPE_UART_TC375_ASC_TX_ISR_PRIORITY;
    asc_config.interrupt.rxPriority = SLOPE_UART_TC375_ASC_RX_ISR_PRIORITY;
    asc_config.interrupt.erPriority = SLOPE_UART_TC375_ASC_ER_ISR_PRIORITY;
    asc_config.interrupt.typeOfService = IfxSrc_Tos_cpu0;

    asc_config.txBuffer = g_slopeUartTc375AscTxBuffer;
    asc_config.txBufferSize = SLOPE_UART_TC375_ASC_TX_BUFFER_SIZE;
    asc_config.rxBuffer = g_slopeUartTc375AscRxBuffer;
    asc_config.rxBufferSize = SLOPE_UART_TC375_ASC_RX_BUFFER_SIZE;

    {
        static const IfxAsclin_Asc_Pins pins =
        {
            NULL_PTR,
            IfxPort_InputMode_pullUp,
            &SLOPE_UART_TC375_ASC_RX_PIN,
            IfxPort_InputMode_pullUp,
            NULL_PTR,
            IfxPort_OutputMode_pushPull,
            &SLOPE_UART_TC375_ASC_TX_PIN,
            IfxPort_OutputMode_pushPull,
            IfxPort_PadDriver_cmosAutomotiveSpeed1
        };

        asc_config.pins = &pins;
    }

    g_slopeUartTicksPerMs = (uint32_t)(IfxScuCcu_getStmFrequency() / 1000u);
    if (g_slopeUartTicksPerMs == 0u)
    {
        g_slopeUartTicksPerMs = 1u;
    }

    IfxAsclin_Asc_initModule(&g_slopeUartTc375Asc, &asc_config);

    slopeUartSenderInit(
        &g_slopeUartSender,
        SLOPE_UART_DEFAULT_ACK_TIMEOUT_MS,
        SLOPE_UART_DEFAULT_RETRY_LIMIT
    );

    slopeUartParserInit(&g_slopeUartAckParser);

    g_desiredCmd = slopeUartMakeCmd(SLOPE_UART_REQ_NONE);
    g_lastQueuedCmd = 0xFFu;
    g_slopeUartInitialized = 1u;
}

void slopeUartTc375TxPoll(void)
{
    if (g_slopeUartInitialized == 0u)
    {
        return;
    }

    slopeUartTc375ProcessRx();
    slopeUartTc375PollRetry();
    slopeUartTc375TrySendDesired();
}

void slopeUartTc375RequestNone(void)
{
    g_desiredCmd = slopeUartMakeCmd(SLOPE_UART_REQ_NONE);
}

void slopeUartTc375RequestOpen(void)
{
    g_desiredCmd = slopeUartMakeCmd(SLOPE_UART_REQ_OPEN);
}

void slopeUartTc375RequestClose(void)
{
    g_desiredCmd = slopeUartMakeCmd(SLOPE_UART_REQ_CLOSE);
}

uint8_t slopeUartTc375TxIsBusy(void)
{
    return slopeUartSenderIsBusy(&g_slopeUartSender);
}

uint8_t slopeUartTc375ConsumeTxSuccess(uint8_t* out_cmd)
{
    if (out_cmd == NULL_PTR)
    {
        return 0u;
    }

    if (g_txSuccessPending == 0u)
    {
        return 0u;
    }

    *out_cmd = g_txSuccessCmd;
    g_txSuccessPending = 0u;
    return 1u;
}