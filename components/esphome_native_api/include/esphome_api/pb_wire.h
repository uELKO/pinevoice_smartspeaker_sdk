// Copyright 2026
// SPDX-License-Identifier: Apache-2.0
//
// Minimal hand-written protobuf wire-format encode/decode. We only ever need
// varint (bool/uint32/enum) and length-delimited (string/bytes/submessage)
// fields for the ESPHome API message subset this component implements --
// nothing here needs fixed32/fixed64/sint zigzag or packed-repeated-of-varint
// support, so those are deliberately not implemented.

#ifndef ESPHOME_API_PB_WIRE_H_
#define ESPHOME_API_PB_WIRE_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define PB_WIRE_TYPE_VARINT 0
#define PB_WIRE_TYPE_LEN    2

// --- Writer ------------------------------------------------------------
// All pbw_write_* functions append to `buf` (capacity `cap`), advancing
// *len. They return false (without partially writing) if the value would
// overflow the buffer, so callers can size a buffer once and bail out
// cleanly instead of corrupting memory.

bool pbw_write_varint(uint8_t *buf, size_t cap, size_t *len, uint64_t value);
bool pbw_write_tag(uint8_t *buf, size_t cap, size_t *len, uint32_t field_number, uint8_t wire_type);
bool pbw_write_varint_field(uint8_t *buf, size_t cap, size_t *len, uint32_t field_number, uint64_t value);
bool pbw_write_bool_field(uint8_t *buf, size_t cap, size_t *len, uint32_t field_number, bool value);
// value == NULL or value_len == 0 writes nothing (proto3 default-value elision).
bool pbw_write_string_field(uint8_t *buf, size_t cap, size_t *len, uint32_t field_number, const char *value);
bool pbw_write_bytes_field(uint8_t *buf, size_t cap, size_t *len, uint32_t field_number, const uint8_t *value, size_t value_len);
// Writes field_number/LEN tag + submessage_len varint, then the caller is
// expected to have already placed submessage_len raw bytes at buf[*len].
// Used when a submessage was encoded into a scratch buffer first.
bool pbw_write_submessage_field(uint8_t *buf, size_t cap, size_t *len, uint32_t field_number, const uint8_t *submsg, size_t submsg_len);

// --- Reader --------------------------------------------------------------
// A pb_reader_t walks one flat message body (the bytes between the frame's
// length-prefix and its end -- see esphome_frame.h). Nested submessages are
// read by pointing a second reader at the slice pb_reader_next_field()
// returns for that field.

typedef struct {
    const uint8_t *buf;
    size_t len;
    size_t pos;
} pb_reader_t;

typedef struct {
    uint32_t field_number;
    uint8_t wire_type;
    uint64_t varint_value;      // valid when wire_type == PB_WIRE_TYPE_VARINT
    const uint8_t *bytes_value; // valid when wire_type == PB_WIRE_TYPE_LEN
    size_t bytes_len;           // ditto
} pb_field_t;

void pb_reader_init(pb_reader_t *r, const uint8_t *buf, size_t len);
// Returns true and fills *field if another field was read, false at
// end-of-message or on a malformed varint/truncated payload (caller should
// treat false as "stop parsing this message", not necessarily an error --
// end-of-message is by far the common case).
bool pb_reader_next_field(pb_reader_t *r, pb_field_t *field);

// Copies a bytes_value/bytes_len field into a NUL-terminated C string,
// truncating (never overflowing) if it doesn't fit in out_cap. Safe to call
// with a bytes_len of 0 (produces an empty string).
void pb_field_to_cstr(const pb_field_t *field, char *out, size_t out_cap);

#endif // ESPHOME_API_PB_WIRE_H_
