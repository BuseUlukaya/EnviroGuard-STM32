/* EnviroGuard STM32F407 - standalone main.c (OLED address diagnostic)

 * DHT22 PA1, USART2 TX PA2 115200, OLED SSD1306 I2C1 PB6/PB7

 * STM32CubeIDE HAL project (peripherals initialized in main.c).

 */

#include "main.h"

#include <stdint.h>

#include <stdio.h>

#include <string.h>

#define DHT_PORT GPIOA

#define DHT_PIN GPIO_PIN_1

#define LED_GREEN_PIN GPIO_PIN_12

#define LED_RED_PIN GPIO_PIN_13

#define LED_PORT GPIOD

static uint16_t oled_addr = (0x3C << 1);

static uint8_t oled_present = 0;

#define OLED_W 128

#define OLED_H 64

TIM_HandleTypeDef htim2;

UART_HandleTypeDef huart2;

I2C_HandleTypeDef hi2c1;

static uint8_t oled_fb[OLED_W * OLED_H / 8];

void SystemClock_Config(void);

static void MX_GPIO_Init(void);

static void MX_TIM2_Init(void);

static void MX_USART2_UART_Init(void);

static void MX_I2C1_Init(void);

void Error_Handler(void);

static void delay_us(uint32_t us) {

    __HAL_TIM_SET_COUNTER(&htim2, 0);

    while (__HAL_TIM_GET_COUNTER(&htim2) < us) { }

}

static void uart_send(const char *s) {

    HAL_UART_Transmit(&huart2, (uint8_t *)s, (uint16_t)strlen(s), 200);

}

static void dht_output(void) {

    GPIO_InitTypeDef p = {0};

    p.Pin = DHT_PIN;

    p.Mode = GPIO_MODE_OUTPUT_PP;

    p.Pull = GPIO_NOPULL;

    p.Speed = GPIO_SPEED_FREQ_LOW;

    HAL_GPIO_Init(DHT_PORT, &p);

}

static void dht_input(void) {

    GPIO_InitTypeDef p = {0};

    p.Pin = DHT_PIN;

    p.Mode = GPIO_MODE_INPUT;

    p.Pull = GPIO_PULLUP;

    HAL_GPIO_Init(DHT_PORT, &p);

}

static uint8_t wait_level(GPIO_PinState state, uint32_t timeout) {

    __HAL_TIM_SET_COUNTER(&htim2, 0);

    while (HAL_GPIO_ReadPin(DHT_PORT, DHT_PIN) != state) {

        if (__HAL_TIM_GET_COUNTER(&htim2) >= timeout) return 0;

    }

    return 1;

}

/* 0=success, 1=timeout, 2=checksum */

static uint8_t dht_read(int *t10, int *h10) {

    uint8_t b[5] = {0};

    dht_output();

    HAL_GPIO_WritePin(DHT_PORT, DHT_PIN, GPIO_PIN_RESET);

    HAL_Delay(2);

    HAL_GPIO_WritePin(DHT_PORT, DHT_PIN, GPIO_PIN_SET);

    delay_us(30);

    dht_input();

    if (!wait_level(GPIO_PIN_RESET, 120)) return 1;

    if (!wait_level(GPIO_PIN_SET, 120)) return 1;

    if (!wait_level(GPIO_PIN_RESET, 120)) return 1;

    for (int i = 0; i < 40; ++i) {

        if (!wait_level(GPIO_PIN_SET, 120)) return 1;

        delay_us(40);

        b[i / 8] <<= 1;

        if (HAL_GPIO_ReadPin(DHT_PORT, DHT_PIN) == GPIO_PIN_SET)

            b[i / 8] |= 1;

        if (!wait_level(GPIO_PIN_RESET, 120)) return 1;

    }

    if ((uint8_t)(b[0] + b[1] + b[2] + b[3]) != b[4]) return 2;

    *h10 = ((int)b[0] << 8) | b[1];

    *t10 = (((int)b[2] << 8) | b[3]) & 0x7FFF;

    if (b[2] & 0x80) *t10 = -*t10;

    return 0;

}

