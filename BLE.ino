#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include "ble_uart.h"

static HardwareSerial TcUart(1);
static BLEScan* g_scan = nullptr;

static constexpr uint32_t kUartRxPin = 20;
static constexpr uint32_t kUartTxPin = 21;

static constexpr uint16_t kCompanyIdBoarding  = 0x1234;
static constexpr uint16_t kCompanyIdAlighting = 0x1235;

static constexpr uint8_t kBoardingSlopeReqCode   = 0xA1;
static constexpr uint8_t kAlightingBellCode      = 0xB1;
static constexpr uint8_t kAlightingSlopeBellCode = 0xB2;
static constexpr uint8_t kActiveCode             = 0x01;

static constexpr int      kMinRssi        = -55;
static constexpr uint32_t kDetectedHoldMs = 2000;

enum class ReceiveFilterMode : uint8_t
{
    BoardingOnly,
    AlightingOnly
};

static constexpr ReceiveFilterMode kReceiveFilterMode = ReceiveFilterMode::BoardingOnly;
// static constexpr ReceiveFilterMode kReceiveFilterMode =
//    ReceiveFilterMode::AlightingOnly;

enum class SignalType : uint8_t
{
    None = 0,
    BoardingSlopeRequest,
    AlightingBell,
    AlightingSlopeBell
};

static BleUartSender g_sender;
static BleUartParser g_uart_rx_parser;

static volatile uint32_t g_last_seen_ms = 0;
static volatile uint8_t  g_last_detected_cmd = bleUartMakeCmd(BLE_UART_REQ_NOP, BLE_UART_REQ_NOP);

static uint8_t g_last_queued_cmd = 0xFFu;  // 강제로 첫 전송 유도

static const char* filterModeToString(ReceiveFilterMode mode)
{
    switch (mode)
    {
    case ReceiveFilterMode::BoardingOnly:
        return "BOARDING_ONLY";
    case ReceiveFilterMode::AlightingOnly:
        return "ALIGHTING_ONLY";
    default:
        return "UNKNOWN";
    }
}

static SignalType decodeSignalFromManufacturerData(const uint8_t* p, size_t len)
{
    uint16_t companyId;
    uint8_t code;
    uint8_t active;

    if (p == nullptr || len < 4)
    {
        return SignalType::None;
    }

    companyId = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
    code = p[2];
    active = p[3];

    if (active != kActiveCode)
    {
        return SignalType::None;
    }

    if (companyId == kCompanyIdBoarding && code == kBoardingSlopeReqCode)
    {
        return SignalType::BoardingSlopeRequest;
    }

    if (companyId == kCompanyIdAlighting && code == kAlightingBellCode)
    {
        return SignalType::AlightingBell;
    }

    if (companyId == kCompanyIdAlighting && code == kAlightingSlopeBellCode)
    {
        return SignalType::AlightingSlopeBell;
    }

    return SignalType::None;
}

static bool matchesReceiveFilter(SignalType signal)
{
    switch (kReceiveFilterMode)
    {
    case ReceiveFilterMode::BoardingOnly:
        return signal == SignalType::BoardingSlopeRequest;

    case ReceiveFilterMode::AlightingOnly:
        return (signal == SignalType::AlightingBell) ||
               (signal == SignalType::AlightingSlopeBell);

    default:
        return false;
    }
}

static uint8_t convertSignalToCmd(SignalType signal)
{
    switch (signal)
    {
    case SignalType::BoardingSlopeRequest:
        /* 승차 슬로프 요청 -> slope open */
        return bleUartMakeCmd(BLE_UART_REQ_NOP, BLE_UART_REQ_OPEN);

    case SignalType::AlightingBell:
        /* 하차벨 -> door open */
        return bleUartMakeCmd(BLE_UART_REQ_OPEN, BLE_UART_REQ_NOP);

    case SignalType::AlightingSlopeBell:
        /* 슬로프 전개 하차벨 -> door + slope */
        return bleUartMakeCmd(BLE_UART_REQ_OPEN, BLE_UART_REQ_OPEN);

    default:
        return bleUartMakeCmd(BLE_UART_REQ_NOP, BLE_UART_REQ_NOP);
    }
}

