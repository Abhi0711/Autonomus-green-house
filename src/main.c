#include "stm32f4xx.h"
#include "lcd.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

// --- Security System Definitions ---
#define GREEN_LED_PIN 2   // PC2 - Access Granted LED
#define RED_LED_PIN   3   // PC3 - Access Denied LED
#define SEG_CLK_PIN 0     // 7-segment CLK - PC0
#define SEG_DIO_PIN 1     // 7-segment DIO - PC1
#define SEG_PORT GPIOC

// --- Environmental System Definitions ---
#define DHT11_PORT GPIOA
#define DHT11_PIN  4
#define TEMP_THRESHOLD 19
#define HUMIDITY_THRESHOLD 75

#define STBY_PIN   13
#define AIN1_PIN   6
#define AIN2_PIN   7
#define BIN1_PIN   8
#define BIN2_PIN   9
#define PWMA_PIN   6

#define RELAY_PIN  8
#define BUTTON_PIN 7

// --- Global Variables ---
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

// --- Delay Functions ---
void delayUs(uint32_t us) {
    for(uint32_t i = 0; i < us * 16; i++) __NOP();
}

void delayMs(uint32_t ms) {
    for(uint32_t i = 0; i < ms; i++) delayUs(1000);
}

void delay_us(uint32_t us) {
    SysTick->LOAD = (SystemCoreClock / 1000000) * us - 1;
    SysTick->VAL = 0;
    SysTick->CTRL = 5;
    while (!(SysTick->CTRL & (1 << 16)));
    SysTick->CTRL = 0;
}

void delay_ms(uint32_t ms) {
    SysTick->LOAD = (SystemCoreClock / 1000) * ms - 1;
    SysTick->VAL = 0;
    SysTick->CTRL = 5;
    while (!(SysTick->CTRL & (1 << 16)));
    SysTick->CTRL = 0;
}

// --- Utility Functions ---
void single_print(uint8_t num) {
    char buffer[4];
    sprintf(buffer, "%d", num);
    for (char *p = buffer; *p; p++) {
        lcd_data(*p);
    }
}

int reverse_number(int number) {
    int digit1 = (number / 1000) % 10;
    int digit2 = (number / 100) % 10;
    int digit3 = (number / 10) % 10;
    int digit4 = number % 10;
    return digit4 * 1000 + digit3 * 100 + digit2 * 10 + digit1;
}

int is_palindrome(int num) {
    return num == reverse_number(num);
}

// --- 7-Segment Display Functions ---
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
        tm1637_start(); tm1637_write_byte(TM1637_CMD1); tm1637_stop(); delayUs(100);
        tm1637_start(); tm1637_write_byte(TM1637_CMD2);
        for (int i = 0; i < 4; i++) tm1637_write_byte(0x00);
        tm1637_stop(); delayUs(100);
        tm1637_start(); tm1637_write_byte(TM1637_CMD3); tm1637_stop(); delayUs(100);
        return;
    }
    int d1 = (number / 1000) % 10, d2 = (number / 100) % 10;
    int d3 = (number / 10) % 10, d4 = number % 10;
    tm1637_start(); tm1637_write_byte(TM1637_CMD1); tm1637_stop(); delayUs(100);
    tm1637_start(); tm1637_write_byte(TM1637_CMD2);
    tm1637_write_byte(seven_seg_patterns[d1]);
    tm1637_write_byte(seven_seg_patterns[d2]);
    tm1637_write_byte(seven_seg_patterns[d3]);
    tm1637_write_byte(seven_seg_patterns[d4]);
    tm1637_stop(); delayUs(100);
    tm1637_start(); tm1637_write_byte(TM1637_CMD3); tm1637_stop(); delayUs(100);
}

