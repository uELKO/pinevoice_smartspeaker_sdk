// Copyright 2026
// SPDX-License-Identifier: Apache-2.0

#include "esphome_api/pb_wire.h"
#include <string.h>

bool pbw_write_varint(uint8_t *buf, size_t cap, size_t *len, uint64_t value)
{
    size_t pos = *len;
    do {
        if (pos >= cap) {
            return false;
        }
        uint8_t byte = (uint8_t)(value & 0x7F);
        value >>= 7;
        if (value != 0) {
            byte |= 0x80;
        }
        buf[pos++] = byte;
    } while (value != 0);
    *len = pos;
    return true;
}

bool pbw_write_tag(uint8_t *buf, size_t cap, size_t *len, uint32_t field_number, uint8_t wire_type)
{
    uint64_t tag = ((uint64_t)field_number << 3) | (wire_type & 0x7);
    return pbw_write_varint(buf, cap, len, tag);
}

bool pbw_write_varint_field(uint8_t *buf, size_t cap, size_t *len, uint32_t field_number, uint64_t value)
{
    if (value == 0) {
        // proto3 default value elision: omit fields that are zero/false.
        return true;
    }
    size_t saved = *len;
    if (!pbw_write_tag(buf, cap, len, field_number, PB_WIRE_TYPE_VARINT)) {
        *len = saved;
        return false;
    }
    if (!pbw_write_varint(buf, cap, len, value)) {
        *len = saved;
        return false;
    }
    return true;
}

bool pbw_write_bool_field(uint8_t *buf, size_t cap, size_t *len, uint32_t field_number, bool value)
{
    return pbw_write_varint_field(buf, cap, len, field_number, value ? 1 : 0);
}

bool pbw_write_bytes_field(uint8_t *buf, size_t cap, size_t *len, uint32_t field_number, const uint8_t *value, size_t value_len)
{
    if (value == NULL || value_len == 0) {
        return true;
    }
    size_t saved = *len;
    if (!pbw_write_tag(buf, cap, len, field_number, PB_WIRE_TYPE_LEN)) {
        goto fail;
    }
    if (!pbw_write_varint(buf, cap, len, value_len)) {
        goto fail;
    }
    if (*len + value_len > cap) {
        goto fail;
    }
    memcpy(buf + *len, value, value_len);
    *len += value_len;
    return true;

fail:
    *len = saved;
    return false;
}

bool pbw_write_string_field(uint8_t *buf, size_t cap, size_t *len, uint32_t field_number, const char *value)
{
    if (value == NULL) {
        return true;
    }
    return pbw_write_bytes_field(buf, cap, len, field_number, (const uint8_t *)value, strlen(value));
}

bool pbw_write_submessage_field(uint8_t *buf, size_t cap, size_t *len, uint32_t field_number, const uint8_t *submsg, size_t submsg_len)
{
    // Same wire shape as bytes -- a submessage is just length-delimited
    // bytes whose contents happen to be another encoded message.
    return pbw_write_bytes_field(buf, cap, len, field_number, submsg, submsg_len);
}

void pb_reader_init(pb_reader_t *r, const uint8_t *buf, size_t len)
{
    r->buf = buf;
    r->len = len;
    r->pos = 0;
}

// Returns false (leaving *out unset) on truncated/oversized varint.
static bool pb_read_varint(pb_reader_t *r, uint64_t *out)
{
    uint64_t result = 0;
    int shift = 0;
    while (r->pos < r->len) {
        uint8_t b = r->buf[r->pos++];
        result |= ((uint64_t)(b & 0x7F)) << shift;
        if ((b & 0x80) == 0) {
            *out = result;
            return true;
        }
        shift += 7;
        if (shift >= 64) {
            return false;
        }
    }
    return false;
}

bool pb_reader_next_field(pb_reader_t *r, pb_field_t *field)
{
    if (r->pos >= r->len) {
        return false;
    }

    uint64_t tag;
    if (!pb_read_varint(r, &tag)) {
        return false;
    }

    field->field_number = (uint32_t)(tag >> 3);
    field->wire_type = (uint8_t)(tag & 0x7);

    switch (field->wire_type) {
    case PB_WIRE_TYPE_VARINT: {
        uint64_t value;
        if (!pb_read_varint(r, &value)) {
            return false;
        }
        field->varint_value = value;
        field->bytes_value = NULL;
        field->bytes_len = 0;
        return true;
    }
    case PB_WIRE_TYPE_LEN: {
        uint64_t length;
        if (!pb_read_varint(r, &length)) {
            return false;
        }
        if (r->pos + length > r->len) {
            return false;
        }
        field->bytes_value = r->buf + r->pos;
        field->bytes_len = (size_t)length;
        field->varint_value = 0;
        r->pos += (size_t)length;
        return true;
    }
    default:
        // fixed32/fixed64 (wire types 5/1) never appear in the message
        // subset we implement -- treat as unparseable rather than guessing
        // a skip width.
        return false;
    }
}

void pb_field_to_cstr(const pb_field_t *field, char *out, size_t out_cap)
{
    if (out_cap == 0) {
        return;
    }
    size_t n = field->bytes_len;
    if (n > out_cap - 1) {
        n = out_cap - 1;
    }
    if (n > 0 && field->bytes_value != NULL) {
        memcpy(out, field->bytes_value, n);
    }
    out[n] = '\0';
}
