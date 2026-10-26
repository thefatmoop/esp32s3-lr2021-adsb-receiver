#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct { uint16_t port; uint8_t enabled, avr; } tcp_config;
typedef struct { tcp_config config; unsigned clients, queued, sent, dropped, slow_clients; int error; } tcp_status;
int tcp_stream_start(void);
bool tcp_stream_configure(tcp_config config);
void tcp_stream_snapshot(tcp_status *out);
void tcp_stream_submit(const char hex[29], uint64_t received_us);
