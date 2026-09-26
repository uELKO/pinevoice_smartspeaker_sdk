// Copyright 2026
// SPDX-License-Identifier: Apache-2.0
//
// Message type IDs and field numbers for the subset of ESPHome's native API
// (https://github.com/esphome/aioesphomeapi, api.proto) that this component
// implements. Verified against api.proto directly rather than guessed --
// keep these in sync if ESPHome ever renumbers a message we use (it hasn't
// historically).

#ifndef ESPHOME_API_ESPHOME_PROTO_H_
#define ESPHOME_API_ESPHOME_PROTO_H_

// --- Message type IDs (the varint after the length in a plaintext frame) --
#define ESPB_MSG_HELLO_REQUEST                  1
#define ESPB_MSG_HELLO_RESPONSE                 2
#define ESPB_MSG_DISCONNECT_REQUEST             5
#define ESPB_MSG_DISCONNECT_RESPONSE            6
#define ESPB_MSG_PING_REQUEST                   7
#define ESPB_MSG_PING_RESPONSE                  8
#define ESPB_MSG_DEVICE_INFO_REQUEST            9
#define ESPB_MSG_DEVICE_INFO_RESPONSE           10
#define ESPB_MSG_LIST_ENTITIES_REQUEST          11
#define ESPB_MSG_LIST_ENTITIES_DONE_RESPONSE    19
#define ESPB_MSG_SUBSCRIBE_VOICE_ASSISTANT_REQ  89
#define ESPB_MSG_VOICE_ASSISTANT_REQUEST        90
#define ESPB_MSG_VOICE_ASSISTANT_RESPONSE       91
#define ESPB_MSG_VOICE_ASSISTANT_EVENT_RESPONSE 92
#define ESPB_MSG_VOICE_ASSISTANT_AUDIO          106
#define ESPB_MSG_VOICE_ASSISTANT_ANNOUNCE_REQ   119
#define ESPB_MSG_VOICE_ASSISTANT_ANNOUNCE_DONE  120

// --- HelloRequest (client->server) field numbers --
#define ESPB_HELLO_REQ_F_CLIENT_INFO   1
#define ESPB_HELLO_REQ_F_API_VER_MAJOR 2
#define ESPB_HELLO_REQ_F_API_VER_MINOR 3

// --- HelloResponse (server->client) field numbers --
#define ESPB_HELLO_RESP_F_API_VER_MAJOR 1
#define ESPB_HELLO_RESP_F_API_VER_MINOR 2
#define ESPB_HELLO_RESP_F_SERVER_INFO   3
#define ESPB_HELLO_RESP_F_NAME          4

// --- DeviceInfoResponse (server->client) field numbers we populate --
// (DeviceInfoResponse has ~27 fields upstream; everything else is optional
// and we simply omit it -- proto3 unknown/absent fields are always safe to
// skip on both ends.)
#define ESPB_DEVINFO_F_USES_PASSWORD              1
#define ESPB_DEVINFO_F_NAME                       2
#define ESPB_DEVINFO_F_MAC_ADDRESS                3
#define ESPB_DEVINFO_F_ESPHOME_VERSION             4
#define ESPB_DEVINFO_F_MODEL                       6
#define ESPB_DEVINFO_F_MANUFACTURER                12
#define ESPB_DEVINFO_F_FRIENDLY_NAME                13
#define ESPB_DEVINFO_F_VOICE_ASSISTANT_FEAT_FLAGS   17

// --- SubscribeVoiceAssistantRequest (client->server) field numbers --
#define ESPB_SUB_VA_F_SUBSCRIBE 1
#define ESPB_SUB_VA_F_FLAGS     2

// --- VoiceAssistantRequest (device->server) field numbers --
#define ESPB_VA_REQ_F_START           1
#define ESPB_VA_REQ_F_CONVERSATION_ID 2
#define ESPB_VA_REQ_F_FLAGS           3
// field 4 (audio_settings) and 5 (wake_word_phrase) are not sent by us yet.