// --- GPIO Initialization ---
void security_gpio_init(void) {
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN | RCC_AHB1ENR_GPIOAEN;

    // Keypad rows (PB2-PB5)
    for (int pin = 2; pin <= 5; pin++) {
        GPIOB->MODER &= ~(3 << (pin * 2));
        GPIOB->MODER |= (1 << (pin * 2));
        GPIOB->ODR |= (1 << pin);
    }

    // Keypad columns (PA9-PA12)
    for (int pin = 9; pin <= 12; pin++) {
        GPIOA->MODER &= ~(3 << (pin * 2));
        GPIOA->PUPDR &= ~(3 << (pin * 2));
        GPIOA->PUPDR |= (1 << (pin * 2));
    }

    // Security LEDs
    GPIOC->MODER &= ~(3 << (GREEN_LED_PIN * 2));
    GPIOC->MODER |= (1 << (GREEN_LED_PIN * 2));
    GPIOC->MODER &= ~(3 << (RED_LED_PIN * 2));
    GPIOC->MODER |= (1 << (RED_LED_PIN * 2));

    // 7-segment display
    SEG_PORT->MODER &= ~(3 << (SEG_CLK_PIN * 2));
    SEG_PORT->MODER |= (1 << (SEG_CLK_PIN * 2));
    SEG_PORT->MODER &= ~(3 << (SEG_DIO_PIN * 2));
    SEG_PORT->MODER |= (1 << (SEG_DIO_PIN * 2));
    SEG_PORT->ODR |= (1 << SEG_CLK_PIN) | (1 << SEG_DIO_PIN);

    GPIOC->ODR &= ~(1 << GREEN_LED_PIN);
    GPIOC->ODR &= ~(1 << RED_LED_PIN);
}

void environmental_gpio_init(void) {
    // Environmental relay control
    GPIOA->MODER |= (1 << (RELAY_PIN * 2));

    // Button input
    GPIOB->MODER &= ~(3 << (BUTTON_PIN * 2));
    GPIOB->PUPDR |= (1 << (BUTTON_PIN * 2));

    // Motor control
    GPIOC->MODER |= (1 << (AIN1_PIN*2)) | (1 << (AIN2_PIN*2)) |
                    (1 << (BIN1_PIN*2)) | (1 << (BIN2_PIN*2)) | (1 << (STBY_PIN*2));

    // LED bar
    GPIOA->MODER |= (1 << (2*2)) | (1 << (3*2)) | (1 << (6*2)) | (1 << (7*2));
    GPIOB->MODER |= (1 << (8*2));
}

// --- System Initialization ---
void systick_init(void) {
    SysTick_Config(SystemCoreClock / 1000);
}

void SysTick_Handler(void) {
    system_ticks++;
}

// --- ADC Functions ---
void ADC1_Init(void) {
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    GPIOA->MODER |= (3 << (5 * 2)); // PA5 Analog
    ADC1->CR2 = 0;
    ADC1->SQR3 = 5;
    ADC1->SMPR2 |= (7 << 15);
    ADC1->CR2 |= ADC_CR2_ADON;
}

uint16_t ADC1_Read(void) {
    ADC1->CR2 |= ADC_CR2_SWSTART;
    while (!(ADC1->SR & ADC_SR_EOC));
    return ADC1->DR;
}

// --- LED Bar Functions ---
void Set_LEDs(uint8_t level) {
    GPIOA->ODR &= ~((1 << 2) | (1 << 3) | (1 << 6) | (1 << 7));
    GPIOB->ODR &= ~(1 << 8);
    if (level >= 1) GPIOA->ODR |= (1 << 2);
    if (level >= 2) GPIOA->ODR |= (1 << 3);
    if (level >= 3) GPIOA->ODR |= (1 << 6);
    if (level >= 4) GPIOA->ODR |= (1 << 7);
    if (level >= 5) GPIOB->ODR |= (1 << 8);
}

// --- DHT11 Functions ---
void DHT11_Out(void) {
    DHT11_PORT->MODER &= ~(0x3 << (DHT11_PIN * 2));
    DHT11_PORT->MODER |=  (0x1 << (DHT11_PIN * 2));
}

void DHT11_In(void) {
    DHT11_PORT->MODER &= ~(0x3 << (DHT11_PIN * 2));
    DHT11_PORT->PUPDR &= ~(0x3 << (DHT11_PIN * 2));
    DHT11_PORT->PUPDR |=  (0x1 << (DHT11_PIN * 2));
}

void DHT11_Write(uint8_t val) {
    if (val) DHT11_PORT->BSRR = (1 << DHT11_PIN);
    else DHT11_PORT->BSRR = (1 << (DHT11_PIN + 16));
}

uint8_t DHT11_Read(void) {
    return (DHT11_PORT->IDR >> DHT11_PIN) & 0x1;
}

