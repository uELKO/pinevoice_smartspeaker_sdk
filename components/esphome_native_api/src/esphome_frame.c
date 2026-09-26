// Copyright 2026
// SPDX-License-Identifier: Apache-2.0

#include "esphome_api/esphome_frame.h"

#include <errno.h>
#include <lwip/sockets.h>

static int recv_full(int fd, uint8_t *buf, size_t n)
{
    size_t got = 0;
    while (got < n) {
        ssize_t r = recv(fd, buf + got, n - got, 0);
        if (r == 0) {
            return -1; // peer closed
        }
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        got += (size_t)r;
    }
    return 0;
}

static int send_full(int fd, const uint8_t *buf, size_t n)
{
    size_t sent = 0;
    while (sent < n) {
        ssize_t r = send(fd, buf + sent, n - sent, 0);
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        sent += (size_t)r;
    }
    return 0;
}

// Reads one wire varint directly off the socket, one byte at a time. Frame
// preambles/lengths/types are at most a couple bytes each, so the extra
// syscalls per field are negligible next to the bulk payload read below.
static int recv_varint(int fd, uint64_t *out)
{
    uint64_t result = 0;
    int shift = 0;
    while (shift < 64) {
        uint8_t b;
        if (recv_full(fd, &b, 1) != 0) {
            return -1;
        }
        result |= ((uint64_t)(b & 0x7F)) << shift;
        if ((b & 0x80) == 0) {
            *out = result;
            return 0;
        }
        shift += 7;
    }
    return -1;
}

static int send_varint(int fd, uint64_t value)
{
    uint8_t buf[10];
    size_t len = 0;
    do {
        uint8_t byte = (uint8_t)(value & 0x7F);
        value >>= 7;
        if (value != 0) {
            byte |= 0x80;
        }
        buf[len++] = byte;
    } while (value != 0 && len < sizeof(buf));
    return send_full(fd, buf, len);
}

int esphome_frame_read(esphome_frame_io_t *io, uint32_t *msg_type, uint8_t *payload_buf, size_t payload_cap, size_t *payload_len)
{
    uint64_t preamble;
    if (recv_varint(io->fd, &preamble) != 0) {
        return -1;
    }
    if (preamble != 0x00) {
        // 0x01 means "the peer expects a Noise-encrypted connection"; any
        // other value is simply not a plaintext frame we understand. Either
        // way we don't speak Noise (yet), so there's nothing to recover.
        return -2;
    }

    uint64_t length;
    if (recv_varint(io->fd, &length) != 0) {
        return -1;
    }
    if (length > ESPB_MAX_FRAME_SIZE || length > payload_cap) {
        return -2;
    }

    uint64_t type;
    if (recv_varint(io->fd, &type) != 0) {
        return -1;
    }

    if (length > 0) {
        if (recv_full(io->fd, payload_buf, (size_t)length) != 0) {
            return -1;
        }
    }

    *msg_type = (uint32_t)type;
    *payload_len = (size_t)length;
    return 0;
}

int esphome_frame_write(esphome_frame_io_t *io, uint32_t msg_type, const uint8_t *payload, size_t payload_len)
{
    if (send_varint(io->fd, 0x00) != 0) {
        return -1;
    }
    if (send_varint(io->fd, payload_len) != 0) {
        return -1;
    }
    if (send_varint(io->fd, msg_type) != 0) {
        return -1;
    }
    if (payload_len > 0 && payload != NULL) {
        if (send_full(io->fd, payload, payload_len) != 0) {
            return -1;
        }
    }
    return 0;
}
