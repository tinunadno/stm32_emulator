#include "gdb_stub.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <errno.h>

#include "common/status.h"
#include "bus/bus.h"
#include "debugger/debugger.h"

#define PKT_BUF_SIZE 4096

/*
 * Cortex-M3 target description XML.
 * Tells arm-none-eabi-gdb exactly which registers exist and their sizes,
 * instead of the legacy ARM format that includes FPA floating-point registers.
 */
static const char TARGET_XML[] =
    "<?xml version=\"1.0\"?>"
    "<!DOCTYPE target SYSTEM \"gdb-target.dtd\">"
    "<target version=\"1.0\">"
      "<architecture>arm</architecture>"
      "<feature name=\"org.gnu.gdb.arm.m-profile\">"
        "<reg name=\"r0\"  bitsize=\"32\" regnum=\"0\"/>"
        "<reg name=\"r1\"  bitsize=\"32\" regnum=\"1\"/>"
        "<reg name=\"r2\"  bitsize=\"32\" regnum=\"2\"/>"
        "<reg name=\"r3\"  bitsize=\"32\" regnum=\"3\"/>"
        "<reg name=\"r4\"  bitsize=\"32\" regnum=\"4\"/>"
        "<reg name=\"r5\"  bitsize=\"32\" regnum=\"5\"/>"
        "<reg name=\"r6\"  bitsize=\"32\" regnum=\"6\"/>"
        "<reg name=\"r7\"  bitsize=\"32\" regnum=\"7\"/>"
        "<reg name=\"r8\"  bitsize=\"32\" regnum=\"8\"/>"
        "<reg name=\"r9\"  bitsize=\"32\" regnum=\"9\"/>"
        "<reg name=\"r10\" bitsize=\"32\" regnum=\"10\"/>"
        "<reg name=\"r11\" bitsize=\"32\" regnum=\"11\"/>"
        "<reg name=\"r12\" bitsize=\"32\" regnum=\"12\"/>"
        "<reg name=\"sp\"  bitsize=\"32\" regnum=\"13\" type=\"data_ptr\"/>"
        "<reg name=\"lr\"  bitsize=\"32\" regnum=\"14\"/>"
        "<reg name=\"pc\"  bitsize=\"32\" regnum=\"15\" type=\"code_ptr\"/>"
        "<reg name=\"xpsr\" bitsize=\"32\" regnum=\"16\"/>"
      "</feature>"
    "</target>";

/* ---- Low-level I/O ---- */

static int recv_byte(int fd, char* c)
{
    return recv(fd, c, 1, 0) == 1;
}

static int send_raw(int fd, const char* data, int len)
{
    int sent = 0;
    while (sent < len) {
        int n = (int)send(fd, data + sent, (size_t)(len - sent), 0);
        if (n <= 0) return 0;
        sent += n;
    }
    return 1;
}

static uint8_t rsp_checksum(const char* data, int len)
{
    uint8_t sum = 0;
    for (int i = 0; i < len; i++)
        sum += (uint8_t)data[i];
    return sum;
}

/*
 * Receive one RSP packet.
 * Skips bytes until '$', reads until '#', validates checksum, sends ACK.
 * Special case: 0x03 (interrupt) is returned as a single-byte "packet".
 * Returns packet length on success, -1 on error.
 */
static int rsp_recv_packet(int fd, char* buf, int maxlen)
{
    char c;

    /* Wait for '$' or 0x03 interrupt */
    while (1) {
        if (!recv_byte(fd, &c)) return -1;
        if (c == 0x03) {
            buf[0] = 0x03;
            buf[1] = '\0';
            return 1;
        }
        if (c == '$') break;
    }

    /* Read data until '#' */
    int   len = 0;
    uint8_t chk = 0;
    while (1) {
        if (!recv_byte(fd, &c)) return -1;
        if (c == '#') break;
        if (len >= maxlen - 1) return -1;
        buf[len++] = c;
        chk += (uint8_t)c;
    }
    buf[len] = '\0';

    /* Read 2-digit checksum */
    char cs[3];
    if (!recv_byte(fd, &cs[0])) return -1;
    if (!recv_byte(fd, &cs[1])) return -1;
    cs[2] = '\0';

    uint8_t expected = (uint8_t)strtoul(cs, NULL, 16);
    if (chk != expected) {
        send_raw(fd, "-", 1);  /* NACK */
        return -1;
    }

    send_raw(fd, "+", 1);  /* ACK */
    return len;
}

