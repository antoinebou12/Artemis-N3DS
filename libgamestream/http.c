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
static uint32_t transfer_timeout_s = 60;
static uint32_t connect_timeout_s = 20;
static int log_level = 0;
static int fresh_connect = 0;
static http_cancel_callback_t cancel_callback = NULL;
static void *cancel_context = NULL;
static char certificateFilePath[4096];
static char keyFilePath[4096];

static size_t _write_curl(void *contents, size_t size, size_t nmemb,
                          void *userp);
static int _cancel_curl(void *context, curl_off_t download_total,
                        curl_off_t download_now, curl_off_t upload_total,
                        curl_off_t upload_now);

static bool is_tls_failure(CURLcode res) {
    return res == CURLE_SSL_CONNECT_ERROR || res == CURLE_SSL_CERTPROBLEM ||
           res == CURLE_SSL_CIPHER || res == CURLE_SSL_CACERT ||
           res == CURLE_PEER_FAILED_VERIFICATION ||
           res == CURLE_SSL_SHUTDOWN_FAILED || res == CURLE_SSL_CACERT_BADFILE;
}

static const char *http_user_error(CURLcode res) {
    if (is_tls_failure(res)) {
        return "HTTPS/TLS failed. Stay on the same Wi-Fi as the PC, retry "
               "Pair, then Connect again.";
    }
    if (res == CURLE_COULDNT_CONNECT || res == CURLE_COULDNT_RESOLVE_HOST) {
        return "Could not reach the host. Check the PC address and Wi-Fi.";
    }
    if (res == CURLE_OPERATION_TIMEDOUT) {
        return "Timeout was reached";
    }
    return curl_easy_strerror(res);
}

static void apply_ssl_and_socket_options(void) {
    if (curl == NULL)
        return;

    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_SSLCERTTYPE, "PEM");
    curl_easy_setopt(curl, CURLOPT_SSLCERT, certificateFilePath);
    curl_easy_setopt(curl, CURLOPT_SSLKEYTYPE, "PEM");
    curl_easy_setopt(curl, CURLOPT_SSLKEY, keyFilePath);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_SESSIONID_CACHE, 0L);
#ifdef CURL_SSLVERSION_TLSv1_2
    // 3DS OpenSSL + Sunshine is reliable on TLS 1.2. TLS 1.3 often resets
    // with SSL errors during pair/connect.
    curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
#endif
#ifdef CURLOPT_SSL_OPTIONS
    curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_ALLOW_BEAST);
#endif

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _write_curl);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, _cancel_curl);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, NULL);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
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
#ifdef __3DS__
    // Reusing a 3DS TLS session across HTTP then HTTPS (or pair then applist)
    // commonly yields SSL connect errors / connection reset.
    curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 1L);
#else
#ifndef __FreeBSD__
    curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 0L);
#endif
#endif
}

static void apply_runtime_options(void) {
    if (curl == NULL)
        return;

    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)transfer_timeout_s);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long)connect_timeout_s);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, log_level > 0 ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, fresh_connect ? 1L : 0L);
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

    log_level = logLevel;
    snprintf(certificateFilePath, sizeof(certificateFilePath), "%s/%s",
             keyDirectory, CERTIFICATE_FILE_NAME);
    snprintf(keyFilePath, sizeof(keyFilePath), "%s/%s", keyDirectory,
             KEY_FILE_NAME);

    apply_ssl_and_socket_options();

    apply_runtime_options();
    return GS_OK;
}

void http_set_timeout_s(uint32_t connection_timeout_in) {
    transfer_timeout_s = connection_timeout_in;
    // 3DS TLS with a client cert is slow. A 5s connect cap made pairing fail
    // with "Timeout was reached" right after the PIN was accepted, on the
    // HTTPS pairchallenge step.
    if (connection_timeout_in <= 20) {
        connect_timeout_s = connection_timeout_in;
    } else if (connection_timeout_in >= 120) {
        connect_timeout_s = 90;
    } else {
        connect_timeout_s = 30;
    }
    apply_runtime_options();
}

void http_set_fresh_connect(int enabled) {
    fresh_connect = enabled ? 1 : 0;
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
    if (is_tls_failure(res)) {
        curl_easy_cleanup(curl);
        curl = curl_easy_init();
        if (curl == NULL) {
            gs_error = "HTTPS/TLS failed";
            return GS_FAILED;
        }
        apply_ssl_and_socket_options();
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, data);
        curl_easy_setopt(curl, CURLOPT_URL, url);
        fresh_connect = 1;
        apply_runtime_options();
        res = curl_easy_perform(curl);
        fresh_connect = 0;
        apply_runtime_options();
    }

    if (res != CURLE_OK) {
        if (res == CURLE_ABORTED_BY_CALLBACK) {
            gs_error = "Cancelled";
            return GS_CANCELLED;
        }
        gs_error = http_user_error(res);
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
