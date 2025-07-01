#include "stm32f4xx.h"

void delay_ms(uint32_t ms) {
    SysTick->LOAD = (SystemCoreClock / 1000) * ms - 1;
    SysTick->VAL = 0;
    SysTick->CTRL = 5;
    while (!(SysTick->CTRL & (1 << 16)));
    SysTick->CTRL = 0;
}

void GPIO_Init(void) {
    // Enable GPIOB and GPIOC
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN;

    // PC6, PC7, PC8, PC9, PC13 as output
    GPIOC->MODER |= (1 << (6 * 2)) | (1 << (7 * 2)) |
                    (1 << (8 * 2)) | (1 << (9 * 2)) |
                    (1 << (13 * 2));
    GPIOC->OTYPER &= ~((1 << 6) | (1 << 7) | (1 << 8) | (1 << 9) | (1 << 13));
    GPIOC->OSPEEDR |= (3 << (6 * 2)) | (3 << (7 * 2)) |
                      (3 << (8 * 2)) | (3 << (9 * 2)) |
                      (3 << (13 * 2));

    // PB6 as Alternate Function for TIM4_CH1
    GPIOB->MODER &= ~(3 << (6 * 2));
    GPIOB->MODER |= (2 << (6 * 2));       // Alternate function mode
    GPIOB->AFR[0] |= (2 << (6 * 4));      // AF2 = TIM4_CH1
}

void TIM4_PWM_CH1_Init(void) {
    RCC->APB1ENR |= RCC_APB1ENR_TIM4EN;

    TIM4->PSC = 84 - 1;            // 1 MHz (assuming 84 MHz system clock)
    TIM4->ARR = 1000 - 1;          // PWM frequency = 1 kHz
    TIM4->CCR1 = 800;              // Duty cycle = 80%

    TIM4->CCMR1 |= (6 << 4);       // PWM Mode 1
    TIM4->CCMR1 |= TIM_CCMR1_OC1PE; // Output preload enable
    TIM4->CCER |= TIM_CCER_CC1E;  // Enable output on CH1
    TIM4->CR1 |= TIM_CR1_ARPE;    // Auto-reload preload
    TIM4->CR1 |= TIM_CR1_CEN;     // Enable Timer
}

void Motor_Run(void) {
    // Enable STBY
    GPIOC->ODR |= (1 << 13);

    // Motor A forward: AIN1 = 1, AIN2 = 0
    GPIOC->ODR |= (1 << 6);
    GPIOC->ODR &= ~(1 << 7);

    // Motor B forward: BIN1 = 1, BIN2 = 0
    GPIOC->ODR |= (1 << 8);
    GPIOC->ODR &= ~(1 << 9);
}

int main(void) {
    GPIO_Init();
    TIM4_PWM_CH1_Init();
    Motor_Run();

    while (1) {
        // Motors running at 80% speed
    }
}
