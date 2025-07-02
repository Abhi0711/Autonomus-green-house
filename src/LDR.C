#include <stdint.h>
#include<stdio.h>
#include<stm32f405xx.h>
#include<lcd.h>
#include "stm32f4xx.h"
 
// Simple software delay
void delay_ms(uint32_t ms) {
    for (uint32_t i = 0; i < ms * 8000; i++) {
        __NOP();
    }
}
 
// Initialize GPIO for LEDs on PA2, PA3, PA6, PA7, PB8
void GPIO_Init(void) {
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN;
 
    // Set PA2, PA3, PA6, PA7 to output mode
    GPIOA->MODER |= (1 << (2 * 2)) | (1 << (3 * 2)) | (1 << (6 * 2)) | (1 << (7 * 2));
    // Set PB8 to output mode
    GPIOB->MODER |= (1 << (8 * 2));
}
 
// Initialize ADC1 on PA5 (Channel 5)
void ADC1_Init(void) {
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
 
    // Set PA5 to analog mode
    GPIOA->MODER |= (3 << (5 * 2)); // Analog mode (11)
 
    ADC1->CR2 = 0;              // Clear ADC config
    ADC1->SQR3 = 5;             // Channel 5 (PA5)
    ADC1->SMPR2 |= (7 << 15);   // Max sample time
    ADC1->CR2 |= ADC_CR2_ADON; // Enable ADC
}
 
// Read value from ADC1
uint16_t ADC1_Read(void) {
    ADC1->CR2 |= ADC_CR2_SWSTART;        // Start conversion
    while (!(ADC1->SR & ADC_SR_EOC));    // Wait for conversion to complete
    return ADC1->DR;                     // Return the result
}
 
// Control LED Bar Graph based on level (0-5)
void Set_LEDs(uint8_t level) {
    // Turn all LEDs OFF
    GPIOA->ODR &= ~((1 << 2) | (1 << 3) | (1 << 6) | (1 << 7));
    GPIOB->ODR &= ~(1 << 8);
 
    // Turn ON LEDs up to level
    if (level >= 1) GPIOA->ODR |= (1 << 2);  // C1
    if (level >= 2) GPIOA->ODR |= (1 << 3);  // C3
    if (level >= 3) GPIOA->ODR |= (1 << 6);  // C5
    if (level >= 4) GPIOA->ODR |= (1 << 7);  // C7
    if (level >= 5) GPIOB->ODR |= (1 << 8);  // C9
}
 
int main(void) {
    GPIO_Init();
    ADC1_Init();
 
    while (1) {
        uint16_t adc_val = ADC1_Read(); // Read ADC value (0-4095)
 
        // Invert logic: more LEDs ON in DARK (low ADC) and fewer in BRIGHT (high ADC)
        uint8_t level = 5-((adc_val * 6) / 4096);
        level = 5 - level;                   // Invert for dark = more LEDs
 
        Set_LEDs(level);
 
        delay_ms(200);
    }
}
