// Copyright 2026
// SPDX-License-Identifier: Apache-2.0
//
// Phase 1 connection lifecycle: Hello/DeviceInfo/ListEntities/Ping/Disconnect
// only. Single connection at a time, blocking I/O in a dedicated task --
// mirrors the model components/wyoming_c_satellite already used
// successfully on this device, not a novel design.

#include "esphome_api/esphome_api.h"
#include "esphome_api/esphome_frame.h"
#include "esphome_api/esphome_proto.h"
#include "esphome_api/pb_wire.h"

#include <string.h>
#include <errno.h>
#include <lwip/sockets.h>
#include <aos/kernel.h>
#include <ulog/ulog.h>

#define TAG "esphome_api"

#define ESPB_PORT 6053
#define ESPB_RX_BUF_SIZE 512
#define ESPB_TX_BUF_SIZE 512

static esphome_api_device_info_t s_device_info;
static volatile bool s_va_subscribed = false;
static volatile uint32_t s_va_flags = 0;

void esphome_api_set_device_info(const esphome_api_device_info_t *info)
{
    s_device_info = *info;
}

bool esphome_api_voice_assistant_subscribed(void)
{
    return s_va_subscribed;
}

uint32_t esphome_api_voice_assistant_flags(void)
{
    return s_va_flags;
}

static bool send_message(esphome_frame_io_t *io, uint32_t msg_type, const uint8_t *payload, size_t len)
{
    if (esphome_frame_write(io, msg_type, payload, len) != 0) {
        LOGW(TAG, "write failed for msg_type %u, errno=%d", (unsigned)msg_type, errno);
        return false;
    }
    return true;
}

static bool send_empty(esphome_frame_io_t *io, uint32_t msg_type)
{
    return send_message(io, msg_type, NULL, 0);
}

static void handle_hello(esphome_frame_io_t *io, const uint8_t *payload, size_t len)
{
    // We don't need anything out of HelloRequest right now beyond logging
    // who's connecting; decode just enough to do that.
    pb_reader_t r;
    pb_reader_init(&r, payload, len);
    pb_field_t f;
    char client_info[48] = {0};
    while (pb_reader_next_field(&r, &f)) {
        if (f.field_number == ESPB_HELLO_REQ_F_CLIENT_INFO && f.wire_type == PB_WIRE_TYPE_LEN) {
            pb_field_to_cstr(&f, client_info, sizeof(client_info));
        }
    }
    LOGI(TAG, "HelloRequest from '%s'", client_info);

    uint8_t buf[ESPB_TX_BUF_SIZE];
    size_t n = 0;
    // API version 1.10: high enough that HA doesn't warn about an ancient
    // client, without claiming a specific ESPHome release's exact feature
    // set.
    pbw_write_varint_field(buf, sizeof(buf), &n, ESPB_HELLO_RESP_F_API_VER_MAJOR, 1);
    pbw_write_varint_field(buf, sizeof(buf), &n, ESPB_HELLO_RESP_F_API_VER_MINOR, 10);
    pbw_write_string_field(buf, sizeof(buf), &n, ESPB_HELLO_RESP_F_SERVER_INFO, s_device_info.version);
    pbw_write_string_field(buf, sizeof(buf), &n, ESPB_HELLO_RESP_F_NAME, s_device_info.name);
    send_message(io, ESPB_MSG_HELLO_RESPONSE, buf, n);
}

// We advertise uses_password=false in DeviceInfoResponse, but aioesphomeapi
// still sends ConnectRequest regardless and expects a matching
// ConnectResponse per the protocol spec. Found unhandled (logged as
// "unhandled msg_type 3") while first testing against real HA 2026.9.2 --
// the connection stayed usable without it in that test, but this closes the
// gap rather than relying on that being true across client versions.
static void handle_connect(esphome_frame_io_t *io)
{
    uint8_t buf[8];
    size_t n = 0;
    pbw_write_bool_field(buf, sizeof(buf), &n, ESPB_CONNECT_RESP_F_INVALID_PASSWORD, false);
    send_message(io, ESPB_MSG_CONNECT_RESPONSE, buf, n);
}

