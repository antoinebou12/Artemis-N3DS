/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2017 Iwan Timmer
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Moonlight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Moonlight; if not, see <http://www.gnu.org/licenses/>.
 */

#include "util.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <3ds.h>

int write_bool(char *path, bool val) {
    int fd = open(path, O_RDWR);

    if (fd >= 0) {
        int ret = write(fd, val ? "1" : "0", 1);
        if (ret < 0)
            fprintf(stderr, "Failed to write %d to %s: %d\n", val ? 1 : 0, path,
                    ret);

        close(fd);
        return 0;
    } else
        return -1;
}

int read_file(char *path, char *output, int output_len) {
    int fd = open(path, O_RDONLY);

    if (fd >= 0) {
        output_len = read(fd, output, output_len);
        close(fd);
        return output_len;
    } else
        return -1;
}

bool ensure_buf_size(void **buf, size_t *buf_size, size_t required_size) {
    if (*buf_size >= required_size)
        return false;

    *buf_size = required_size;
    *buf = realloc(*buf, *buf_size);
    if (!*buf) {
        fprintf(stderr, "Failed to allocate %zu bytes\n", *buf_size);
        abort();
    }

    return true;
}

int ensure_linear_buf_size(void **buf, size_t *buf_size, size_t required_size) {
    if (buf == NULL || buf_size == NULL) {
        return 1;
    }

    // Require a live pointer — a prior linearAlloc failure used to leave
    // (*buf == NULL) with a non-zero *buf_size, so the next frame skipped
    // realloc and memcpy'd to NULL (Luma "Translation - Section", FAR~0x2).
    if (*buf != NULL && *buf_size >= required_size) {
        return 0;
    }

    if (*buf != NULL) {
        linearFree(*buf);
        *buf = NULL;
    }
    *buf_size = 0;

    void *grown = linearAlloc(required_size);
    if (grown == NULL) {
        fprintf(stderr, "Failed to allocate %zu bytes\n", required_size);
        return 1;
    }

    *buf = grown;
    *buf_size = required_size;
    return 0;
}
