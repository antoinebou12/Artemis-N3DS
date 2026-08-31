/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2015 Iwan Timmer
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

#include "http.h"
#include "errors.h"

#include <curl/curl.h>
#include <stdbool.h>
#include <string.h>

static CURL *curl = NULL;
static uint32_t connection_timeout_s = 60;
static int log_level = 0;
static http_cancel_callback_t cancel_callback = NULL;
static void *cancel_context = NULL;

static long connect_timeout_seconds(void) {
    // Connection establishment should fail quickly on a handheld LAN client.
    // Long operations such as pairing still keep their larger overall timeout.
    if (connection_timeout_s <= 5)
        return (long)connection_timeout_s;
    return 5L;
}

static void apply_runtime_options(void) {
    if (curl == NULL)
        return;

    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)connection_timeout_s);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, connect_timeout_seconds());
    curl_easy_setopt(curl, CURLOPT_VERBOSE, log_level > 0 ? 1L : 0L);
}

static size_t _write_curl(void *contents, size_t size, size_t nmemb,
                          void *userp) {
    size_t realsize = size * nmemb;
    PHTTP_DATA mem = (PHTTP_DATA)userp;

    mem->memory = realloc(mem->memory, mem->size + realsize + 1);
    if (mem->memory == NULL)
        return 0;

    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

static int _cancel_curl(void *context, curl_off_t download_total,
                        curl_off_t download_now, curl_off_t upload_total,
                        curl_off_t upload_now) {
    (void)context;
    (void)download_total;
    (void)download_now;
    (void)upload_total;
    (void)upload_now;
    return cancel_callback != NULL && cancel_callback(cancel_context) ? 1 : 0;
}

int http_init(const char *keyDirectory, int logLevel) {
    // gs_init() may be called repeatedly when refreshing a host. Ensure we do
    // not leak or stack easy handles between sessions.
    if (curl != NULL) {
        curl_easy_cleanup(curl);
        curl = NULL;
    }

    curl = curl_easy_init();
    if (!curl)
        return GS_FAILED;

    char certificateFilePath[4096];
    snprintf(certificateFilePath, sizeof(certificateFilePath), "%s/%s",
             keyDirectory, CERTIFICATE_FILE_NAME);

    char keyFilePath[4096];
    snprintf(keyFilePath, sizeof(keyFilePath), "%s/%s", keyDirectory,
             KEY_FILE_NAME);

    log_level = logLevel;

    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_SSLENGINE_DEFAULT, 1L);
    curl_easy_setopt(curl, CURLOPT_SSLCERTTYPE, "PEM");
    curl_easy_setopt(curl, CURLOPT_SSLCERT, certificateFilePath);
    curl_easy_setopt(curl, CURLOPT_SSLKEYTYPE, "PEM");
    curl_easy_setopt(curl, CURLOPT_SSLKEY, keyFilePath);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _write_curl);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, _cancel_curl);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, NULL);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_SESSIONID_CACHE, 0L);

    // The 3DS networking stack is IPv4-only in practice. Avoid wasting time on
    // IPv6 resolution/connection attempts for hostnames.
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1L);
#ifdef CURLOPT_TCP_KEEPALIVE
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
#endif
#ifdef CURLOPT_TCP_KEEPIDLE
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 15L);
#endif
#ifdef CURLOPT_TCP_KEEPINTVL
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 5L);
#endif

    // Keep the easy handle alive across serverinfo/applist/launch requests so
    // libcurl can reuse the TCP/TLS connection when the host permits it.
    curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 0L);
#ifndef __FreeBSD__
    curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 0L);
#endif

    apply_runtime_options();
    return GS_OK;
}

void http_set_timeout_s(uint32_t connection_timeout_in) {
    connection_timeout_s = connection_timeout_in;
    apply_runtime_options();
}

void http_set_log_level(int log_level_in) {
    log_level = log_level_in;
    apply_runtime_options();
}

void http_set_cancel_callback(http_cancel_callback_t callback, void *context) {
    cancel_callback = callback;
    cancel_context = context;
}

int http_request(char *url, PHTTP_DATA data) {
    if (curl == NULL) {
        gs_error = "HTTP client is not initialized";
        return GS_FAILED;
    }
    if (data == NULL) {
        gs_error = "Invalid HTTP response buffer";
        return GS_FAILED;
    }

    curl_easy_setopt(curl, CURLOPT_WRITEDATA, data);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    apply_runtime_options();
#ifdef __FreeBSD__
    curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 1L);
#endif

    if (log_level) {
        printf("Request %s\n", url);
    }

    if (data->size > 0) {
        free(data->memory);
        data->memory = malloc(1);
        if (data->memory == NULL)
            return GS_OUT_OF_MEMORY;

        data->size = 0;
    }
    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        if (res == CURLE_ABORTED_BY_CALLBACK) {
            gs_error = "Cancelled";
            return GS_CANCELLED;
        }
        gs_error = curl_easy_strerror(res);
        return GS_FAILED;
    } else if (data->memory == NULL) {
        return GS_OUT_OF_MEMORY;
    }

    if (log_level) {
        printf("Response:\n%s\n\n", data->memory);
    }

    return GS_OK;
}

void http_cleanup() {
    if (curl != NULL) {
        curl_easy_cleanup(curl);
        curl = NULL;
    }
}

PHTTP_DATA http_create_data() {
    PHTTP_DATA data = malloc(sizeof(HTTP_DATA));
    if (data == NULL)
        return NULL;

    data->memory = malloc(1);
    if (data->memory == NULL) {
        free(data);
        return NULL;
    }
    data->size = 0;

    return data;
}

void http_free_data(PHTTP_DATA data) {
    if (data != NULL) {
        if (data->memory != NULL)
            free(data->memory);

        free(data);
    }
}
