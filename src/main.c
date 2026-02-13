#include <stdio.h>
#include "simulator/simulator.h"
#include "ui/ui.h"

int main(int argc, char* argv[])
{
    Simulator sim;
    simulator_init(&sim);

    /* If a binary path is provided as argument, load it */
    if (argc > 1) {
        Status s = simulator_load(&sim, argv[1]);
        if (s != STATUS_OK) {
            fprintf(stderr, "Failed to load '%s'\n", argv[1]);
            return 1;
        }
    }

    /* Enter interactive CLI */
    ui_run(&sim);

    return 0;
}
