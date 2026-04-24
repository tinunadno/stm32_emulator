#include "peripherals/uart/uart_logger.h"

#include <string.h>
#include <stdio.h>

void uart_logger_init(UartLogger* log)
{
    memset(log, 0, sizeof(UartLogger));
}

void uart_logger_tick(UartLogger* log)
{
    log->tick_count++;
}

void uart_logger_clear(UartLogger* log)
{
    uint64_t saved_tick = log->tick_count;
    memset(log, 0, sizeof(UartLogger));
    log->tick_count = saved_tick;
}

static void push_event(UartLogger* log, UartLogDir dir, uint8_t byte)
{
    log->events[log->head].tick = log->tick_count;
    log->events[log->head].dir  = dir;
    log->events[log->head].byte = byte;
    log->head = (log->head + 1) % UART_LOG_MAX_EVENTS;
    if (log->count < UART_LOG_MAX_EVENTS)
        log->count++;
}

void uart_logger_log_tx(UartLogger* log, uint8_t byte)
{
    push_event(log, UART_LOG_TX, byte);
}

void uart_logger_log_rx(UartLogger* log, uint8_t byte)
{
    push_event(log, UART_LOG_RX, byte);
}

/* ---- diagram helpers ---- */

static char printable(uint8_t b)
{
    return (b >= 0x20 && b < 0x7F) ? (char)b : '.';
}

static void char_repr(uint8_t b, char* out, size_t out_sz)
{
    if      (b >= 0x20 && b < 0x7F) snprintf(out, out_sz, "'%c'",  (char)b);
    else if (b == '\n')              snprintf(out, out_sz, "\\n");
    else if (b == '\r')              snprintf(out, out_sz, "\\r");
    else if (b == '\t')              snprintf(out, out_sz, "\\t");
    else                             snprintf(out, out_sz, "[%02X]", b);
}

int uart_logger_generate_diagram(UartLogger* log, char* buf, size_t buf_size)
{
    int pos = 0;
    int n;

#define W(...) do { \
    n = snprintf(buf + pos, buf_size - (size_t)pos - 1, __VA_ARGS__); \
    if (n > 0) pos += n; \
} while (0)

    int total   = log->count;
    /* Index of the oldest stored event */
    int oldest  = (log->head - log->count + UART_LOG_MAX_EVENTS) % UART_LOG_MAX_EVENTS;

    int tx_count = 0, rx_count = 0;
    for (int i = 0; i < total; i++) {
        int idx = (oldest + i) % UART_LOG_MAX_EVENTS;
        if (log->events[idx].dir == UART_LOG_TX) tx_count++;
        else                                       rx_count++;
    }

    W("UART Timing Diagram\r\n");
    W("===================\r\n");
    W("Tick: %llu  |  TX: %d  RX: %d",
      (unsigned long long)log->tick_count, tx_count, rx_count);
    if (log->count >= UART_LOG_MAX_EVENTS)
        W("  (ring full, last %d)", UART_LOG_MAX_EVENTS);
    W("\r\n\r\n");

    if (total == 0) {
        W("No UART activity recorded.\r\n");
        buf[pos] = '\0';
        return pos;
    }

    /* ---- event table ---- */
    int show_from = 0;
    if (total > UART_TABLE_MAX_SHOW) {
        show_from = total - UART_TABLE_MAX_SHOW;
        W("  ... (showing last %d of %d events)\r\n\r\n",
          UART_TABLE_MAX_SHOW, total);
    }

    W("   Tick     Dir   Hex    Char\r\n");
    W("---------  ----  -----  ----\r\n");

    char repr[8];
    for (int i = show_from; i < total; i++) {
        int idx = (oldest + i) % UART_LOG_MAX_EVENTS;
        UartLogEvent* e = &log->events[idx];
        char_repr(e->byte, repr, sizeof(repr));
        W("%9llu   %s   0x%02X   %s\r\n",
          (unsigned long long)e->tick,
          e->dir == UART_LOG_TX ? "TX" : "RX",
          e->byte, repr);
    }

    /* ---- ASCII timeline ---- */
    if (total >= 2) {
        uint64_t t_min = log->events[oldest].tick;
        uint64_t t_max = t_min;
        for (int i = 0; i < total; i++) {
            int idx = (oldest + i) % UART_LOG_MAX_EVENTS;
            uint64_t t = log->events[idx].tick;
            if (t < t_min) t_min = t;
            if (t > t_max) t_max = t;
        }

        uint64_t span = (t_max > t_min) ? (t_max - t_min) : 1;
        uint64_t ticks_per_col = span / UART_DIAGRAM_COLS + 1;

        char tx_row[UART_DIAGRAM_COLS + 1];
        char rx_row[UART_DIAGRAM_COLS + 1];
        memset(tx_row, '.', UART_DIAGRAM_COLS);
        memset(rx_row, '.', UART_DIAGRAM_COLS);
        tx_row[UART_DIAGRAM_COLS] = '\0';
        rx_row[UART_DIAGRAM_COLS] = '\0';

        for (int i = 0; i < total; i++) {
            int idx = (oldest + i) % UART_LOG_MAX_EVENTS;
            UartLogEvent* e = &log->events[idx];
            int col = (int)(((e->tick - t_min) * (uint64_t)(UART_DIAGRAM_COLS - 1)) / span);
            if (col >= UART_DIAGRAM_COLS) col = UART_DIAGRAM_COLS - 1;
            char c = printable(e->byte);
            if (e->dir == UART_LOG_TX) { if (tx_row[col] == '.') tx_row[col] = c; }
            else                        { if (rx_row[col] == '.') rx_row[col] = c; }
        }

        W("\r\nTimeline (%llu ticks/col):\r\n",
          (unsigned long long)ticks_per_col);
        W("TX |%s|\r\n", tx_row);
        W("RX |%s|\r\n", rx_row);
        W("    ^ tick %-20llu%*llu ^\r\n",
          (unsigned long long)t_min,
          (int)(UART_DIAGRAM_COLS - 22),
          (unsigned long long)t_max);
    }

    W("\r\nTip: 'monitor uart-clear' resets the log.\r\n");
    buf[pos] = '\0';

