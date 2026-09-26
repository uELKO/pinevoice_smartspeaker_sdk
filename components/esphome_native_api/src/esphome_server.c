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
// Bumped from 512 after a real pipeline run against HA closed the
// connection with "malformed/oversized frame" -- some inbound message (not
// yet pinned down exactly which) exceeded that. rx_buf is `static`, so this
// costs BSS, not the 4096-byte esphome_api task stack.
#define ESPB_RX_BUF_SIZE 4096
#define ESPB_TX_BUF_SIZE 512

static esphome_api_device_info_t s_device_info;
static volatile bool s_va_subscribed = false;
static volatile uint32_t s_va_flags = 0;

// Single-connection model (see esphome_server_task) -- connfd of the one
// currently connected client, or -1 if none. Lets esphome_api_send_voice_
// assistant_start() write onto the connection from a different task than
// the one blocked reading it in handle_connection().
static volatile int s_active_connfd = -1;
static aos_sem_t s_va_response_sem;
static bool s_va_response_sem_ready = false;
static volatile bool s_va_response_pending = false;
static volatile uint32_t s_va_response_port = 0;
static volatile bool s_va_response_error = false;

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

static void handle_voice_assistant_response(const uint8_t *payload, size_t len)
{
    pb_reader_t r;
    pb_reader_init(&r, payload, len);
    pb_field_t f;
    uint32_t port = 0;
    bool error = false;
    while (pb_reader_next_field(&r, &f)) {
        if (f.field_number == ESPB_VA_RESP_F_PORT && f.wire_type == PB_WIRE_TYPE_VARINT) {
            port = (uint32_t)f.varint_value;
        } else if (f.field_number == ESPB_VA_RESP_F_ERROR && f.wire_type == PB_WIRE_TYPE_VARINT) {
            error = f.varint_value != 0;
        }
    }
    LOGI(TAG, "VoiceAssistantResponse: port=%u error=%d", (unsigned)port, (int)error);
    s_va_response_port = port;
    s_va_response_error = error;
    if (s_va_response_pending) {
        s_va_response_pending = false;
        aos_sem_signal(&s_va_response_sem);
    }
}

bool esphome_api_send_voice_assistant_start(const char *conversation_id, uint32_t flags,
                                             uint32_t timeout_ms, uint32_t *out_port, bool *out_error)
{
    int connfd = s_active_connfd;
    if (connfd < 0) {
        LOGW(TAG, "send_voice_assistant_start: no active connection");
        return false;
    }
    if (!s_va_response_sem_ready) {
        aos_sem_new(&s_va_response_sem, 0);
        s_va_response_sem_ready = true;
    }
    // Drain any stale signal left over from a previous call that timed out
    // after its response finally arrived, so we don't return instantly on
    // a response that isn't actually ours.
    while (aos_sem_wait(&s_va_response_sem, 0) == 0) { }

    uint8_t buf[128];
    size_t n = 0;
    pbw_write_bool_field(buf, sizeof(buf), &n, ESPB_VA_REQ_F_START, true);
    pbw_write_string_field(buf, sizeof(buf), &n, ESPB_VA_REQ_F_CONVERSATION_ID, conversation_id ? conversation_id : "");
    pbw_write_varint_field(buf, sizeof(buf), &n, ESPB_VA_REQ_F_FLAGS, flags);

    esphome_frame_io_t io = { .fd = connfd };
    s_va_response_pending = true;
    if (!send_message(&io, ESPB_MSG_VOICE_ASSISTANT_REQUEST, buf, n)) {
        s_va_response_pending = false;
        return false;
    }

    if (aos_sem_wait(&s_va_response_sem, timeout_ms) != 0) {
        s_va_response_pending = false;
        LOGW(TAG, "send_voice_assistant_start: timed out waiting for VoiceAssistantResponse");
        return false;
    }

    if (out_port) *out_port = s_va_response_port;
    if (out_error) *out_error = s_va_response_error;
    return true;
}

// Generous headroom over the app's actual chunk size (1600 bytes today) --
// this is a generic component, so it doesn't know the app's chunking, just
// refuses anything that would overflow its static scratch buffer.
#define ESPB_VA_AUDIO_TX_MAX_CHUNK 4000