// --- VoiceAssistantResponse (server->device) field numbers --
#define ESPB_VA_RESP_F_PORT  1
#define ESPB_VA_RESP_F_ERROR 2

// --- VoiceAssistantEventResponse (server->device) field numbers --
#define ESPB_VA_EVT_F_EVENT_TYPE 1
#define ESPB_VA_EVT_F_DATA       2 // repeated VoiceAssistantEventData (submessage)
// VoiceAssistantEventData submessage field numbers:
#define ESPB_VA_EVT_DATA_F_NAME  1
#define ESPB_VA_EVT_DATA_F_VALUE 2

// --- VoiceAssistantAudio (both directions) field numbers --
#define ESPB_VA_AUDIO_F_DATA 1
#define ESPB_VA_AUDIO_F_END  2

// --- VoiceAssistantAnnounceRequest (server->device) field numbers --
#define ESPB_VA_ANNOUNCE_F_MEDIA_ID             1
#define ESPB_VA_ANNOUNCE_F_TEXT                 2
#define ESPB_VA_ANNOUNCE_F_PREANNOUNCE_MEDIA_ID  3
#define ESPB_VA_ANNOUNCE_F_START_CONVERSATION    4

// --- VoiceAssistantAnnounceFinished (device->server) field numbers --
#define ESPB_VA_ANNOUNCE_DONE_F_SUCCESS 1

// VoiceAssistantEvent enum (event_type in VoiceAssistantEventResponse)
#define ESPB_VA_EVENT_ERROR             0
#define ESPB_VA_EVENT_RUN_START         1
#define ESPB_VA_EVENT_RUN_END           2
#define ESPB_VA_EVENT_STT_START         3
#define ESPB_VA_EVENT_STT_END           4
#define ESPB_VA_EVENT_INTENT_START      5
#define ESPB_VA_EVENT_INTENT_END        6
#define ESPB_VA_EVENT_TTS_START         7
#define ESPB_VA_EVENT_TTS_END           8
#define ESPB_VA_EVENT_WAKE_WORD_START   9
#define ESPB_VA_EVENT_WAKE_WORD_END     10
#define ESPB_VA_EVENT_STT_VAD_START     11
#define ESPB_VA_EVENT_STT_VAD_END       12
#define ESPB_VA_EVENT_TTS_STREAM_START  98
#define ESPB_VA_EVENT_TTS_STREAM_END    99
#define ESPB_VA_EVENT_INTENT_PROGRESS   100

// VoiceAssistantSubscribeFlag (flags in SubscribeVoiceAssistantRequest)
#define ESPB_VA_SUBSCRIBE_API_AUDIO (1u << 0)

// VoiceAssistantRequestFlag (flags in VoiceAssistantRequest, device->server)
#define ESPB_VA_REQUEST_USE_VAD       (1u << 0)
#define ESPB_VA_REQUEST_USE_WAKE_WORD (1u << 1)

// VoiceAssistantFeature (voice_assistant_feature_flags in DeviceInfoResponse)
// -- verified against aioesphomeapi/model.py, VoiceAssistantFeature IntFlag.
#define ESPB_VA_FEATURE_VOICE_ASSISTANT    (1u << 0)
#define ESPB_VA_FEATURE_SPEAKER            (1u << 1)
#define ESPB_VA_FEATURE_API_AUDIO          (1u << 2)
#define ESPB_VA_FEATURE_TIMERS             (1u << 3)
#define ESPB_VA_FEATURE_ANNOUNCE           (1u << 4)
#define ESPB_VA_FEATURE_START_CONVERSATION (1u << 5)
#define ESPB_VA_FEATURE_MULTI_CHANNEL_AUDIO (1u << 6)

#endif // ESPHOME_API_ESPHOME_PROTO_H_