static HAL_StatusTypeDef oled_cmd(uint8_t cmd) {

    uint8_t data[2] = {0x00, cmd};

    return HAL_I2C_Master_Transmit(&hi2c1, oled_addr, data, 2, 100);

}

static void oled_init(void) {

    const uint8_t init[] = {

        0xAE,0xD5,0x80,0xA8,0x3F,0xD3,0x00,0x40,0x8D,0x14,

        0x20,0x02,0xA1,0xC8,0xDA,0x12,0x81,0x7F,0xD9,0xF1,

        0xDB,0x40,0xA4,0xA6,0xAF

    };

    HAL_Delay(100);

    for (unsigned i = 0; i < sizeof(init); ++i) oled_cmd(init[i]);

}

static void oled_pixel(int x, int y) {

    if (x >= 0 && x < OLED_W && y >= 0 && y < OLED_H)

        oled_fb[x + (y / 8) * OLED_W] |= (uint8_t)(1U << (y % 8));

}

static void oled_clear(void) { memset(oled_fb, 0, sizeof(oled_fb)); }

static void oled_update(void) {

    uint8_t chunk[17];

    chunk[0] = 0x40;

    for (int page = 0; page < 8; ++page) {

        oled_cmd((uint8_t)(0xB0 + page));

        oled_cmd(0x00);

        oled_cmd(0x10);

        for (int block = 0; block < 8; ++block) {

            memcpy(chunk + 1, oled_fb + page * 128 + block * 16, 16);

            HAL_I2C_Master_Transmit(&hi2c1, oled_addr, chunk, 17, 100);

        }

    }

}

/* Minimal 3x5 font, bit 0 = top pixel. */

static void glyph(char c, uint8_t g[3]) {

    g[0] = g[1] = g[2] = 0;

    if (c >= '0' && c <= '9') {

        static const uint8_t digits[10][3] = {

            {31,17,31},{0,31,0},{29,21,23},{21,21,31},{7,4,31},

            {23,21,29},{31,21,29},{1,1,31},{31,21,31},{23,21,31}

        };

        memcpy(g, digits[c-'0'], 3);

        return;

    }

    switch(c) {

        case 'A': g[0]=30;g[1]=5;g[2]=30;break;

        case 'C': g[0]=31;g[1]=17;g[2]=17;break;

        case 'D': g[0]=31;g[1]=17;g[2]=14;break;

        case 'E': g[0]=31;g[1]=21;g[2]=17;break;

        case 'G': g[0]=31;g[1]=17;g[2]=29;break;

        case 'H': g[0]=31;g[1]=4;g[2]=31;break;

        case 'I': g[0]=17;g[1]=31;g[2]=17;break;

        case 'M': g[0]=31;g[1]=6;g[2]=31;break;

        case 'N': g[0]=31;g[1]=2;g[2]=31;break;

        case 'O': g[0]=31;g[1]=17;g[2]=31;break;

        case 'P': g[0]=31;g[1]=5;g[2]=7;break;

        case 'R': g[0]=31;g[1]=5;g[2]=26;break;

        case 'S': g[0]=23;g[1]=21;g[2]=29;break;

        case 'T': g[0]=1;g[1]=31;g[2]=1;break;

        case 'U': g[0]=31;g[1]=16;g[2]=31;break;

        case 'V': g[0]=15;g[1]=16;g[2]=15;break;

        case '.': g[1]=16;break;

        case ':': g[1]=10;break;

        case '-': g[0]=4;g[1]=4;g[2]=4;break;

        case '%': g[0]=25;g[1]=4;g[2]=19;break;

        default: break;

    }

}

