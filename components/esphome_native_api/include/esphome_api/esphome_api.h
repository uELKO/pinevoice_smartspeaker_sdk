// Copyright 2026
// SPDX-License-Identifier: Apache-2.0
//
// Phase 1: a minimal ESPHome native-API server. Handles the connection
// lifecycle (Hello/DeviceInfo/ListEntities/Ping/Disconnect) so this device
// can be added in Home Assistant as an ESPHome node and show up connected --
// no entities, no voice assistant support yet (see TODO.md, "ESPHome-Native-
// API statt Wyoming"). Plaintext only; Noise encryption is not implemented,
// so the corresponding ESPHome/HA config must leave the API key unset.

#ifndef ESPHOME_API_ESPHOME_API_H_
#define ESPHOME_API_ESPHOME_API_H_

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    const char *name;          // hostname-style id, e.g. "pinevoice-a1b2c3"
    const char *friendly_name; // display name, e.g. "PineVoice Kueche"
    const char *mac_address;   // "AA:BB:CC:DD:EE:FF"
    const char *model;         // "PineVoice"
    const char *manufacturer;  // "Pine64"
    const char *version;       // our firmware version string
} esphome_api_device_info_t;

// Must be called once before esphome_api_start(); the struct is copied
// in, the pointed-to strings are not (they must outlive the server, e.g.
// string literals or static buffers).
void esphome_api_set_device_info(const esphome_api_device_info_t *info);

// Spawns the API server task (TCP port 6053, one connection at a time).
void esphome_api_start(void);

// Whether the currently connected HA client (if any) has an active
// SubscribeVoiceAssistantRequest subscription, and the flags it sent
// (see ESPB_VA_SUBSCRIBE_* in esphome_proto.h). false/0 if never subscribed,
// or if the client explicitly unsubscribed, or if no client is connected.
bool esphome_api_voice_assistant_subscribed(void);
uint32_t esphome_api_voice_assistant_flags(void);

// Sends a VoiceAssistantRequest (start=true) to the currently connected HA
// client and waits up to timeout_ms for the matching VoiceAssistantResponse.
// Returns false if there is no active connection, the write failed, or no
// response arrived within timeout_ms; *out_port/*out_error are only valid
// when this returns true. Not yet wired to the wake-word path (see
// TODO.md) -- exposed for a CLI test command first.
bool esphome_api_send_voice_assistant_start(const char *conversation_id, uint32_t flags,
                                             uint32_t timeout_ms, uint32_t *out_port, bool *out_error);

#endif // ESPHOME_API_ESPHOME_API_H_