#undef W
    return pos;
}

/* ---- SVG output ---- */

/* Write a byte label safe for SVG <text>: printable ASCII or escaped */
static void svg_byte_label(FILE* f, uint8_t b)
{
    if      (b == '<')              fputs("&lt;",  f);
    else if (b == '>')              fputs("&gt;",  f);
    else if (b == '&')              fputs("&amp;", f);
    else if (b == '"')              fputs("&quot;",f);
    else if (b >= 0x20 && b < 0x7F) fputc((char)b, f);
    else if (b == '\n')             fputs("LF",    f);
    else if (b == '\r')             fputs("CR",    f);
    else if (b == '\t')             fputs("HT",    f);
    else                            fprintf(f, "%02X", b);
}

/* Same for <title> tooltips (allows two chars) */
static void svg_tooltip(FILE* f, uint8_t b)
{
    if      (b == '<')              fputs("&lt;",  f);
    else if (b == '>')              fputs("&gt;",  f);
    else if (b == '&')              fputs("&amp;", f);
    else if (b >= 0x20 && b < 0x7F) fputc((char)b, f);
    else                            fprintf(f, "0x%02X", b);
}

void uart_logger_write_svg(UartLogger* log, FILE* f)
{
    int total  = log->count;
    int oldest = (log->head - log->count + UART_LOG_MAX_EVENTS) % UART_LOG_MAX_EVENTS;

    int tx_count = 0, rx_count = 0;
    for (int i = 0; i < total; i++) {
        int idx = (oldest + i) % UART_LOG_MAX_EVENTS;
        if (log->events[idx].dir == UART_LOG_TX) tx_count++;
        else                                       rx_count++;
    }

    /* ---- layout constants ---- */
    const int W       = 960;
    const int H       = 300;
    const int x_left  = 55;
    const int x_right = W - 30;
    const int chart_w = x_right - x_left;
    const int tx_y    = 105;   /* TX baseline */
    const int rx_y    = 180;   /* RX baseline */
    const int box_h   = 28;
    const int tl_y    = 240;   /* timeline axis */

    /* time range */
    uint64_t t_min = 0, t_max = 1;
    if (total >= 1) {
        t_min = log->events[oldest].tick;
        t_max = t_min;
        for (int i = 0; i < total; i++) {
            int idx = (oldest + i) % UART_LOG_MAX_EVENTS;
            uint64_t t = log->events[idx].tick;
            if (t < t_min) t_min = t;
            if (t > t_max) t_max = t;
        }
        if (t_max == t_min) t_max = t_min + 1;
    }
    uint64_t span = t_max - t_min;

    /* ---- SVG header ---- */
    fprintf(f,
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<svg xmlns=\"http://www.w3.org/2000/svg\""
        " width=\"%d\" height=\"%d\">\n", W, H);

    /* background */
    fprintf(f, "<rect width=\"%d\" height=\"%d\" fill=\"#1e1e1e\"/>\n", W, H);

    /* title bar */
    fprintf(f, "<rect width=\"%d\" height=\"38\" fill=\"#252526\"/>\n", W);
    fprintf(f,
        "<text x=\"%d\" y=\"24\" text-anchor=\"middle\""
        " font-family=\"monospace\" font-size=\"13\" fill=\"#cccccc\">"
        "UART Timing Diagram"
        " &#160;|&#160; Tick: %llu"
        " &#160;|&#160; TX: %d"
        " &#160;|&#160; RX: %d"
        "</text>\n",
        W / 2,
        (unsigned long long)log->tick_count,
        tx_count, rx_count);

    /* row labels */
    fprintf(f,
        "<text x=\"%d\" y=\"%d\" text-anchor=\"end\""
        " font-family=\"monospace\" font-size=\"12\" fill=\"#569cd6\">TX</text>\n",
        x_left - 6, tx_y + 4);
    fprintf(f,
        "<text x=\"%d\" y=\"%d\" text-anchor=\"end\""
        " font-family=\"monospace\" font-size=\"12\" fill=\"#4ec9b0\">RX</text>\n",
        x_left - 6, rx_y + 4);

    /* row baselines */
    fprintf(f,
        "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\""
        " stroke=\"#264f78\" stroke-width=\"1\"/>\n",
        x_left, tx_y, x_right, tx_y);
    fprintf(f,
        "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\""
        " stroke=\"#1e4f40\" stroke-width=\"1\"/>\n",
        x_left, rx_y, x_right, rx_y);

    /* timeline axis */
    fprintf(f,
        "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\""
        " stroke=\"#555\" stroke-width=\"1\"/>\n",
        x_left, tl_y, x_right, tl_y);

    /* 5 tick marks on timeline */
    for (int i = 0; i <= 4; i++) {
        int x = x_left + i * chart_w / 4;
        uint64_t tick_val = t_min + (uint64_t)i * span / 4;
        fprintf(f,
            "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\""
            " stroke=\"#555\" stroke-width=\"1\"/>\n",
            x, tl_y - 4, x, tl_y + 4);
        fprintf(f,
            "<text x=\"%d\" y=\"%d\" text-anchor=\"middle\""
            " font-family=\"monospace\" font-size=\"9\" fill=\"#777\">%llu</text>\n",
            x, tl_y + 16, (unsigned long long)tick_val);
    }

    /* ---- draw each event ---- */
    for (int i = 0; i < total; i++) {
        int idx = (oldest + i) % UART_LOG_MAX_EVENTS;
        UartLogEvent* e = &log->events[idx];

        int x = x_left + (int)(((e->tick - t_min) * (uint64_t)chart_w) / span);
        if (x < x_left)       x = x_left;
        if (x > x_right - 12) x = x_right - 12;

        int is_tx = (e->dir == UART_LOG_TX);
        int y_base  = is_tx ? tx_y : rx_y;
        int y_box   = y_base - box_h / 2;
        const char* stroke  = is_tx ? "#569cd6" : "#4ec9b0";
        const char* fill    = is_tx ? "#1a3a5c" : "#0d3b30";
        const char* tcolor  = is_tx ? "#7fb8e0" : "#7fdfc8";

        /* stem from box to timeline */
        fprintf(f,
            "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\""
            " stroke=\"%s\" stroke-width=\"1\" opacity=\"0.35\"/>\n",
            x, y_base + box_h / 2, x, tl_y, stroke);

        /* box with tooltip */
        fprintf(f, "<g><title>%s 0x%02X '",
                is_tx ? "TX" : "RX", e->byte);
        svg_tooltip(f, e->byte);
        fprintf(f, "' @ tick %llu</title>\n",
                (unsigned long long)e->tick);

        fprintf(f,
            "<rect x=\"%d\" y=\"%d\" width=\"24\" height=\"%d\""
            " rx=\"3\" fill=\"%s\" stroke=\"%s\" stroke-width=\"1\"/>\n",
            x - 12, y_box, box_h, fill, stroke);

        /* label inside box */
        fprintf(f,
            "<text x=\"%d\" y=\"%d\" text-anchor=\"middle\""
            " dominant-baseline=\"middle\""
            " font-family=\"monospace\" font-size=\"10\" fill=\"%s\">",
            x, y_base, tcolor);
        svg_byte_label(f, e->byte);
        fprintf(f, "</text>\n</g>\n");
    }

    /* "no data" message */
    if (total == 0) {
        fprintf(f,
            "<text x=\"%d\" y=\"%d\" text-anchor=\"middle\""
            " font-family=\"monospace\" font-size=\"13\" fill=\"#555\">"
            "No UART activity recorded</text>\n",
            W / 2, (tx_y + rx_y) / 2);
    }

    fprintf(f, "</svg>\n");
}

void uart_logger_write_html(UartLogger* log, FILE* f)
{
    fprintf(f,
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "<meta charset=\"UTF-8\">\n"
        /* Live Preview (WebSocket) handles refresh; meta-refresh is a fallback
         * for plain browser viewing — reloads every 2 s. */
        "<meta http-equiv=\"refresh\" content=\"2\">\n"
        "<title>UART Timing Diagram</title>\n"
        "<style>\n"
        "  html,body{margin:0;padding:0;background:#1e1e1e;overflow:hidden;}\n"
        "  svg{display:block;}\n"
        "</style>\n"
        "</head>\n"
        "<body>\n");

    uart_logger_write_svg(log, f);

    fprintf(f, "</body>\n</html>\n");
}