int dht11_read(uint8_t *t, uint8_t *h) {
    uint8_t data[5] = {0};

    DHT11_Out(); DHT11_Write(0); delay_ms(18);
    DHT11_Write(1); delay_us(30); DHT11_In();
    delay_us(40);
    if (DHT11_Read()) return -1;
    delay_us(80);
    if (!DHT11_Read()) return -1;
    delay_us(80);

    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 8; j++) {
            while (!DHT11_Read());
            delay_us(40);
            if (DHT11_Read()) data[i] |= (1 << (7 - j));
            while (DHT11_Read());
        }
    }

    if ((data[0]+data[1]+data[2]+data[3]) != data[4]) return -1;
    *h = data[0]; *t = data[2];
    return 0;
}

// --- Motor Control Functions ---
void TIM4_PWM_Init(void) {
    RCC->APB1ENR |= RCC_APB1ENR_TIM4EN;
    TIM4->PSC = 84 - 1; TIM4->ARR = 1000 - 1; TIM4->CCR1 = 0;
    TIM4->CCMR1 |= (6 << 4) | (1 << 3);
    TIM4->CCER |= 1; TIM4->CR1 |= (1 << 7) | 1;
}

void Motor_Run(uint16_t speed) {
    GPIOC->ODR |= (1 << STBY_PIN);
    GPIOC->ODR |= (1 << AIN1_PIN); GPIOC->ODR &= ~(1 << AIN2_PIN);
    GPIOC->ODR |= (1 << BIN1_PIN); GPIOC->ODR &= ~(1 << BIN2_PIN);
    if (speed > 1000) speed = 1000;
    for (int s = 0; s <= speed; s += 50) {
        TIM4->CCR1 = s;
        delay_ms(5);
    }
}

void Motor_Stop(void) {
    for (int s = TIM4->CCR1; s > 0; s -= 50) {
        TIM4->CCR1 = s;
        delay_ms(5);
    }
    TIM4->CCR1 = 0;
    GPIOC->ODR &= ~(1 << STBY_PIN);
}

void Relay_Control(uint8_t state) {
    if (state) GPIOA->ODR |= (1 << RELAY_PIN);
    else GPIOA->ODR &= ~(1 << RELAY_PIN);
}

// --- Security System Functions ---
void generate_new_random_number(void) {
    static int last_number = -1;
    int try_count = 0;
    do {
        current_random_number = ((system_ticks + try_count * 12345) * 1103515245 + 12345) & 0x7FFFFFFF;
        current_random_number = (current_random_number % 9000) + 1000;
        try_count++;
        if (try_count > 100) break;
    } while (is_palindrome(current_random_number) || current_random_number == last_number);
    last_number = current_random_number;
    expected_security_code = reverse_number(current_random_number);
    display_7_segment_4_digit(current_random_number);
    last_random_time = system_ticks;
}

char scan_keypad(void) {
    for (int row = 0; row < 4; row++) {
        GPIOB->ODR |= (0xF << 2);
        GPIOB->ODR &= ~(1 << (row + 2));
        delayUs(50);
        for (int col = 0; col < 4; col++) {
            if (!(GPIOA->IDR & (1 << (9 + col)))) {
                while (!(GPIOA->IDR & (1 << (9 + col))));
                delayMs(5);
                return keymap[row][col];
            }
        }
    }
    return 0;
}

