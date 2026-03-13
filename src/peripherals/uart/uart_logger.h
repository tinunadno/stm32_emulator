#ifndef STM32_UART_LOGGER_H
#define STM32_UART_LOGGER_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

/* Maximum events kept in the ring buffer */
#define UART_LOG_MAX_EVENTS  256
/* Width of the ASCII timeline in characters */
#define UART_DIAGRAM_COLS    60
/* Max rows shown in the event table (diagram stays readable) */
#define UART_TABLE_MAX_SHOW  40
/* Diagram text buffer size */
#define UART_DIAGRAM_BUF_SZ  4096

typedef enum {
    UART_LOG_TX = 0,
    UART_LOG_RX = 1
} UartLogDir;

typedef struct {
    uint64_t   tick;
    UartLogDir dir;
    uint8_t    byte;
} UartLogEvent;

/**
 * Ring-buffer logger for UART activity.
 * tick_count increments once per uart_logger_tick() call (one per simulator step).
 * Stores the last UART_LOG_MAX_EVENTS TX/RX bytes with timestamps.
 */
typedef struct {
    UartLogEvent events[UART_LOG_MAX_EVENTS];
    int          count;       /* events stored, capped at UART_LOG_MAX_EVENTS */
    int          head;        /* next write index (0..UART_LOG_MAX_EVENTS-1) */
    uint64_t     tick_count;  /* current simulation tick */
} UartLogger;

void uart_logger_init(UartLogger* log);

/** Advance the tick counter — call once per simulator step. */
void uart_logger_tick(UartLogger* log);

void uart_logger_log_tx(UartLogger* log, uint8_t byte);
void uart_logger_log_rx(UartLogger* log, uint8_t byte);

/** Clear all recorded events (tick counter keeps running). */
void uart_logger_clear(UartLogger* log);

/**
 * Render an ASCII timing diagram into buf.
 * Returns number of characters written (not counting null terminator).
 */
int uart_logger_generate_diagram(UartLogger* log, char* buf, size_t buf_size);

/**
 * Write an SVG timing diagram directly to an open FILE*.
 * The SVG is dark-themed (matches VS Code dark mode).
 */
void uart_logger_write_svg(UartLogger* log, FILE* f);

/**
 * Write a standalone HTML page containing the SVG diagram.
 * Includes <meta refresh> so a browser or Live Preview auto-reloads.
 */
void uart_logger_write_html(UartLogger* log, FILE* f);

#endif /* STM32_UART_LOGGER_H */
