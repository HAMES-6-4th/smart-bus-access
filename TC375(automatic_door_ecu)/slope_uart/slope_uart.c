#include "slope_uart.h"

enum
{
    SLOPE_UART_PARSER_WAIT_SOF0 = 0,
    SLOPE_UART_PARSER_WAIT_SOF1,
    SLOPE_UART_PARSER_WAIT_TYPE,
    SLOPE_UART_PARSER_WAIT_SEQ,
    SLOPE_UART_PARSER_WAIT_CMD,
    SLOPE_UART_PARSER_WAIT_CRC
};

uint8_t slopeUartMakeCmd(uint8_t slope_req)
{
    return (uint8_t)(slope_req & 0x03u);
}

uint8_t slopeUartCmdGetSlope(uint8_t cmd)
{
    return (uint8_t)(cmd & 0x03u);
}

uint8_t slopeUartCmdIsValid(uint8_t cmd)
{
    uint8_t slope;

    if ((cmd & 0xFCu) != 0u)
    {
        return 0u;
    }

    slope = slopeUartCmdGetSlope(cmd);

    if (slope > SLOPE_UART_REQ_CLOSE)
    {
        return 0u;
    }

    return 1u;
}

uint8_t slopeUartCrc8(const uint8_t* data, size_t len)
{
    uint8_t crc = 0x00u;
    size_t i;
    uint8_t bit;

    for (i = 0; i < len; ++i)
    {
        crc ^= data[i];

        for (bit = 0; bit < 8u; ++bit)
        {
            if ((crc & 0x80u) != 0u)
            {
                crc = (uint8_t)((crc << 1) ^ 0x07u);
            }
            else
            {
                crc <<= 1;
            }
        }
    }

    return crc;
}

void slopeUartEncodeData(uint8_t seq, uint8_t cmd, SlopeUartFrame* out_frame)
{
    uint8_t crc_input[3];

    out_frame->bytes[0] = SLOPE_UART_SOF0;
    out_frame->bytes[1] = SLOPE_UART_SOF1;
    out_frame->bytes[2] = SLOPE_UART_TYPE_DATA;
    out_frame->bytes[3] = seq;
    out_frame->bytes[4] = cmd;

    crc_input[0] = out_frame->bytes[2];
    crc_input[1] = out_frame->bytes[3];
    crc_input[2] = out_frame->bytes[4];

    out_frame->bytes[5] = slopeUartCrc8(crc_input, 3u);
}

void slopeUartEncodeAck(uint8_t seq, SlopeUartFrame* out_frame)
{
    uint8_t crc_input[3];

    out_frame->bytes[0] = SLOPE_UART_SOF0;
    out_frame->bytes[1] = SLOPE_UART_SOF1;
    out_frame->bytes[2] = SLOPE_UART_TYPE_ACK;
    out_frame->bytes[3] = seq;
    out_frame->bytes[4] = 0x00u;

    crc_input[0] = out_frame->bytes[2];
    crc_input[1] = out_frame->bytes[3];
    crc_input[2] = out_frame->bytes[4];

    out_frame->bytes[5] = slopeUartCrc8(crc_input, 3u);
}

void slopeUartParserInit(SlopeUartParser* parser)
{
    parser->state = SLOPE_UART_PARSER_WAIT_SOF0;
    parser->type = 0u;
    parser->seq = 0u;
    parser->cmd = 0u;
}

int slopeUartParserFeed(SlopeUartParser* parser, uint8_t byte, SlopeUartDecodedFrame* out_frame)
{
    uint8_t crc_input[3];
    uint8_t crc;

    switch (parser->state)
    {
    case SLOPE_UART_PARSER_WAIT_SOF0:
        if (byte == SLOPE_UART_SOF0)
        {
            parser->state = SLOPE_UART_PARSER_WAIT_SOF1;
        }
        return SLOPE_UART_PARSE_NONE;

    case SLOPE_UART_PARSER_WAIT_SOF1:
				if (byte == SLOPE_UART_SOF1)
				{
						parser->state = SLOPE_UART_PARSER_WAIT_TYPE;
				}
				else if (byte == SLOPE_UART_SOF0)
				{
						parser->state = SLOPE_UART_PARSER_WAIT_SOF1;
				}
				else
				{
						parser->state = SLOPE_UART_PARSER_WAIT_SOF0;
				}
				return SLOPE_UART_PARSE_NONE;

    case SLOPE_UART_PARSER_WAIT_TYPE:
        parser->type = byte;
        parser->state = SLOPE_UART_PARSER_WAIT_SEQ;
        return SLOPE_UART_PARSE_NONE;

    case SLOPE_UART_PARSER_WAIT_SEQ:
        parser->seq = byte;
        parser->state = SLOPE_UART_PARSER_WAIT_CMD;
        return SLOPE_UART_PARSE_NONE;

    case SLOPE_UART_PARSER_WAIT_CMD:
        parser->cmd = byte;
        parser->state = SLOPE_UART_PARSER_WAIT_CRC;
        return SLOPE_UART_PARSE_NONE;

    case SLOPE_UART_PARSER_WAIT_CRC:
				crc_input[0] = parser->type;
				crc_input[1] = parser->seq;
				crc_input[2] = parser->cmd;
				crc = slopeUartCrc8(crc_input, 3u);

				parser->state = SLOPE_UART_PARSER_WAIT_SOF0;

				if (crc != byte)
				{
						return SLOPE_UART_PARSE_ERROR;
				}

				if ((parser->type != SLOPE_UART_TYPE_DATA) &&
						(parser->type != SLOPE_UART_TYPE_ACK))
				{
						return SLOPE_UART_PARSE_ERROR;
				}

				out_frame->type = parser->type;
				out_frame->seq = parser->seq;
				out_frame->cmd = parser->cmd;

				return SLOPE_UART_PARSE_OK;

    default:
        parser->state = SLOPE_UART_PARSER_WAIT_SOF0;
        return SLOPE_UART_PARSE_ERROR;
    }
}