// --- Main Program ---
int main(void) {
    SystemCoreClockUpdate();

    // Initialize all peripherals
    security_gpio_init();
    environmental_gpio_init();
    systick_init();
    ADC1_Init();
    TIM4_PWM_Init();
    LcdInit();

    // Initial display
    lcd_print(0x80, "Security System");
    lcd_print(0xC0, "Press * to start");
    display_7_segment_4_digit(-1);

    // Environmental monitoring variables
    uint8_t temp = 0, hum = 0, relay_state = 0;
    uint8_t manual_control = 0, button_prev = 1, motor_on = 0;

    while (1) {
        // --- SECURITY SYSTEM PHASE ---
        if (!access_granted) {
            char key = scan_keypad();
            if (key == '*') {
                lcd_print(0x80, "Security Code:  ");
                lcd_print(0xC0, "                ");
                generate_new_random_number();
                memset(entered_key, 0, sizeof(entered_key));
                int index = 0;

                while (!access_granted) {
                    if ((system_ticks - last_random_time) >= 20000) {
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
                                GPIOC->ODR &= ~(1 << RED_LED_PIN);
                                display_7_segment_4_digit(-1);
                                access_granted = 1;
                                delayMs(200);
                                lcd_print(0x80, "Starting Env    ");
                                lcd_print(0xC0, "Monitoring...   ");
                                delayMs(150);
                            } else {
                                lcd_print(0x80, "Access Denied");
                                lcd_print(0xC0, "Press * to retry");
                                GPIOC->ODR |= (1 << RED_LED_PIN);
                                GPIOC->ODR &= ~(1 << GREEN_LED_PIN);
                                display_7_segment_4_digit(-1);
                                uint32_t denied_start_time = system_ticks;
                                while ((system_ticks - denied_start_time) < 10000) {
                                    char retry_key = scan_keypad();
                                    if (retry_key == '*') {
                                        lcd_print(0x80, "                ");
                                        lcd_print(0xC0, "                ");
                                        delayMs(50);
                                        lcd_print(0x80, "Security Code:  ");
                                        lcd_print(0xC0, "                ");
                                        generate_new_random_number();
                                        memset(entered_key, 0, sizeof(entered_key));
                                        index = 0;
                                        GPIOC->ODR &= ~(1 << RED_LED_PIN);
                                        goto continue_security_entry;
                                    }
                                }
                                lcd_print(0x80, "Security System ");
                                lcd_print(0xC0, "Press * to start");
                                GPIOC->ODR &= ~(1 << RED_LED_PIN);
                            }
                            break;
                            continue_security_entry:;
                        } else if (index < 5 && key != '*' && key >= '0' && key <= '9') {
                            entered_key[index++] = key;
                            char display[17] = "Code: ";
                            for (int i = 0; i < index; i++) display[6 + i] = '*';
                            display[6 + index] = '\0';
                            lcd_print(0xC0, display);
                        }
                    }
                }
            }
        }

        // --- ENVIRONMENTAL MONITORING PHASE ---
        if (access_granted) {
            // Check for lock command
            char key = scan_keypad();
            if (key == '*') {
                access_granted = 0;
                GPIOC->ODR &= ~(1 << GREEN_LED_PIN);
                GPIOC->ODR &= ~(1 << RED_LED_PIN);
                Motor_Stop();
                Relay_Control(0);
                motor_on = 0;
                relay_state = 0;
                lcd_print(0x80, "System Locked   ");
                lcd_print(0xC0, "Press * to unlock");
                delayMs(200);
                continue;
            }

            // Read LDR and update LED bar
            uint16_t adc_val = ADC1_Read();
            uint8_t level = 5 - ((adc_val * 6) / 4096);
            level = 5 - level;
            Set_LEDs(level);

            // Button control for manual relay toggle
            uint8_t button_now = (GPIOB->IDR & (1 << BUTTON_PIN)) ? 1 : 0;
            if (button_prev == 1 && button_now == 0) {
                manual_control = !manual_control;
                if (manual_control) {
                    relay_state = !relay_state;
                    Relay_Control(relay_state);
                }
                delayMs(50);
            }
            button_prev = button_now;

            // Read DHT11 and control devices
            if (dht11_read(&temp, &hum) == 0) {
                lcd_command(0x80);
                lcd_print(0x80, "T:"); single_print(temp);
                lcd_data('C');
                lcd_print(0x00, " H:"); single_print(hum);
                lcd_data('%');

                // Temperature-based fan control
                if (temp >= TEMP_THRESHOLD && !motor_on) {
                    Motor_Run(800); motor_on = 1;
                } else if (temp < TEMP_THRESHOLD && motor_on) {
                    Motor_Stop(); motor_on = 0;
                }

                // Humidity-based relay control (if not in manual mode)
                if (!manual_control) {
                    if (hum >= HUMIDITY_THRESHOLD && !relay_state) {
                        relay_state = 1; Relay_Control(1);
                    } else if (hum < HUMIDITY_THRESHOLD - 2 && relay_state) {
                        relay_state = 0; Relay_Control(0);
                    }
                }

                // Display system status
                lcd_command(0xC0);
                if (motor_on && relay_state) lcd_print(0xC0, "Fan+Light ON    ");
                else if (motor_on) lcd_print(0xC0, "Fan ON          ");
                else if (relay_state) lcd_print(0xC0, "Light ON        ");
                else lcd_print(0xC0, "All OFF         ");
            } else {
                lcd_print(0x80, "Sensor Error    ");
                lcd_print(0xC0, "Check DHT11     ");
            }

            delayMs(50);
        }
    }
}