static void oled_text(int x, int y, const char *s, int scale) {

    while (*s) {

        uint8_t g[3];

        glyph(*s++, g);

        for (int col = 0; col < 3; ++col)

            for (int row = 0; row < 5; ++row)

                if (g[col] & (1U << row))

                    for (int dx = 0; dx < scale; ++dx)

                        for (int dy = 0; dy < scale; ++dy)

                            oled_pixel(x + col * scale + dx, y + row * scale + dy);

        x += 4 * scale;

    }

}

static void oled_show(int t10, int h10) {

    char s[32];

    int at = t10 < 0 ? -t10 : t10;

    oled_clear();

    oled_text(3, 2, "ENVIROGUARD", 2);

    oled_text(4, 21, "TEMP:", 2);

    snprintf(s, sizeof(s), "%s%d.%d C", t10 < 0 ? "-" : "", at / 10, at % 10);

    oled_text(4, 32, s, 2);

    oled_text(4, 48, "HUM:", 2);

    snprintf(s, sizeof(s), "%d.%d%%", h10 / 10, h10 % 10);

    oled_text(53, 48, s, 2);

    oled_update();

}

static void oled_error(void) {

    oled_clear();

    oled_text(3, 2, "ENVIROGUARD", 2);

    oled_text(4, 28, "DHT ERROR", 2);

    oled_update();

}

/* Temperature threshold: >=25C red, below 25C green. */

static void led_update(int t10) {

    uint8_t hot = (t10 >= 250) ? 1U : 0U;

    HAL_GPIO_WritePin(LED_PORT, LED_RED_PIN, hot ? GPIO_PIN_SET : GPIO_PIN_RESET);

    HAL_GPIO_WritePin(LED_PORT, LED_GREEN_PIN, hot ? GPIO_PIN_RESET : GPIO_PIN_SET);

}

int main(void) {

    HAL_Init();

    SystemClock_Config();

    MX_GPIO_Init();

    MX_TIM2_Init();

    MX_USART2_UART_Init();

    MX_I2C1_Init();

    if (HAL_TIM_Base_Start(&htim2) != HAL_OK) Error_Handler();

    HAL_GPIO_WritePin(LED_PORT, LED_GREEN_PIN, GPIO_PIN_SET);

    HAL_Delay(2000);

    uart_send("EnviroGuard started\r\n");

    if (HAL_I2C_IsDeviceReady(&hi2c1, (0x3C << 1), 3, 100) == HAL_OK) {

        oled_addr = (0x3C << 1);

        oled_present = 1;

        uart_send("OLED bulundu: 0x3C\r\n");

    } else if (HAL_I2C_IsDeviceReady(&hi2c1, (0x3D << 1), 3, 100) == HAL_OK) {

        oled_addr = (0x3D << 1);

        oled_present = 1;

        uart_send("OLED bulundu: 0x3D\r\n");

    } else {

        uart_send("OLED bulunamadi: I2C kablo/besleme/pull-up kontrol edin\r\n");

    }

    if (oled_present) {

        oled_init();

        oled_clear();

        oled_text(3, 8, "ENVIROGUARD", 2);

        oled_text(3, 30, "START", 2);

        oled_update();

    }

    uint32_t oled_report_counter = 0;

    while (1) {

        if (++oled_report_counter >= 5) {

            oled_report_counter = 0;

            if (!oled_present) uart_send("OLED bulunamadi (0x3C/0x3D)\r\n");

        }

        int t10 = 0, h10 = 0;

        uint8_t result = dht_read(&t10, &h10);

        if (result == 0) {

            char line[100];

            int at = t10 < 0 ? -t10 : t10;

            snprintf(line, sizeof(line),

                     "Temperature: %s%d.%d C | Humidity: %d.%d %%\r\n",

                     t10 < 0 ? "-" : "", at / 10, at % 10, h10 / 10, h10 % 10);

            uart_send(line);

            led_update(t10);

            if (oled_present) oled_show(t10, h10);

        } else {

            uart_send(result == 1 ? "DHT22 no response / timeout\r\n" : "DHT22 checksum error\r\n");

            if (oled_present) oled_error();

        }

        HAL_Delay(2000);

    }

}

