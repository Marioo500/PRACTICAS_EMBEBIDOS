#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "esp_log.h"

// UART 0
#define PC_UART_PORT    (0)
#define PC_UART_RX_PIN  (3)
#define PC_UART_TX_PIN  (1)
// UART 1
#define UART1_PORT      (1)
#define UART1_RX_PIN    (18)
#define UART1_TX_PIN    (19)
// UART 2
#define UART2_PORT      (2)
#define UART2_RX_PIN    (16)
#define UART2_TX_PIN    (17)

#define UART_BAUD_RATE          (115200) 
#define TASK_STACK_SIZE         (1048)
#define READ_BUF_SIZE           (1024)

#define LED_GPIO 22

//Funciones auxiliares
const char encendido[] = "ON";
const char apagado[] = "OFF";

uint8_t ledstate = 1;
static void uart_init(uart_port_t uart_num, uint8_t rxPin, uint8_t txPin)
{
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };

    ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));
    ESP_ERROR_CHECK(uart_driver_install(uart_num, READ_BUF_SIZE, READ_BUF_SIZE, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_set_pin(uart_num, txPin, rxPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

void delayMs(uint16_t ms)
{
    vTaskDelay(ms / portTICK_PERIOD_MS);
}
   
uint16_t myAtoi(char *str) // TERMINADA
{
    uint16_t number = 0;
    uint16_t i = 0;
    while(str[i] >= '0' && str[i] <= '9')
    {
        number = number * 10 + str[i] - '0';
        i++;
    }
    return number;
}

void myItoa(uint16_t number, char* str, uint8_t base) //TERMINADA
{
    uint16_t i = 0;  
    while(number)
    {
        uint16_t r = number % base;
        if (r >= 0 && r <=9) {
            str[i] = '0' + r;
        }
        else {
            str[i] = 'A' + (r - 10);
        }
        i++;
        number = number / base;
    }
    str[i] = '\0';
    reverse(str, 0, i - 1);
}

bool uartKbhit(uart_port_t uart_num)
{
    uint8_t length;
    uart_get_buffered_data_len(uart_num, (size_t*)&length);
    return (length > 0);
}

struct crc{
    uint32_t cabezera=0x05;
    uint32_t comando;
    uint32_t longitud;
    uint32_t datos;
    uint32_t fin=0x5B
} cifrado;

crc(uint32_t cabezera, uint32_t comando, uint32_t longitud , uint32_t datos, uint32_t fin){
    
}

void app_main(void)
{
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GPIO, ledstate);
    char cad[20];
    uart_init(UART2_PORT, UART2_RX_PIN, UART2_TX_PIN);
    uart_init(PC_UART_PORT, PC_UART_RX_PIN, PC_UART_TX_PIN);

    
    while (1)
    {
        if(uartKbhit(UART2_PORT))
        {
            int len = uart_read_bytes(UART2_PORT, cad, (READ_BUF_SIZE - 1), 0);
            if (len)
            {
                if(!strcmp(cad, "0x10")) //Eviar timestamp
                {
                    myItoa(time(NULL), cad, 10);
                    uart_write_bytes(UART2_PORT, (const char *) cad, sizeof(cad));
                }
                else if (!strcmp(cad, "0x11")) //Enviar estado del led GPIO
                {
                    myItoa(gpio_get_level(LED_GPIO), cad, 10);
                    ledstate == 1 ? uart_write_bytes(UART2_PORT, encendido, sizeof(encendido)) : uart_write_bytes(UART2_PORT, apagado, sizeof(apagado));
                }
                else if (!strcmp(cad, "0x12")) //Enviar temperatura
                {
                    myItoa(25, cad, 10);
                    uart_write_bytes(UART2_PORT, (const char *) cad, 2);
                }
                else if (!strcmp(cad, "0x13")) //Invertir estado del led
                {
                    ledstate = !ledstate;
                    gpio_set_level(LED_GPIO, ledstate);
                }
            }
        }
        delayMs(10);
    }
}
