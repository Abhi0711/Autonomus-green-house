#include "stm32f4xx.h"
#include "lcd.h"
#include <string.h>
#include <stdlib.h>

#define GREEN_LED_PIN 6   // PC6 - Access Granted LED
#define RED_LED_PIN   13  // PB13 - Access Denied LED

// 7-segment display on PC0 (CLK) and PC1 (DIO)
#define SEG_CLK_PIN 0
#define SEG_DIO_PIN 1
#define SEG_PORT GPIOC

char entered_key[6] = "";
volatile uint32_t system_ticks = 0;
volatile uint32_t last_random_time = 0;
volatile int current_random_number = 0;
volatile int expected_security_code = 0;
volatile uint8_t access_granted = 0;

const char keymap[4][4] = {
    {'1','2','3','A'},
    {'4','5','6','B'},
    {'7','8','9','C'},
    {'*','0','#','D'}
};

const uint8_t seven_seg_patterns[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66,
    0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

#define TM1637_CMD1 0x40
#define TM1637_CMD2 0xC0
#define TM1637_CMD3 0x88

// Optimized delay function - much faster
void delayUs(uint32_t us) {
    for(uint32_t i = 0; i < us * 16; i++) __NOP();
}

void delayMs(uint32_t ms) {
    for(uint32_t i = 0; i < ms; i++) {
        delayUs(1000);
    }
}

int reverse_number(int number) {
    int digit1 = (number / 1000) % 10;
    int digit2 = (number / 100) % 10;
    int digit3 = (number / 10) % 10;
    int digit4 = number % 10;
    return digit4 * 1000 + digit3 * 100 + digit2 * 10 + digit1;
}

int generate_random(void) {
    static uint32_t seed = 12345;
    seed = (seed * 1103515245 + 12345) & 0x7fffffff;
    return (seed % 9000) + 1000;
}

// TM1637 functions with proper timing for reliability
void tm1637_start(void) {
    SEG_PORT->ODR |= (1 << SEG_CLK_PIN) | (1 << SEG_DIO_PIN);
    delayUs(50);
    SEG_PORT->ODR &= ~(1 << SEG_DIO_PIN);
    delayUs(50);
    SEG_PORT->ODR &= ~(1 << SEG_CLK_PIN);
    delayUs(50);
}

void tm1637_stop(void) {
    SEG_PORT->ODR &= ~(1 << SEG_CLK_PIN);
    SEG_PORT->ODR &= ~(1 << SEG_DIO_PIN);
    delayUs(50);
    SEG_PORT->ODR |= (1 << SEG_CLK_PIN);
    delayUs(50);
    SEG_PORT->ODR |= (1 << SEG_DIO_PIN);
    delayUs(50);
}

void tm1637_write_byte(uint8_t data) {
    for (int i = 0; i < 8; i++) {
        SEG_PORT->ODR &= ~(1 << SEG_CLK_PIN);
        delayUs(30);
        if (data & 0x01)
            SEG_PORT->ODR |= (1 << SEG_DIO_PIN);
        else
            SEG_PORT->ODR &= ~(1 << SEG_DIO_PIN);
        delayUs(30);
        SEG_PORT->ODR |= (1 << SEG_CLK_PIN);
        delayUs(30);
        data >>= 1;
    }

    // ACK bit handling
    SEG_PORT->ODR &= ~(1 << SEG_CLK_PIN);
    SEG_PORT->ODR |= (1 << SEG_DIO_PIN);
    delayUs(30);
    SEG_PORT->ODR |= (1 << SEG_CLK_PIN);
    delayUs(30);
    SEG_PORT->ODR &= ~(1 << SEG_CLK_PIN);
    delayUs(30);
}

void display_7_segment_4_digit(int number) {
    if (number < 0) {
        tm1637_start();
        tm1637_write_byte(TM1637_CMD1);
        tm1637_stop();
        delayUs(100);

        tm1637_start();
        tm1637_write_byte(TM1637_CMD2);
        tm1637_write_byte(0x00);
        tm1637_write_byte(0x00);
        tm1637_write_byte(0x00);
        tm1637_write_byte(0x00);
        tm1637_stop();
        delayUs(100);

        tm1637_start();
        tm1637_write_byte(TM1637_CMD3);
        tm1637_stop();
        delayUs(100);
        return;
    }

    int digit1 = (number / 1000) % 10;
    int digit2 = (number / 100) % 10;
    int digit3 = (number / 10) % 10;
    int digit4 = number % 10;

    // Set data command
    tm1637_start();
    tm1637_write_byte(TM1637_CMD1);
    tm1637_stop();
    delayUs(100);

    // Set address and send data
    tm1637_start();
    tm1637_write_byte(TM1637_CMD2);
    tm1637_write_byte(seven_seg_patterns[digit1]);
    tm1637_write_byte(seven_seg_patterns[digit2]);
    tm1637_write_byte(seven_seg_patterns[digit3]);
    tm1637_write_byte(seven_seg_patterns[digit4]);
    tm1637_stop();
    delayUs(100);

    // Set display control (brightness)
    tm1637_start();
    tm1637_write_byte(TM1637_CMD3);
    tm1637_stop();
    delayUs(100);
}

void gpio_init(void) {
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN | RCC_AHB1ENR_GPIOAEN;

    for (int pin = 2; pin <= 5; pin++) {
        GPIOB->MODER &= ~(3 << (pin * 2));
        GPIOB->MODER |= (1 << (pin * 2));
        GPIOB->ODR |= (1 << pin);
    }

    for (int pin = 9; pin <= 12; pin++) {
        GPIOA->MODER &= ~(3 << (pin * 2));
        GPIOA->PUPDR &= ~(3 << (pin * 2));
        GPIOA->PUPDR |= (1 << (pin * 2));
    }

    GPIOC->MODER &= ~(3 << (GREEN_LED_PIN * 2));
    GPIOC->MODER |= (1 << (GREEN_LED_PIN * 2));

    GPIOB->MODER &= ~(3 << (RED_LED_PIN * 2));
    GPIOB->MODER |= (1 << (RED_LED_PIN * 2));

    SEG_PORT->MODER &= ~(3 << (SEG_CLK_PIN * 2));
    SEG_PORT->MODER |= (1 << (SEG_CLK_PIN * 2));
    SEG_PORT->MODER &= ~(3 << (SEG_DIO_PIN * 2));
    SEG_PORT->MODER |= (1 << (SEG_DIO_PIN * 2));

    SEG_PORT->ODR |= (1 << SEG_CLK_PIN);
    SEG_PORT->ODR |= (1 << SEG_DIO_PIN);

    GPIOC->ODR &= ~(1 << GREEN_LED_PIN);
    GPIOB->ODR &= ~(1 << RED_LED_PIN);
}

void systick_init(void) {
    SysTick_Config(SystemCoreClock / 1000);
}

void SysTick_Handler(void) {
    system_ticks++;
}

void generate_new_random_number(void) {
    current_random_number = generate_random();
    expected_security_code = reverse_number(current_random_number);
    display_7_segment_4_digit(current_random_number);
    last_random_time = system_ticks;
}

// Fast keypad scanning with minimal debounce
char scan_keypad(void) {
    for (int row = 0; row < 4; row++) {
        GPIOB->ODR |= (0xF << 2);
        GPIOB->ODR &= ~(1 << (row + 2));
        delayUs(50); // Minimal setup time

        for (int col = 0; col < 4; col++) {
            if (!(GPIOA->IDR & (1 << (9 + col)))) {
                while (!(GPIOA->IDR & (1 << (9 + col))));
                delayMs(5); // Very quick debounce for faster response
                return keymap[row][col];
            }
        }
    }
    return 0;
}

int main(void) {
    gpio_init();
    systick_init();
    LcdInit();

    lcd_print(0x80, "Welcome");
    display_7_segment_4_digit(-1);

    while (1) {
        char key = scan_keypad();

        if (key == '*' && !access_granted) {
            lcd_print(0x80, "Security Code:");
            generate_new_random_number();
            memset(entered_key, 0, sizeof(entered_key));
            int index = 0;

            while (1) {
                if (!access_granted && (system_ticks - last_random_time) >= 20000) {
                    generate_new_random_number();
                }

                key = scan_keypad();
                if (key) {
                    if (key == '#') {
                        entered_key[index] = '\0';
                        int entered_code = atoi(entered_key);

                        if (entered_code == expected_security_code) {
                            lcd_print(0x80, "Access Granted");
                            lcd_print(0xC0, "Welcome!");
                            GPIOC->ODR |= (1 << GREEN_LED_PIN);
                            GPIOB->ODR &= ~(1 << RED_LED_PIN);
                            display_7_segment_4_digit(-1);
                            access_granted = 1;
                            delayMs(2000); // Reduced from 3000ms to 2000ms
                            lcd_print(0x80, "System Ready");
                            lcd_print(0xC0, "Press * to lock");
                        } else {
                            lcd_print(0x80, "Access Denied");
                            lcd_print(0xC0, "Press * to retry");
                            GPIOB->ODR |= (1 << RED_LED_PIN);
                            GPIOC->ODR &= ~(1 << GREEN_LED_PIN);
                            display_7_segment_4_digit(-1);

                            uint32_t denied_start_time = system_ticks;
                            while ((system_ticks - denied_start_time) < 10000) {
                                char retry_key = scan_keypad();
                                if (retry_key == '*') {
                                    lcd_print(0x80, "                ");
                                    lcd_print(0xC0, "                ");
                                    delayMs(50); // Reduced from 100ms to 50ms
                                    lcd_print(0x80, "Security Code:");
                                    lcd_print(0xC0, "                ");
                                    generate_new_random_number();
                                    memset(entered_key, 0, sizeof(entered_key));
                                    index = 0;
                                    GPIOB->ODR &= ~(1 << RED_LED_PIN);
                                    goto continue_security_entry;
                                }
                            }
                            lcd_print(0x80, "Welcome");
                            lcd_print(0xC0, "                ");
                            GPIOB->ODR &= ~(1 << RED_LED_PIN);
                        }
                        break;

                        continue_security_entry:;
                    } else if (index < 5 && key != '*' && key >= '0' && key <= '9') {
                        entered_key[index++] = key;
                        char display[17] = "Code: ";
                        for (int i = 0; i < index; i++)
                            display[6 + i] = '*';
                        display[6 + index] = '\0';
                        lcd_print(0xC0, display);
                    }
                }
            }
        } else if (key == '*' && access_granted) {
            access_granted = 0;
            GPIOC->ODR &= ~(1 << GREEN_LED_PIN);
            GPIOB->ODR &= ~(1 << RED_LED_PIN);
            lcd_print(0x80, "Welcome");
            lcd_print(0xC0, "                ");
        }
    }
}