static void handle_subscribe_voice_assistant(const uint8_t *payload, size_t len)
{
    pb_reader_t r;
    pb_reader_init(&r, payload, len);
    pb_field_t f;
    bool subscribe = false;
    uint32_t flags = 0;
    while (pb_reader_next_field(&r, &f)) {
        if (f.field_number == ESPB_SUB_VA_F_SUBSCRIBE && f.wire_type == PB_WIRE_TYPE_VARINT) {
            subscribe = f.varint_value != 0;
        } else if (f.field_number == ESPB_SUB_VA_F_FLAGS && f.wire_type == PB_WIRE_TYPE_VARINT) {
            flags = (uint32_t)f.varint_value;
        }
    }
    s_va_subscribed = subscribe;
    s_va_flags = subscribe ? flags : 0;
    LOGI(TAG, "SubscribeVoiceAssistantRequest: subscribe=%d flags=0x%x", (int)subscribe, (unsigned)flags);
}

// Reports the one fixed, built-in on-device wake word (see the "alexa"
// wsat_wake config in app/src/wyoming/wyoming.c) -- this device has no
// runtime-selectable wake-word set, so available == active, always. Without
// this response HA's ESPHome integration leaves the "Aktivierungswort" and
// "Assist-Satellit" entities stuck on "unavailable" (observed against real
// HA 2026.9.2).
static void handle_voice_assistant_config(esphome_frame_io_t *io)
{
    uint8_t ww_buf[64];
    size_t ww_n = 0;
    pbw_write_string_field(ww_buf, sizeof(ww_buf), &ww_n, ESPB_VA_WAKE_WORD_F_ID, "alexa");
    pbw_write_string_field(ww_buf, sizeof(ww_buf), &ww_n, ESPB_VA_WAKE_WORD_F_WAKE_WORD, "Alexa");
    pbw_write_string_field(ww_buf, sizeof(ww_buf), &ww_n, ESPB_VA_WAKE_WORD_F_TRAINED_LANGUAGES, "en");

    uint8_t buf[ESPB_TX_BUF_SIZE];
    size_t n = 0;
    pbw_write_submessage_field(buf, sizeof(buf), &n, ESPB_VA_CONFIG_RESP_F_AVAILABLE_WAKE_WORDS, ww_buf, ww_n);
    pbw_write_string_field(buf, sizeof(buf), &n, ESPB_VA_CONFIG_RESP_F_ACTIVE_WAKE_WORDS, "alexa");
    pbw_write_varint_field(buf, sizeof(buf), &n, ESPB_VA_CONFIG_RESP_F_MAX_ACTIVE_WAKE_WORDS, 1);
    send_message(io, ESPB_MSG_VOICE_ASSISTANT_CONFIG_RESPONSE, buf, n);
}

static void handle_device_info(esphome_frame_io_t *io)
{
    uint8_t buf[ESPB_TX_BUF_SIZE];
    size_t n = 0;
    pbw_write_bool_field(buf, sizeof(buf), &n, ESPB_DEVINFO_F_USES_PASSWORD, false);
    pbw_write_string_field(buf, sizeof(buf), &n, ESPB_DEVINFO_F_NAME, s_device_info.name);
    pbw_write_string_field(buf, sizeof(buf), &n, ESPB_DEVINFO_F_MAC_ADDRESS, s_device_info.mac_address);
    pbw_write_string_field(buf, sizeof(buf), &n, ESPB_DEVINFO_F_ESPHOME_VERSION, s_device_info.version);
    pbw_write_string_field(buf, sizeof(buf), &n, ESPB_DEVINFO_F_MODEL, s_device_info.model);
    pbw_write_string_field(buf, sizeof(buf), &n, ESPB_DEVINFO_F_MANUFACTURER, s_device_info.manufacturer);
    pbw_write_string_field(buf, sizeof(buf), &n, ESPB_DEVINFO_F_FRIENDLY_NAME, s_device_info.friendly_name);
    // Phase 1: advertised so HA recognizes this as a voice satellite once we
    // add the actual VoiceAssistantRequest/Event flow; no behavior yet.
    uint32_t va_flags = ESPB_VA_FEATURE_VOICE_ASSISTANT | ESPB_VA_FEATURE_SPEAKER |
                         ESPB_VA_FEATURE_API_AUDIO | ESPB_VA_FEATURE_ANNOUNCE |
                         ESPB_VA_FEATURE_START_CONVERSATION;
    pbw_write_varint_field(buf, sizeof(buf), &n, ESPB_DEVINFO_F_VOICE_ASSISTANT_FEAT_FLAGS, va_flags);
    send_message(io, ESPB_MSG_DEVICE_INFO_RESPONSE, buf, n);
}