/*
 * Send one RSP packet: $data#checksum
 * Waits for ACK from GDB.
 */
static int rsp_send_packet(int fd, const char* data)
{
    int    len = (int)strlen(data);
    uint8_t chk = rsp_checksum(data, len);

    char header = '$';
    send_raw(fd, &header, 1);
    send_raw(fd, data, len);

    char footer[4];
    snprintf(footer, sizeof(footer), "#%02x", chk);
    send_raw(fd, footer, 3);

    /* Wait for ACK */
    char ack;
    if (!recv_byte(fd, &ack)) return 0;
    return ack == '+';
}

/* ---- Register encoding (little-endian) ---- */

/* Encode uint32 as 8 hex chars in little-endian byte order */
static void encode_u32le(uint32_t val, char* out)
{
    for (int i = 0; i < 4; i++)
        snprintf(out + i * 2, 3, "%02x", (val >> (8 * i)) & 0xFF);
}

/* Decode 8 hex chars (little-endian) to uint32 */
static uint32_t decode_u32le(const char* hex)
{
    uint32_t val = 0;
    for (int i = 0; i < 4; i++) {
        char bs[3] = { hex[i * 2], hex[i * 2 + 1], '\0' };
        uint8_t b = (uint8_t)strtoul(bs, NULL, 16);
        val |= (uint32_t)b << (8 * i);
    }
    return val;
}

/* ---- Command handlers ---- */

/* 'g' — send all registers: r0-r15 (64 bytes) + xpsr (4 bytes) = 136 hex chars */
static void handle_read_regs(GdbStub* stub, int fd)
{
    CoreState* s = &stub->sim->core.state;
    char buf[17 * 8 + 1];

    for (int i = 0; i < 16; i++)
        encode_u32le(s->r[i], buf + i * 8);
    encode_u32le(s->xpsr, buf + 16 * 8);
    buf[17 * 8] = '\0';

    rsp_send_packet(fd, buf);
}

/* 'G...' — write all registers */
static void handle_write_regs(GdbStub* stub, int fd, const char* data)
{
    CoreState* s = &stub->sim->core.state;

    for (int i = 0; i < 16; i++)
        s->r[i] = decode_u32le(data + i * 8);
    s->xpsr = decode_u32le(data + 16 * 8);

    rsp_send_packet(fd, "OK");
}

/* 'p n' — read single register */
static void handle_read_reg(GdbStub* stub, int fd, const char* args)
{
    int n = (int)strtol(args, NULL, 16);
    CoreState* s = &stub->sim->core.state;

    uint32_t val;
    if (n >= 0 && n < 16)
        val = s->r[n];
    else if (n == 16)
        val = s->xpsr;
    else {
        rsp_send_packet(fd, "E00");
        return;
    }

    char buf[9];
    encode_u32le(val, buf);
    buf[8] = '\0';
    rsp_send_packet(fd, buf);
}

/* 'P n=v' — write single register */
static void handle_write_reg(GdbStub* stub, int fd, const char* args)
{
    const char* eq = strchr(args, '=');
    if (!eq) { rsp_send_packet(fd, "E00"); return; }

    int      n   = (int)strtol(args, NULL, 16);
    uint32_t val = decode_u32le(eq + 1);

    CoreState* s = &stub->sim->core.state;
    if (n >= 0 && n < 16)
        s->r[n] = val;
    else if (n == 16)
        s->xpsr = val;
    else {
        rsp_send_packet(fd, "E00");
        return;
    }

    rsp_send_packet(fd, "OK");
}