void slopeUartSenderInit(SlopeUartSender* sender, uint32_t ack_timeout_ms, uint8_t retry_limit)
{
    sender->waiting_ack = 0u;
    sender->pending_seq = 0u;
    sender->retry_count = 0u;
    sender->retry_limit = (retry_limit == 0u) ? SLOPE_UART_DEFAULT_RETRY_LIMIT : retry_limit;
    sender->next_seq = 0u;
    sender->ack_timeout_ms = (ack_timeout_ms == 0u) ? SLOPE_UART_DEFAULT_ACK_TIMEOUT_MS : ack_timeout_ms;
    sender->last_tx_ms = 0u;
}

uint8_t slopeUartSenderIsBusy(const SlopeUartSender* sender)
{
    return sender->waiting_ack;
}

int slopeUartSenderStart(
    SlopeUartSender* sender,
    uint8_t cmd,
    uint32_t now_ms,
    SlopeUartFrame* out_frame
)
{
    uint8_t seq;

    if (sender->waiting_ack != 0u)
    {
        return 0;
    }

    if (slopeUartCmdIsValid(cmd) == 0u)
    {
        return 0;
    }

    seq = sender->next_seq;
    sender->next_seq = (uint8_t)(sender->next_seq + 1u);

    slopeUartEncodeData(seq, cmd, &sender->pending_frame);

    sender->waiting_ack = 1u;
    sender->pending_seq = seq;
    sender->retry_count = 0u;
    sender->last_tx_ms = now_ms;

    *out_frame = sender->pending_frame;
    return 1;
}

int slopeUartSenderOnFrame(
    SlopeUartSender* sender,
    const SlopeUartDecodedFrame* rx_frame
)
{
    if (sender->waiting_ack == 0u)
    {
        return 0;
    }

    if (rx_frame->type != SLOPE_UART_TYPE_ACK)
    {
        return 0;
    }

    if (rx_frame->seq != sender->pending_seq)
    {
        return 0;
    }

    sender->waiting_ack = 0u;
    return 1;
}

int slopeUartSenderPoll(
    SlopeUartSender* sender,
    uint32_t now_ms,
    SlopeUartFrame* out_frame
)
{
    uint32_t elapsed;

    if (sender->waiting_ack == 0u)
    {
        return SLOPE_UART_SENDER_POLL_NONE;
    }

    elapsed = (uint32_t)(now_ms - sender->last_tx_ms);
    if (elapsed < sender->ack_timeout_ms)
    {
        return SLOPE_UART_SENDER_POLL_NONE;
    }

    if (sender->retry_count < sender->retry_limit)
    {
        sender->retry_count = (uint8_t)(sender->retry_count + 1u);
        sender->last_tx_ms = now_ms;
        *out_frame = sender->pending_frame;
        return SLOPE_UART_SENDER_POLL_RESEND;
    }

    sender->waiting_ack = 0u;
    return SLOPE_UART_SENDER_POLL_GIVEUP;
}

void slopeUartReceiverInit(SlopeUartReceiver* receiver)
{
    receiver->has_last_seq = 0u;
    receiver->last_seq = 0u;
}

uint8_t slopeUartReceiverOnFrame(
    SlopeUartReceiver* receiver,
    const SlopeUartDecodedFrame* rx_frame,
    SlopeUartFrame* out_ack_frame,
    uint8_t* out_cmd_to_deliver
)
{
    uint8_t flags = SLOPE_UART_RX_NONE;

    if (rx_frame->type != SLOPE_UART_TYPE_DATA)
    {
        return SLOPE_UART_RX_NONE;
    }

    if (slopeUartCmdIsValid(rx_frame->cmd) == 0u)
    {
        return SLOPE_UART_RX_NONE;
    }

    slopeUartEncodeAck(rx_frame->seq, out_ack_frame);
    flags |= SLOPE_UART_RX_SEND_ACK;

    if ((receiver->has_last_seq == 0u) ||
        (receiver->last_seq != rx_frame->seq))
    {
        receiver->has_last_seq = 1u;
        receiver->last_seq = rx_frame->seq;
        *out_cmd_to_deliver = rx_frame->cmd;
        flags |= SLOPE_UART_RX_DELIVER;
    }

    return flags;
}