#include "stm32f4xx.h"

uint32_t SystemCoreClock = 16000000; // HSI by default (16 MHz)

void SystemInit(void) {
    // Default clock is HSI, no config needed for basic test
}

void SystemCoreClockUpdate(void) {
    SystemCoreClock = 16000000;
}