/* 'm addr,len' — read memory */
static void handle_read_mem(GdbStub* stub, int fd, const char* args)
{
    const char* comma = strchr(args, ',');
    if (!comma) { rsp_send_packet(fd, "E00"); return; }

    uint32_t addr = (uint32_t)strtoul(args,     NULL, 16);
    uint32_t len  = (uint32_t)strtoul(comma + 1, NULL, 16);
    if (len > 1024) len = 1024;

    char buf[1024 * 2 + 1];
    int  pos = 0;
    for (uint32_t i = 0; i < len; i++) {
        uint32_t b = bus_read(&stub->sim->bus, addr + i, 1);
        snprintf(buf + pos, 3, "%02x", b & 0xFF);
        pos += 2;
    }
    buf[pos] = '\0';
    rsp_send_packet(fd, buf);
}

/* 'M addr,len:data' — write memory */
static void handle_write_mem(GdbStub* stub, int fd, const char* args)
{
    const char* comma = strchr(args, ',');
    const char* colon = strchr(args, ':');
    if (!comma || !colon) { rsp_send_packet(fd, "E00"); return; }

    uint32_t    addr = (uint32_t)strtoul(args,     NULL, 16);
    uint32_t    len  = (uint32_t)strtoul(comma + 1, NULL, 16);
    const char* hex  = colon + 1;

    for (uint32_t i = 0; i < len; i++) {
        char bs[3] = { hex[i * 2], hex[i * 2 + 1], '\0' };
        uint8_t b = (uint8_t)strtoul(bs, NULL, 16);
        bus_write(&stub->sim->bus, addr + i, b, 1);
    }
    rsp_send_packet(fd, "OK");
}

/* Check if GDB sent a 0x03 interrupt — non-blocking */
static int check_interrupt(int fd)
{
    fd_set fds;
    struct timeval tv = { 0, 0 };
    FD_ZERO(&fds);
    FD_SET(fd, &fds);

    if (select(fd + 1, &fds, NULL, NULL, &tv) > 0) {
        char c;
        if (recv(fd, &c, 1, MSG_PEEK) == 1 && (unsigned char)c == 0x03) {
            recv(fd, &c, 1, 0);  /* consume */
            return 1;
        }
    }
    return 0;
}

/* 'c [addr]' — continue until breakpoint or GDB interrupt */
static void handle_continue(GdbStub* stub, int fd, const char* args)
{
    if (args && *args) {
        uint32_t addr = (uint32_t)strtoul(args, NULL, 16);
        stub->sim->core.state.r[REG_PC] = addr;
    }

    stub->sim->halted  = 0;
    stub->sim->running = 1;

    while (!stub->sim->halted) {
        Status s = simulator_step(stub->sim);

        if (check_interrupt(fd))
            break;

        if (s == STATUS_BREAKPOINT_HIT || s != STATUS_OK)
            break;
    }

    stub->sim->running = 0;
    rsp_send_packet(fd, "S05");  /* SIGTRAP */
}

/* 's [addr]' — single step */
static void handle_step(GdbStub* stub, int fd, const char* args)
{
    if (args && *args) {
        uint32_t addr = (uint32_t)strtoul(args, NULL, 16);
        stub->sim->core.state.r[REG_PC] = addr;
    }

    stub->sim->halted = 0;
    simulator_step(stub->sim);
    rsp_send_packet(fd, "S05");  /* SIGTRAP */
}

/* 'Z0,addr,kind' — insert software breakpoint */
static void handle_set_bp(GdbStub* stub, int fd, const char* args)
{
    /* args format: "type,addr,kind" */
    const char* p = strchr(args, ',');
    if (!p) { rsp_send_packet(fd, "E00"); return; }

    uint32_t addr = (uint32_t)strtoul(p + 1, NULL, 16);

    if (debugger_add_breakpoint(&stub->sim->debugger, addr) == 0)
        rsp_send_packet(fd, "OK");
    else
        rsp_send_packet(fd, "E01");  /* breakpoint table full */
}

