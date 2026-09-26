// Copyright 2026
// SPDX-License-Identifier: Apache-2.0
//
// Plaintext ESPHome API frame I/O: preamble(varint, must be 0x00) +
// length(varint) + msg_type(varint) + payload(length bytes). Verified
// against aioesphomeapi's _frame_helper/plain_text.py (data_received/
// make_plain_text_packets) rather than guessed. Noise-encrypted framing is
// not implemented -- see TODO.md; this only speaks to clients that connect
// without a configured encryption key.

#ifndef ESPHOME_API_ESPHOME_FRAME_H_
#define ESPHOME_API_ESPHOME_FRAME_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Mirrors aioesphomeapi's own cap (17-bit length field, well above anything
// this device's message set ever sends/receives).
#define ESPB_MAX_FRAME_SIZE 65535

typedef struct {
    int fd;
} esphome_frame_io_t;

// Blocking read of one complete frame into payload_buf (capacity
// payload_cap). On success, *msg_type and *payload_len are set and
// payload_buf[0..*payload_len) holds the message body.
// Returns: 0 on success, -1 on socket error/EOF, -2 on a malformed preamble
// or a frame that exceeds payload_cap. Either -1 or -2 leaves the socket in
// an indeterminate mid-stream position (payload not drained on the
// too-large case) -- the caller must close the connection, not try to
// resync and keep reading.
int esphome_frame_read(esphome_frame_io_t *io, uint32_t *msg_type, uint8_t *payload_buf, size_t payload_cap, size_t *payload_len);

// Blocking write of one frame. Returns 0 on success, -1 on socket error.
int esphome_frame_write(esphome_frame_io_t *io, uint32_t msg_type, const uint8_t *payload, size_t payload_len);

#endif // ESPHOME_API_ESPHOME_FRAME_H_
