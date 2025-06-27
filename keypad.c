#include "stm32f4xx.h"
#include "lcd.h"
#include <string.h>
#include <stdlib.h>

#define GREEN_LED_PIN 6   // PC6 - Access Granted LED
#define RED_LED_PIN   13  // PB13 - Access Denied LED

// 7-segment display pins (TM1637-style serial display)
#define SEG_CLK_PIN 2  // PA2
#define SEG_DIO_PIN 3  // PA3

char entered_key[6] = ""; // Increased size for 4-digit + 10
volatile uint32_t system_ticks = 0;
volatile uint32_t last_random_time = 0;
volatile int current_random_number = 0;
volatile int expected_security_code = 0;
volatile uint8_t access_granted = 0;

const char keymap[4][4] = {
    {'1','3','2','A'},
    {'4','6','5','B'},
    {'7','9','8','C'},
    {'*','#','0','D'}
};

// TM1637 7-segment display patterns for digits 0-9
const uint8_t seven_seg_patterns[10] = {
    0x3F, // 0
    0x06, // 1
    0x5B, // 2
    0x4F, // 3
    0x66, // 4
    0x6D, // 5
    0x7D, // 6
    0x07, // 7
    0x7F, // 8
    0x6F  // 9
};

// TM1637 Commands
#define TM1637_CMD1 0x40  // Data command
#define TM1637_CMD2 0xC0  // Address command (start from first position)
#define TM1637_CMD3 0x88  // Display control command (display on, brightness level 0)

void delayMs(uint32_t ms) {
    for(uint32_t i = 0; i < ms * 16000; i++) __NOP();
}

// Function to reverse a 4-digit number
int reverse_number(int number) {
    int reversed = 0;
    int digit1 = (number / 1000) % 10;  // Thousands
    int digit2 = (number / 100) % 10;   // Hundreds
    int digit3 = (number / 10) % 10;    // Tens
    int digit4 = number % 10;           // Units

    // Reverse the digits: 1234 becomes 4321
    reversed = digit4 * 1000 + digit3 * 100 + digit2 * 10 + digit1;
    return reversed;
}
int generate_random(void) {
    static uint32_t seed = 12345;
    seed = (seed * 1103515245 + 12345) & 0x7fffffff;
    return (seed % 9000) + 1000; // Return 1000-9999 (4-digit number)
}

// TM1637 Communication Functions
void tm1637_start(void) {
    // CLK high, DIO high
    GPIOA->ODR |= (1 << SEG_CLK_PIN);
    GPIOA->ODR |= (1 << SEG_DIO_PIN);
    delayMs(1);

    // DIO low while CLK high (start condition)
    GPIOA->ODR &= ~(1 << SEG_DIO_PIN);
    delayMs(1);

    // CLK low
    GPIOA->ODR &= ~(1 << SEG_CLK_PIN);
    delayMs(1);
}

void tm1637_stop(void) {
    // CLK low, DIO low
    GPIOA->ODR &= ~(1 << SEG_CLK_PIN);
    GPIOA->ODR &= ~(1 << SEG_DIO_PIN);
    delayMs(1);

    // CLK high
    GPIOA->ODR |= (1 << SEG_CLK_PIN);
    delayMs(1);

    // DIO high (stop condition)
    GPIOA->ODR |= (1 << SEG_DIO_PIN);
    delayMs(1);
}

void tm1637_write_byte(uint8_t data) {
    for (int i = 0; i < 8; i++) {
        // CLK low
        GPIOA->ODR &= ~(1 << SEG_CLK_PIN);
        delayMs(1);

        // Set data bit
        if (data & 0x01) {
            GPIOA->ODR |= (1 << SEG_DIO_PIN);
        } else {
            GPIOA->ODR &= ~(1 << SEG_DIO_PIN);
        }
        delayMs(1);

        // CLK high
        GPIOA->ODR |= (1 << SEG_CLK_PIN);
        delayMs(1);

        data >>= 1;
    }

    // ACK
    GPIOA->ODR &= ~(1 << SEG_CLK_PIN);
    GPIOA->ODR |= (1 << SEG_DIO_PIN);
    delayMs(1);
    GPIOA->ODR |= (1 << SEG_CLK_PIN);
    delayMs(1);
    GPIOA->ODR &= ~(1 << SEG_CLK_PIN);
    delayMs(1);
}