/* 'z0,addr,kind' — remove software breakpoint */
static void handle_remove_bp(GdbStub* stub, int fd, const char* args)
{
    const char* p = strchr(args, ',');
    if (!p) { rsp_send_packet(fd, "E00"); return; }

    uint32_t addr = (uint32_t)strtoul(p + 1, NULL, 16);

    if (debugger_remove_breakpoint(&stub->sim->debugger, addr) == 0)
        rsp_send_packet(fd, "OK");
    else
        rsp_send_packet(fd, "E01");  /* not found */
}

/*
 * Handle qRcmd — the RSP packet behind GDB's "monitor <cmd>".
 * args is the hex-encoded command string (e.g. "68616c74" = "halt").
 * Cortex-Debug sends "monitor halt" and "monitor reset halt" on startup.
 */
static void handle_monitor_cmd(GdbStub* stub, int fd, const char* hex_cmd)
{
    /* Decode hex → ASCII */
    char cmd[256];
    int  len = 0;
    for (int i = 0; hex_cmd[i] && hex_cmd[i + 1] && len < (int)sizeof(cmd) - 1; i += 2) {
        char bs[3] = { hex_cmd[i], hex_cmd[i + 1], '\0' };
        cmd[len++] = (char)strtoul(bs, NULL, 16);
    }
    cmd[len] = '\0';

    /* Trim trailing whitespace / newlines */
    while (len > 0 && (cmd[len - 1] == '\n' || cmd[len - 1] == '\r' || cmd[len - 1] == ' '))
        cmd[--len] = '\0';

    if (strcmp(cmd, "halt") == 0) {
        simulator_halt(stub->sim);
    } else if (strcmp(cmd, "reset") == 0 || strcmp(cmd, "reset halt") == 0) {
        simulator_reset(stub->sim);
        simulator_halt(stub->sim);
    }
    /* Unknown monitor commands get silently accepted */
    rsp_send_packet(fd, "OK");
}

/*
 * Handle qXfer:features:read:target.xml:offset,length
 * args points to "offset,length" (hex values).
 * Response prefix: 'l' = last chunk, 'm' = more data follows.
 */
static void handle_features_xml(int fd, const char* args)
{
    uint32_t offset = (uint32_t)strtoul(args, NULL, 16);
    const char* comma = strchr(args, ',');
    if (!comma) { rsp_send_packet(fd, "E00"); return; }
    uint32_t length = (uint32_t)strtoul(comma + 1, NULL, 16);

    uint32_t xml_len = (uint32_t)strlen(TARGET_XML);

    if (offset >= xml_len) {
        rsp_send_packet(fd, "l");  /* empty last chunk */
        return;
    }

    uint32_t avail = xml_len - offset;
    if (avail > length) avail = length;
    /* Leave room for 'l'/'m' prefix + null terminator */
    if (avail > PKT_BUF_SIZE - 2) avail = PKT_BUF_SIZE - 2;

    char buf[PKT_BUF_SIZE];
    buf[0] = (offset + avail >= xml_len) ? 'l' : 'm';
    memcpy(buf + 1, TARGET_XML + offset, avail);
    buf[avail + 1] = '\0';

    rsp_send_packet(fd, buf);
}

/* ---- Main RSP session loop ---- */

