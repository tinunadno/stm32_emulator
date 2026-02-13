#ifndef STM32_UI_H
#define STM32_UI_H

#include "simulator/simulator.h"

/**
 * CLI command handler function type.
 * @param sim  Pointer to the simulator
 * @param args Argument string (everything after the command name), may be NULL
 */
typedef void (*CommandHandler)(Simulator* sim, const char* args);

/**
 * CLI command table entry.
 * To add a new command:
 *   1. Write a handler function matching CommandHandler signature
 *   2. Add an entry to the commands[] table in ui.c
 */
typedef struct {
    const char*    name;
    const char*    help;
    CommandHandler handler;
} Command;

/**
 * Start the interactive CLI loop.
 * Blocks until the user types "quit".
 */
void ui_run(Simulator* sim);

#endif /* STM32_UI_H */
