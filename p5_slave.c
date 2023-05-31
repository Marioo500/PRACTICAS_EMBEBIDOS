// Slave as receiver for SPI communitation

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "lwip/igmp.h"

#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "soc/rtc_periph.h"
#include "driver/spi_slave.h"
#include "esp_log.h"
#include "esp_spi_flash.h"
#include "driver/gpio.h"

// Pins in use
#define GPIO_MOSI 23
#define GPIO_MISO 19
#define GPIO_SCLK 18
#define GPIO_CS 5


// Tools

#define LED_GPIO 2
uint8_t ledstate = 1;

void swap(char *x, char *y) 
{
    char t = *x; *x = *y; *y = t;
}

char* reverse(char *buffer, int i, int j)
{
    while (i < j) {
        swap(&buffer[i++], &buffer[j--]);
    }
 
    return buffer;
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

void slaveAsTransmitter(spi_slave_transaction_t t, char *sendbuffer, char *cad){
    snprintf(sendbuffer, sizeof(&sendbuffer), cad);
    t.length = sizeof(sendbuffer) * 8; 
    t.tx_buffer = sendbuffer;
    spi_slave_transmit(HSPI_HOST, &t, portMAX_DELAY);
    printf("Transmitted: %s\n", sendbuffer);
}
//Main application
void app_main(void)
{
    //Configuration for the SPI bus
    spi_bus_config_t buscfg={
        .mosi_io_num=GPIO_MOSI,
        .miso_io_num=GPIO_MISO,
        .sclk_io_num=GPIO_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };

    //Configuration for the SPI slave interface
    spi_slave_interface_config_t slvcfg={
        .mode=0,
        .spics_io_num=GPIO_CS,
        .queue_size=3,
        .flags=0,
    };

    //Initialize SPI slave interface
    spi_slave_initialize(HSPI_HOST, &buscfg, &slvcfg, SPI_DMA_CH_AUTO);
    
    // Tools and tools variables
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GPIO, ledstate);
    char cad[20];
    const char encendido[20] = "ON";
    const char apagado[20] = "OFF";

    // Send / recieve as slave
    char sendbuf[128] = {0};
    char recvbuf[128]="";
    //memset(recvbuf, 0, 33);
    spi_slave_transaction_t t;
    //memset(&t, 0, sizeof(t));

	printf("SLAVE\n");
    while(1) {
        t.length=128*8;
        t.rx_buffer=recvbuf;
        spi_slave_transmit(HSPI_HOST, &t, portMAX_DELAY);
        if(!strcmp(recvbuf, "0x10")) //Enviar timestamp
            {
                myItoa(time(NULL), cad, 10);
                slaveAsTransmitter(t,sendbuf,cad);
        }
        else if (!strcmp(recvbuf, "0x11")) //Enviar estado del led GPIO
            {
                myItoa(gpio_get_level(LED_GPIO), cad, 10);
                ledstate == 1 ? slaveAsTransmitter(t,sendbuf,encendido) : slaveAsTransmitter(t,sendbuf,apagado);
        }
        else if (!strcmp(recvbuf, "0x12")) //Enviar temperatura
            {
                myItoa(25, cad, 10);
                slaveAsTransmitter(t,sendbuf,cad);
        }
        else if (!strcmp(recvbuf, "0x13")) //Invertir estado del led
            {
                ledstate = !ledstate;
                gpio_set_level(LED_GPIO, ledstate);
                slaveAsTransmitter(t,sendbuf,"Invertido!");
        }
    }
}