void display_7_segment_4_digit(int number) {
    if (number < 0) {
        // Turn off display
        tm1637_start();
        tm1637_write_byte(TM1637_CMD1);
        tm1637_stop();

        tm1637_start();
        tm1637_write_byte(TM1637_CMD2);
        tm1637_write_byte(0x00); // Clear all digits
        tm1637_write_byte(0x00);
        tm1637_write_byte(0x00);
        tm1637_write_byte(0x00);
        tm1637_stop();

        tm1637_start();
        tm1637_write_byte(0x80); // Display off
        tm1637_stop();
        return;
    }

    // Extract individual digits
    int digit1 = (number / 1000) % 10;  // Thousands
    int digit2 = (number / 100) % 10;   // Hundreds
    int digit3 = (number / 10) % 10;    // Tens
    int digit4 = number % 10;           // Units

    // Send data command
    tm1637_start();
    tm1637_write_byte(TM1637_CMD1);
    tm1637_stop();

    // Send address and data for all 4 digits
    tm1637_start();
    tm1637_write_byte(TM1637_CMD2); // Start from address 0
    tm1637_write_byte(seven_seg_patterns[digit1]); // First digit
    tm1637_write_byte(seven_seg_patterns[digit2]); // Second digit
    tm1637_write_byte(seven_seg_patterns[digit3]); // Third digit
    tm1637_write_byte(seven_seg_patterns[digit4]); // Fourth digit
    tm1637_stop();

    // Send display control command
    tm1637_start();
    tm1637_write_byte(TM1637_CMD3);
    tm1637_stop();
}

void gpio_init(void) {
    // Enable GPIOB, GPIOC, and GPIOA
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN | RCC_AHB1ENR_GPIOAEN;

    // ROWs: PB2, PB3, PB4, PB5 as output
    for (int pin = 2; pin <= 5; pin++) {
        GPIOB->MODER &= ~(3 << (pin * 2));
        GPIOB->MODER |= (1 << (pin * 2));  // Output
        GPIOB->ODR |= (1 << pin);          // Set high initially
    }

    // COLUMNs: PA9–PA12 as input with pull-up
    for (int pin = 9; pin <= 12; pin++) {
        GPIOA->MODER &= ~(3 << (pin * 2));      // Input
        GPIOA->PUPDR &= ~(3 << (pin * 2));      // Clear
        GPIOA->PUPDR |= (1 << (pin * 2));       // Pull-up
    }

    // Green LED: PC6 as output
    GPIOC->MODER &= ~(3 << (GREEN_LED_PIN * 2));
    GPIOC->MODER |= (1 << (GREEN_LED_PIN * 2));

    // Red LED: PB13 as output
    GPIOB->MODER &= ~(3 << (RED_LED_PIN * 2));
    GPIOB->MODER |= (1 << (RED_LED_PIN * 2));

    // 7-segment display pins (CLK and DIO) as output
    GPIOA->MODER &= ~(3 << (SEG_CLK_PIN * 2));
    GPIOA->MODER |= (1 << (SEG_CLK_PIN * 2));
    GPIOA->MODER &= ~(3 << (SEG_DIO_PIN * 2));
    GPIOA->MODER |= (1 << (SEG_DIO_PIN * 2));

    // Initialize 7-segment pins high
    GPIOA->ODR |= (1 << SEG_CLK_PIN);
    GPIOA->ODR |= (1 << SEG_DIO_PIN);

    // Initialize LEDs to OFF
    GPIOC->ODR &= ~(1 << GREEN_LED_PIN);
    GPIOB->ODR &= ~(1 << RED_LED_PIN);
}