bool esphome_api_send_voice_assistant_audio(const uint8_t *data, size_t len, bool end)
{
    int connfd = s_active_connfd;
    if (connfd < 0) {
        return false;
    }
    if (len > ESPB_VA_AUDIO_TX_MAX_CHUNK) {
        LOGW(TAG, "send_voice_assistant_audio: chunk too large (%u > %u), dropping",
             (unsigned)len, (unsigned)ESPB_VA_AUDIO_TX_MAX_CHUNK);
        return false;
    }
    static uint8_t buf[ESPB_VA_AUDIO_TX_MAX_CHUNK + 16];
    size_t n = 0;
    if (data != NULL && len > 0) {
        pbw_write_bytes_field(buf, sizeof(buf), &n, ESPB_VA_AUDIO_F_DATA, data, len);
    }
    if (end) {
        pbw_write_bool_field(buf, sizeof(buf), &n, ESPB_VA_AUDIO_F_END, true);
    }
    esphome_frame_io_t io = { .fd = connfd };
    return send_message(&io, ESPB_MSG_VOICE_ASSISTANT_AUDIO, buf, n);
}

// Diagnostic only for now (see TODO.md) -- just logs event_type and any
// name/value data pairs so we can see *why* a pipeline run ends, instead of
// guessing from silence. Not yet wired into any LED/state behavior.
static void handle_voice_assistant_event(const uint8_t *payload, size_t len)
{
    pb_reader_t r;
    pb_reader_init(&r, payload, len);
    pb_field_t f;
    // proto3 elides a field encoding its default value, and 0 (VOICE_ASSISTANT_ERROR)
    // is event_type's default -- so an absent field here means ERROR, not "unknown".
    uint32_t event_type = ESPB_VA_EVENT_ERROR;
    char data_buf[192] = {0};
    size_t data_used = 0;

    while (pb_reader_next_field(&r, &f)) {
        if (f.field_number == ESPB_VA_EVT_F_EVENT_TYPE && f.wire_type == PB_WIRE_TYPE_VARINT) {
            event_type = (uint32_t)f.varint_value;
        } else if (f.field_number == ESPB_VA_EVT_F_DATA && f.wire_type == PB_WIRE_TYPE_LEN) {
            pb_reader_t sub;
            pb_reader_init(&sub, f.bytes_value, f.bytes_len);
            pb_field_t sf;
            char name[32] = {0};
            char value[96] = {0};
            while (pb_reader_next_field(&sub, &sf)) {
                if (sf.field_number == ESPB_VA_EVT_DATA_F_NAME && sf.wire_type == PB_WIRE_TYPE_LEN) {
                    pb_field_to_cstr(&sf, name, sizeof(name));
                } else if (sf.field_number == ESPB_VA_EVT_DATA_F_VALUE && sf.wire_type == PB_WIRE_TYPE_LEN) {
                    pb_field_to_cstr(&sf, value, sizeof(value));
                }
            }
            if (data_used < sizeof(data_buf)) {
                int written = snprintf(data_buf + data_used, sizeof(data_buf) - data_used,
                                        "%s%s=%s", data_used ? "," : "", name, value);
                if (written > 0) {
                    data_used += (size_t)written;
                }
            }
        }
    }
    LOGI(TAG, "VoiceAssistantEventResponse: event_type=%u data={%s}", (unsigned)event_type, data_buf);
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
    s_active_connfd = connfd;

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
        case ESPB_MSG_VOICE_ASSISTANT_RESPONSE:
            handle_voice_assistant_response(rx_buf, payload_len);
            break;
        case ESPB_MSG_VOICE_ASSISTANT_EVENT_RESPONSE:
            handle_voice_assistant_event(rx_buf, payload_len);
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
            s_active_connfd = -1;
            if (s_va_response_pending) {
                s_va_response_pending = false;
                aos_sem_signal(&s_va_response_sem);
            }
            lwip_close(connfd);
            return;
        default:
            LOGD(TAG, "unhandled msg_type %u (%u bytes), ignoring", (unsigned)msg_type, (unsigned)payload_len);
            break;
        }
    }

    // Also reached on an abrupt close (rc == -1/-2 above) -- a client that
    // drops the TCP connection without a DisconnectRequest must not leave a
    // stale subscription (or a sender blocked on a response that will never
    // arrive) behind for/after the next connection.
    s_va_subscribed = false;
    s_va_flags = 0;
    s_active_connfd = -1;
    if (s_va_response_pending) {
        s_va_response_pending = false;
        aos_sem_signal(&s_va_response_sem);
    }
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