static void rsp_session(GdbStub* stub, int fd)
{
    char buf[PKT_BUF_SIZE];

    while (1) {
        int len = rsp_recv_packet(fd, buf, (int)sizeof(buf));
        if (len < 0) break;

        /* GDB interrupt (Ctrl+C) */
        if (len == 1 && (unsigned char)buf[0] == 0x03) {
            simulator_halt(stub->sim);
            rsp_send_packet(fd, "S02");  /* SIGINT */
            continue;
        }

        char  cmd  = buf[0];
        char* args = buf + 1;

        switch (cmd) {

        case '?':
            rsp_send_packet(fd, "S05");
            break;

        case 'g':
            handle_read_regs(stub, fd);
            break;

        case 'G':
            handle_write_regs(stub, fd, args);
            break;

        case 'p':
            handle_read_reg(stub, fd, args);
            break;

        case 'P':
            handle_write_reg(stub, fd, args);
            break;

        case 'm':
            handle_read_mem(stub, fd, args);
            break;

        case 'M':
            handle_write_mem(stub, fd, args);
            break;

        case 'c':
            handle_continue(stub, fd, args);
            break;

        case 's':
            handle_step(stub, fd, args);
            break;

        case 'Z':
            handle_set_bp(stub, fd, args);
            break;

        case 'z':
            handle_remove_bp(stub, fd, args);
            break;

        case 'H':
            /* Thread-select — we're single-threaded, always OK */
            rsp_send_packet(fd, "OK");
            break;

        case 'T':
            /* Is thread alive? */
            rsp_send_packet(fd, "OK");
            break;

        case 'D':
            /* Detach */
            rsp_send_packet(fd, "OK");
            return;

        case 'k':
            /* Kill */
            return;

        case 'q':
            if (strncmp(args, "Supported", 9) == 0)
                rsp_send_packet(fd, "PacketSize=1000;qXfer:features:read+");
            else if (strncmp(args, "Rcmd,", 5) == 0)
                handle_monitor_cmd(stub, fd, args + 5);
            else if (strncmp(args, "Xfer:features:read:target.xml:", 30) == 0)
                handle_features_xml(fd, args + 30);
            else if (strncmp(args, "Attached", 8) == 0)
                rsp_send_packet(fd, "1");
            else if (strcmp(args, "C") == 0)
                rsp_send_packet(fd, "QC0");
            else if (strncmp(args, "fThreadInfo", 11) == 0)
                rsp_send_packet(fd, "m0");
            else if (strncmp(args, "sThreadInfo", 11) == 0)
                rsp_send_packet(fd, "l");
            else
                rsp_send_packet(fd, "");  /* unsupported query */
            break;

        default:
            rsp_send_packet(fd, "");  /* unsupported command */
            break;
        }
    }
}

/* ---- Public API ---- */

void gdb_stub_init(GdbStub* stub, Simulator* sim, int port)
{
    stub->sim       = sim;
    stub->port      = port;
    stub->server_fd = -1;
    stub->client_fd = -1;
}

void gdb_stub_run(GdbStub* stub)
{
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("gdb_stub: socket");
        return;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET,  SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(server_fd, IPPROTO_TCP, TCP_NODELAY,  &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((uint16_t)stub->port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("gdb_stub: bind");
        close(server_fd);
        return;
    }

    listen(server_fd, 1);
    stub->server_fd = server_fd;

    printf("GDB stub listening on port %d\n", stub->port);
    printf("Connect with:\n");
    printf("  arm-none-eabi-gdb -ex 'target remote :%d' firmware.elf\n\n",
           stub->port);

    /* Accept connections in a loop so GDB can reconnect after detach */
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd,
                               (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            perror("gdb_stub: accept");
            break;
        }

        int nodelay = 1;
        setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

        printf("GDB connected from %s\n", inet_ntoa(client_addr.sin_addr));
        stub->client_fd = client_fd;

        rsp_session(stub, client_fd);

        close(client_fd);
        stub->client_fd = -1;
        printf("GDB disconnected\n");
    }

    close(server_fd);
    stub->server_fd = -1;
}

void gdb_stub_close(GdbStub* stub)
{
    if (stub->client_fd >= 0) {
        close(stub->client_fd);
        stub->client_fd = -1;
    }
    if (stub->server_fd >= 0) {
        close(stub->server_fd);
        stub->server_fd = -1;
    }
}
