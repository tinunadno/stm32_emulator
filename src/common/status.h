#ifndef STM32_STATUS_H
#define STM32_STATUS_H

/**
 * Unified status codes used across all modules.
 * Every function that can fail returns Status.
 */
typedef enum {
    STATUS_OK = 0,
    STATUS_ERROR,
    STATUS_INVALID_ADDRESS,
    STATUS_INVALID_INSTRUCTION,
    STATUS_BREAKPOINT_HIT,
    STATUS_HALTED
} Status;

#endif /* STM32_STATUS_H */
