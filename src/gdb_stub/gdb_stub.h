#ifndef STM32_GDB_STUB_H
#define STM32_GDB_STUB_H

#include "simulator/simulator.h"

#define GDB_STUB_DEFAULT_PORT 3333

/**
 * GDB Remote Serial Protocol (RSP) stub.
 * Listens on a TCP port and allows arm-none-eabi-gdb to connect and
 * debug firmware running inside the emulator.
 *
 * Usage:
 *   GdbStub stub;
 *   gdb_stub_init(&stub, &sim, 3333);
 *   gdb_stub_run(&stub);   // blocks, accepts connections in a loop
 */
typedef struct {
    Simulator* sim;
    int        server_fd;
    int        client_fd;
    int        port;
} GdbStub;

void gdb_stub_init(GdbStub* stub, Simulator* sim, int port);

/** Start listening and accepting GDB connections. Blocks until error. */
void gdb_stub_run(GdbStub* stub);

void gdb_stub_close(GdbStub* stub);

#endif /* STM32_GDB_STUB_H */
