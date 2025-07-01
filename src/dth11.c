#include "stm32f4xx.h"
#include "lcd.h"

#define DHT11_PORT GPIOA
#define DHT11_PIN  4
#define MOTOR_PORT GPIOB
#define MOTOR_PIN  0
#define TEMP_THRESHOLD 20

void DHT11_Out(void)
{
    DHT11_PORT->MODER &= ~(3 << 8);
    DHT11_PORT->MODER |=  (1 << (DHT11_PIN * 2));
}

void DHT11_In(void)
{
    DHT11_PORT->MODER &= ~(3 << (DHT11_PIN * 2));
    DHT11_PORT->PUPDR &= ~(3 << (DHT11_PIN * 2));
    DHT11_PORT->PUPDR |=  (1 << (DHT11_PIN * 2));
}

void DHT11_Write(uint8_t x)
{
    if (x)
        DHT11_PORT->BSRR = (1 << DHT11_PIN);
    else
        DHT11_PORT->BSRR = (1 << (DHT11_PIN + 16));
}

uint8_t DHT11_Read(void) {
    return (DHT11_PORT->IDR >> DHT11_PIN) & 0x01;
}

void delay_us(uint32_t us)
{
    SysTick->LOAD = (SystemCoreClock / 1000000) * us - 1; //15
    SysTick->VAL = 0;
    SysTick->CTRL = 5;
    while (!(SysTick->CTRL & (1 << 16)));
    SysTick->CTRL = 0;
}

void delay_ms(uint32_t ms) {
    for (uint32_t i = 0; i < ms * 1000; i++) __NOP();
}

int dht11_read(uint8_t *t, uint8_t *h) {
    uint8_t d[5] = {0};

    DHT11_Out();
    DHT11_Write(0);
    delay_ms(18);
    DHT11_Write(1);
    delay_us(30);
    DHT11_In();

    delay_us(40);
    if (DHT11_Read()) return -1;
    delay_us(80);
    if (!DHT11_Read()) return -1;
    delay_us(80);

    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 8; j++) {
            uint32_t tmo = 1000;
            while (!DHT11_Read() && tmo--) delay_us(1);
            delay_us(40);
            if (DHT11_Read())
                d[i] |= (1 << (7 - j));
            tmo = 1000;
            while (DHT11_Read() && tmo--) delay_us(1);
        }
    }

    if ((d[0] + d[1] + d[2] + d[3]) != d[4]) return -1;

    *h = d[0];
    *t = d[2];
    return 0;
}

void motor_init(void) {
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    MOTOR_PORT->MODER &= ~(3 << (MOTOR_PIN * 2));
    MOTOR_PORT->MODER |=  (1 << (MOTOR_PIN * 2));
}

void motor_on(void) {
    MOTOR_PORT->BSRR = (1 << MOTOR_PIN);
}

void motor_off(void) {
    MOTOR_PORT->BSRR = (1 << (MOTOR_PIN + 16));
}

int main(void) {
    SystemCoreClockUpdate();
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;

    motor_init();
    lcd_gpio_init();
    lcd_init();

    uint8_t temp, hum;

    while (1) {
        if (dht11_read(&temp, &hum) == 0) {
            lcd(0x80, 0);
            lcd_string("T:");
            single_print(temp);
            lcd_string("C H:");
            single_print(hum);
            lcd_string("%");

            lcd(0xC0, 0);
            if (temp >= TEMP_THRESHOLD) {
                lcd_string("Fan ON - Hot    ");
                motor_on();
            } else {
                lcd_string("Fan OFF - Normal");
                motor_off();
            }
        } else {
            lcd(0x80, 0);
            lcd_string("DHT11 Error     ");
            lcd(0xC0, 0);
            lcd_string("Check Wiring    ");
            motor_off();
        }
        delay_ms(2000);
    }
}