// Runs one accepted connection to completion (until the peer disconnects or
// a protocol/socket error occurs). Never returns early leaving the fd open.
static void handle_connection(int connfd)
{
    esphome_frame_io_t io = { .fd = connfd };
    static uint8_t rx_buf[ESPB_RX_BUF_SIZE];

    LOGI(TAG, "client connected");

    while (1) {
        uint32_t msg_type;
        size_t payload_len;
        int rc = esphome_frame_read(&io, &msg_type, rx_buf, sizeof(rx_buf), &payload_len);
        if (rc == -1) {
            LOGI(TAG, "connection closed (errno=%d)", errno);
            break;
        }
        if (rc == -2) {
            LOGW(TAG, "malformed/oversized frame, closing connection");
            break;
        }

        switch (msg_type) {
        case ESPB_MSG_HELLO_REQUEST:
            handle_hello(&io, rx_buf, payload_len);
            break;
        case ESPB_MSG_CONNECT_REQUEST:
            handle_connect(&io);
            break;
        case ESPB_MSG_DEVICE_INFO_REQUEST:
            handle_device_info(&io);
            break;
        case ESPB_MSG_SUBSCRIBE_VOICE_ASSISTANT_REQ:
            handle_subscribe_voice_assistant(rx_buf, payload_len);
            break;
        case ESPB_MSG_VOICE_ASSISTANT_CONFIG_REQUEST:
            handle_voice_assistant_config(&io);
            break;
        case ESPB_MSG_LIST_ENTITIES_REQUEST:
            // No native entities yet (MQTT still owns restart/LED/volume) --
            // an immediately-empty entity list is a normal, valid response.
            send_empty(&io, ESPB_MSG_LIST_ENTITIES_DONE_RESPONSE);
            break;
        case ESPB_MSG_PING_REQUEST:
            send_empty(&io, ESPB_MSG_PING_RESPONSE);
            break;
        case ESPB_MSG_DISCONNECT_REQUEST:
            send_empty(&io, ESPB_MSG_DISCONNECT_RESPONSE);
            LOGI(TAG, "client requested disconnect");
            s_va_subscribed = false;
            s_va_flags = 0;
            lwip_close(connfd);
            return;
        default:
            LOGD(TAG, "unhandled msg_type %u (%u bytes), ignoring", (unsigned)msg_type, (unsigned)payload_len);
            break;
        }
    }

    // Also reached on an abrupt close (rc == -1/-2 above) -- a client that
    // drops the TCP connection without a DisconnectRequest must not leave a
    // stale subscription behind for the next connection to inherit.
    s_va_subscribed = false;
    s_va_flags = 0;
    lwip_close(connfd);
}

static void esphome_server_task(void *arg)
{
    (void)arg;

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        LOGE(TAG, "socket() failed");
        return;
    }

    const int enable = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(ESPB_PORT);

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOGE(TAG, "bind() failed, errno=%d", errno);
        lwip_close(sockfd);
        return;
    }

    if (listen(sockfd, 1) < 0) {
        LOGE(TAG, "listen() failed, errno=%d", errno);
        lwip_close(sockfd);
        return;
    }

    LOGI(TAG, "listening on port %d", ESPB_PORT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int connfd = accept(sockfd, (struct sockaddr *)&client_addr, &client_len);
        if (connfd < 0) {
            if (errno == EINTR) {
                continue;
            }
            LOGW(TAG, "accept() failed, errno=%d", errno);
            // A persistent (non-EINTR) accept() error would otherwise retry
            // in a tight loop with no blocking syscall in between -- a
            // busy-loop at AOS_DEFAULT_APP_PRI that could starve other
            // tasks at the same priority. Found while debugging the
            // 2026-09-26 hang (not confirmed as the cause, but a real bug
            // regardless).
            aos_msleep(200);
            continue;
        }
        // One connection at a time, same as the Wyoming satellite server --
        // HA only ever opens one API connection per device anyway.
        handle_connection(connfd);
    }
}

void esphome_api_start(void)
{
    aos_task_t task_handle;
    aos_task_new_ext(&task_handle, "esphome_api", esphome_server_task, NULL, 4096, AOS_DEFAULT_APP_PRI);
}
