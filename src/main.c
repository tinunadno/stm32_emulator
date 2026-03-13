#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "simulator/simulator.h"
#include "ui/ui.h"
#include "gdb_stub/gdb_stub.h"

static void usage(const char* prog)
{
    fprintf(stderr,
            "Usage: %s [binary.bin] [--gdb [port]]\n"
            "\n"
            "  binary.bin   STM32 binary to load into flash\n"
            "  --gdb        Start GDB RSP server (default port %d)\n"
            "  --gdb PORT   Start GDB RSP server on PORT\n"
            "\n"
            "Examples:\n"
            "  %s firmware.bin              # interactive CLI\n"
            "  %s firmware.bin --gdb        # GDB server on port %d\n"
            "  %s firmware.bin --gdb 4444   # GDB server on port 4444\n",
            prog,
            GDB_STUB_DEFAULT_PORT,
            prog, prog, GDB_STUB_DEFAULT_PORT, prog);
}

int main(int argc, char* argv[])
{
    Simulator sim;
    simulator_init(&sim);

    const char* binary  = NULL;
    int         gdb_port = 0;   /* 0 = no GDB server */

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--gdb") == 0) {
            gdb_port = GDB_STUB_DEFAULT_PORT;
            /* Optional next arg: port number */
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                gdb_port = atoi(argv[++i]);
                if (gdb_port <= 0 || gdb_port > 65535) {
                    fprintf(stderr, "Invalid port: %s\n", argv[i]);
                    return 1;
                }
            }
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            binary = argv[i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    if (binary) {
        Status s = simulator_load(&sim, binary);
        if (s != STATUS_OK) {
            fprintf(stderr, "Failed to load '%s'\n", binary);
            return 1;
        }
    }

    if (gdb_port) {
        GdbStub stub;
        gdb_stub_init(&stub, &sim, gdb_port);
        gdb_stub_run(&stub);   /* blocks */
    } else {
        ui_run(&sim);
    }

    return 0;
}