void systick_init(void) {
    // Configure SysTick for 1ms interrupts (assuming 168MHz system clock)
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

char scan_keypad(void) {
    for (int row = 0; row < 4; row++) {
        // Set all rows high
        GPIOB->ODR |= (0xF << 2);

        // Pull one row low at a time
        GPIOB->ODR &= ~(1 << (row + 2));
        delayMs(1);

        for (int col = 0; col < 4; col++) {
            if (!(GPIOA->IDR & (1 << (9 + col)))) {
                while (!(GPIOA->IDR & (1 << (9 + col)))); // wait for release
                delayMs(20);
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

    // Turn off 7-segment display initially
    display_7_segment_4_digit(-1);

    while (1) {
        char key = scan_keypad();

        if (key == '*' && !access_granted) {
            lcd_print(0x80, "Security Code:");

            // Generate first random number and start timer
            generate_new_random_number();

            memset(entered_key, 0, sizeof(entered_key));
            int index = 0;

            while (1) {
                // Check if 20 seconds have passed and access not yet granted
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

                            // Turn on green LED, turn off red LED
                            GPIOC->ODR |= (1 << GREEN_LED_PIN);
                            GPIOB->ODR &= ~(1 << RED_LED_PIN);

                            // Turn off 7-segment display
                            display_7_segment_4_digit(-1);

                            access_granted = 1;
                            delayMs(3000);
                            lcd_print(0x80, "System Ready");
                            lcd_print(0xC0, "Press * to lock");
                        } else {
                            lcd_print(0x80, "Access Denied");
                            lcd_print(0xC0, "Press * to retry");

                            // Turn on red LED, turn off green LED
                            GPIOB->ODR |= (1 << RED_LED_PIN);
                            GPIOC->ODR &= ~(1 << GREEN_LED_PIN);

                            // Turn off 7-segment display
                            display_7_segment_4_digit(-1);

                            // Wait for * key to retry or timeout
                            uint32_t denied_start_time = system_ticks;
                            while ((system_ticks - denied_start_time) < 10000) { // 10 second timeout
                                char retry_key = scan_keypad();
                                if (retry_key == '*') {
                                    // Clear LCD completely and restart security code entry
                                    lcd_print(0x80, "                "); // Clear line 1
                                    lcd_print(0xC0, "                "); // Clear line 2
                                    delayMs(100); // Small delay for LCD to process

                                    lcd_print(0x80, "Security Code:");
                                    lcd_print(0xC0, "                "); // Ensure line 2 is clear

                                    // Generate new random number and reset variables
                                    generate_new_random_number();
                                    memset(entered_key, 0, sizeof(entered_key));
                                    index = 0;

                                    // Turn off red LED
                                    GPIOB->ODR &= ~(1 << RED_LED_PIN);

                                    goto continue_security_entry; // Continue in the security entry loop
                                }
                            }

                            // Timeout reached, return to welcome screen
                            lcd_print(0x80, "Welcome");
                            lcd_print(0xC0, "                ");
                            GPIOB->ODR &= ~(1 << RED_LED_PIN);
                        }
                        break;

                        continue_security_entry:; // Label for retry functionality
                    } else if (index < 5 && key != '*' && key >= '0' && key <= '9') {
                        entered_key[index++] = key;

                        char display[17] = "Code: ";
                        for (int i = 0; i < index; i++) {
                            display[6 + i] = '*';
                        }
                        display[6 + index] = '\0';
                        lcd_print(0xC0, display);
                    }
                }
            }
        } else if (key == '*' && access_granted) {
            // Lock the system again
            access_granted = 0;
            GPIOC->ODR &= ~(1 << GREEN_LED_PIN);
            GPIOB->ODR &= ~(1 << RED_LED_PIN);
            lcd_print(0x80, "Welcome");
            lcd_print(0xC0, "                "); // Clear second line
        }
    }
}