void SystemClock_Config(void) {

    RCC_OscInitTypeDef osc = {0};

    RCC_ClkInitTypeDef clk = {0};

    __HAL_RCC_PWR_CLK_ENABLE();

    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;

    osc.HSIState = RCC_HSI_ON;

    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;

    osc.PLL.PLLState = RCC_PLL_OFF;

    if (HAL_RCC_OscConfig(&osc) != HAL_OK) Error_Handler();

    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |

                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;

    clk.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;

    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;

    clk.APB1CLKDivider = RCC_HCLK_DIV1;

    clk.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_0) != HAL_OK) Error_Handler();

}

static void MX_GPIO_Init(void) {

    GPIO_InitTypeDef p = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    __HAL_RCC_GPIOB_CLK_ENABLE();

    __HAL_RCC_GPIOD_CLK_ENABLE();

    p.Pin = DHT_PIN;

    p.Mode = GPIO_MODE_INPUT;

    p.Pull = GPIO_PULLUP;

    HAL_GPIO_Init(DHT_PORT, &p);

    HAL_GPIO_WritePin(LED_PORT, LED_GREEN_PIN | LED_RED_PIN, GPIO_PIN_RESET);

    p.Pin = LED_GREEN_PIN | LED_RED_PIN;

    p.Mode = GPIO_MODE_OUTPUT_PP;

    p.Pull = GPIO_NOPULL;

    p.Speed = GPIO_SPEED_FREQ_LOW;

    HAL_GPIO_Init(LED_PORT, &p);

}

static void MX_TIM2_Init(void) {

    __HAL_RCC_TIM2_CLK_ENABLE();

    htim2.Instance = TIM2;

    htim2.Init.Prescaler = 15;  /* 16 MHz / 16 = 1 MHz */

    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;

    htim2.Init.Period = 0xFFFFFFFFU;

    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;

    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_Base_Init(&htim2) != HAL_OK) Error_Handler();

}

static void MX_USART2_UART_Init(void) {

    GPIO_InitTypeDef p = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    __HAL_RCC_USART2_CLK_ENABLE();

    p.Pin = GPIO_PIN_2 | GPIO_PIN_3;

    p.Mode = GPIO_MODE_AF_PP;

    p.Pull = GPIO_PULLUP;

    p.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

    p.Alternate = GPIO_AF7_USART2;

    HAL_GPIO_Init(GPIOA, &p);

    huart2.Instance = USART2;

    huart2.Init.BaudRate = 115200;

    huart2.Init.WordLength = UART_WORDLENGTH_8B;

    huart2.Init.StopBits = UART_STOPBITS_1;

    huart2.Init.Parity = UART_PARITY_NONE;

    huart2.Init.Mode = UART_MODE_TX_RX;

    huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;

    huart2.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&huart2) != HAL_OK) Error_Handler();

}

static void MX_I2C1_Init(void) {

    GPIO_InitTypeDef p = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    __HAL_RCC_I2C1_CLK_ENABLE();

    p.Pin = GPIO_PIN_6 | GPIO_PIN_7;

    p.Mode = GPIO_MODE_AF_OD;

    p.Pull = GPIO_PULLUP;

    p.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

    p.Alternate = GPIO_AF4_I2C1;

    HAL_GPIO_Init(GPIOB, &p);

    hi2c1.Instance = I2C1;

    hi2c1.Init.ClockSpeed = 100000;

    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;

    hi2c1.Init.OwnAddress1 = 0;

    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;

    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;

    hi2c1.Init.OwnAddress2 = 0;

    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;

    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(&hi2c1) != HAL_OK) Error_Handler();

}

void Error_Handler(void) {

    __disable_irq();

    while (1) { }

}