static void uartWriteFrame(const BleUartFrame& frame)
{
    TcUart.write(frame.bytes, BLE_UART_FRAME_SIZE);
    TcUart.flush();
}

static void uartProcessRx()
{
    while (TcUart.available() > 0)
    {
        int raw = TcUart.read();
        BleUartDecodedFrame decoded;
        int rc;

        if (raw < 0)
        {
            break;
        }

        rc = bleUartParserFeed(&g_uart_rx_parser, (uint8_t)raw, &decoded);
        if (rc == BLE_UART_PARSE_OK)
        {
            if (decoded.type == BLE_UART_TYPE_ACK)
            {
                if (bleUartSenderOnFrame(&g_sender, &decoded) != 0)
                {
                    Serial.print("ACK received, seq=");
                    Serial.println(decoded.seq);
                }
            }
        }
    }
}

static void uartTrySendDesiredState(uint8_t desiredCmd)
{
    BleUartFrame frame;

    if (bleUartSenderIsBusy(&g_sender) != 0u)
    {
        return;
    }

    if (desiredCmd == g_last_queued_cmd)
    {
        return;
    }

    if (bleUartSenderStart(&g_sender, desiredCmd, millis(), &frame) != 0)
    {
        uartWriteFrame(frame);
        g_last_queued_cmd = desiredCmd;

        Serial.print("DATA sent, cmd=0x");
        Serial.println(desiredCmd, HEX);
    }
}

static void uartPollRetry()
{
    BleUartFrame frame;
    int rc = bleUartSenderPoll(&g_sender, millis(), &frame);

    if (rc == BLE_UART_SENDER_POLL_RESEND)
    {
        uartWriteFrame(frame);
        Serial.println("DATA resent");
    }
    else if (rc == BLE_UART_SENDER_POLL_GIVEUP)
    {
        /* 강제로 다음 loop에서 현재 desired state 다시 시도 */
        g_last_queued_cmd = 0xFFu;
        Serial.println("ACK timeout: give up current frame, will retry current desired state");
    }
}

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks
{
    void onResult(BLEAdvertisedDevice advertisedDevice) override
    {
        String md;
        const uint8_t* p;
        SignalType signal;
        int rssi;

        if (!advertisedDevice.haveManufacturerData())
        {
            return;
        }

        md = advertisedDevice.getManufacturerData();
        if (md.length() < 4)
        {
            return;
        }

        p = reinterpret_cast<const uint8_t*>(md.c_str());
        signal = decodeSignalFromManufacturerData(p, (size_t)md.length());

        if (signal == SignalType::None)
        {
            return;
        }

        if (!matchesReceiveFilter(signal))
        {
            return;
        }

        rssi = advertisedDevice.getRSSI();
        if (rssi < kMinRssi)
        {
            return;
        }

        g_last_detected_cmd = convertSignalToCmd(signal);
        g_last_seen_ms = millis();
    }
};

void setup()
{
    Serial.begin(115200);
    delay(500);

    TcUart.begin(115200, SERIAL_8N1, kUartRxPin, kUartTxPin);

    bleUartSenderInit(&g_sender, BLE_UART_DEFAULT_ACK_TIMEOUT_MS, BLE_UART_DEFAULT_RETRY_LIMIT);
    bleUartParserInit(&g_uart_rx_parser);

    BLEDevice::init("");
    g_scan = BLEDevice::getScan();
    g_scan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    g_scan->setActiveScan(false);
    g_scan->setInterval(100);
    g_scan->setWindow(99);

    Serial.print("BLE scanner started, filter=");
    Serial.println(filterModeToString(kReceiveFilterMode));
}

void loop()
{
    uint32_t now;
    uint32_t lastSeen;
    uint8_t desiredCmd;

    uartProcessRx();
    uartPollRetry();

    g_scan->start(1, false);
    g_scan->clearResults();

    now = millis();
    lastSeen = g_last_seen_ms;

    if (lastSeen != 0u && (now - lastSeen) <= kDetectedHoldMs)
    {
        desiredCmd = g_last_detected_cmd;
    }
    else
    {
        desiredCmd = bleUartMakeCmd(BLE_UART_REQ_NOP, BLE_UART_REQ_NOP);
    }

    uartTrySendDesiredState(desiredCmd);
}