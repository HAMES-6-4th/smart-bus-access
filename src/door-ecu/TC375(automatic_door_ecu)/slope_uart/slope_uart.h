#ifndef SLOPE_UART_H
#define SLOPE_UART_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

#define SLOPE_UART_SOF0              ((uint8_t)0xA5u)
#define SLOPE_UART_SOF1              ((uint8_t)0x5Au)
#define SLOPE_UART_FRAME_SIZE        ((uint8_t)6u)

#define SLOPE_UART_TYPE_DATA         ((uint8_t)0x01u)
#define SLOPE_UART_TYPE_ACK          ((uint8_t)0x02u)

#define SLOPE_UART_REQ_NONE          ((uint8_t)0x00u)
#define SLOPE_UART_REQ_OPEN          ((uint8_t)0x01u)
#define SLOPE_UART_REQ_CLOSE         ((uint8_t)0x02u)

#define SLOPE_UART_DEFAULT_ACK_TIMEOUT_MS  ((uint32_t)20u)
#define SLOPE_UART_DEFAULT_RETRY_LIMIT     ((uint8_t)5u)

#define SLOPE_UART_PARSE_NONE        (0)
#define SLOPE_UART_PARSE_OK          (1)
#define SLOPE_UART_PARSE_ERROR       (-1)

#define SLOPE_UART_RX_NONE           ((uint8_t)0x00u)
#define SLOPE_UART_RX_SEND_ACK       ((uint8_t)0x01u)
#define SLOPE_UART_RX_DELIVER        ((uint8_t)0x02u)

#define SLOPE_UART_SENDER_POLL_NONE    (0)
#define SLOPE_UART_SENDER_POLL_RESEND  (1)
#define SLOPE_UART_SENDER_POLL_GIVEUP  (2)

typedef struct SlopeUartFrameStruct
{
    uint8_t bytes[SLOPE_UART_FRAME_SIZE];
} SlopeUartFrame;

typedef struct SlopeUartDecodedFrameStruct
{
    uint8_t type;
    uint8_t seq;
    uint8_t cmd;
} SlopeUartDecodedFrame;

typedef struct SlopeUartParserStruct
{
    uint8_t state;
    uint8_t type;
    uint8_t seq;
    uint8_t cmd;
} SlopeUartParser;

typedef struct SlopeUartSenderStruct
{
    uint8_t waiting_ack;
    uint8_t pending_seq;
    uint8_t retry_count;
    uint8_t retry_limit;
    uint8_t next_seq;
    uint32_t ack_timeout_ms;
    uint32_t last_tx_ms;
    SlopeUartFrame pending_frame;
} SlopeUartSender;

typedef struct SlopeUartReceiverStruct
{
    uint8_t has_last_seq;
    uint8_t last_seq;
} SlopeUartReceiver;

/* ---------- command helpers ---------- */

uint8_t slopeUartMakeCmd(uint8_t slope_req);
uint8_t slopeUartCmdGetSlope(uint8_t cmd);
uint8_t slopeUartCmdIsValid(uint8_t cmd);

/* ---------- crc / encoding ---------- */

uint8_t slopeUartCrc8(const uint8_t* data, size_t len);
void slopeUartEncodeData(uint8_t seq, uint8_t cmd, SlopeUartFrame* out_frame);
void slopeUartEncodeAck(uint8_t seq, SlopeUartFrame* out_frame);

/* ---------- parser ---------- */

void slopeUartParserInit(SlopeUartParser* parser);
int slopeUartParserFeed(SlopeUartParser* parser, uint8_t byte, SlopeUartDecodedFrame* out_frame);

/* ---------- sender ---------- */

void slopeUartSenderInit(SlopeUartSender* sender, uint32_t ack_timeout_ms, uint8_t retry_limit);
uint8_t slopeUartSenderIsBusy(const SlopeUartSender* sender);

int slopeUartSenderStart(
    SlopeUartSender* sender,
    uint8_t cmd,
    uint32_t now_ms,
    SlopeUartFrame* out_frame
);

int slopeUartSenderOnFrame(
    SlopeUartSender* sender,
    const SlopeUartDecodedFrame* rx_frame
);

int slopeUartSenderPoll(
    SlopeUartSender* sender,
    uint32_t now_ms,
    SlopeUartFrame* out_frame
);

/* ---------- receiver ---------- */

void slopeUartReceiverInit(SlopeUartReceiver* receiver);

uint8_t slopeUartReceiverOnFrame(
    SlopeUartReceiver* receiver,
    const SlopeUartDecodedFrame* rx_frame,
    SlopeUartFrame* out_ack_frame,
    uint8_t* out_cmd_to_deliver
);

#ifdef __cplusplus
}
#endif

#endif