#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"

#define CHIP_SELECT 15  //BLANCO
#define SCLK_PIN 14     //VERDE
#define MISO_PIN 12     //AMARILLO
#define MOSI_PIN 13     //AZUL

#define MS5611_CMD_RESET 0x1E
#define MS5611_CMD_ADC_READ 0x00
#define MS5611_CMD_TEMP_CONV 0x58 //TEMPERATURA OSR = 4096
#define MS5611_CMD_PRESS_CONV 0x48 //PRESION OSR = 4096

spi_device_handle_t spi2;
spi_transaction_t trans;
uint16_t coeficientes[8] = {0};
uint8_t cmd;

static void spi_init() {
    esp_err_t ret;
    spi_bus_config_t buscfg={
        .miso_io_num = MISO_PIN,
        .mosi_io_num = MOSI_PIN,
        .sclk_io_num = SCLK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 1,
    };

    ret = spi_bus_initialize(SPI2_HOST, &buscfg, 1);
    ESP_ERROR_CHECK(ret);
    spi_device_interface_config_t devcfg={
        .clock_speed_hz = 1000000,  // 1 MHz
        .mode = 0,                  //SPI mode 0
        .spics_io_num = CHIP_SELECT,     
        .queue_size = 7,
        .pre_cb = NULL,
        .post_cb = NULL,
        .flags = SPI_DEVICE_NO_DUMMY,
        .command_bits = 8,
        .address_bits = 0,
        .dummy_bits = 0,
        .cs_ena_pretrans = 0,
        .cs_ena_posttrans = 0,
        .input_delay_ns = 0,
        };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &spi2));
};

void delayMs(uint16_t ms)
{
    vTaskDelay(ms / portTICK_PERIOD_MS);
}

void read_prom(void)
{
    
    uint8_t buffer_rom[2] = {0};
    memset(coeficientes, 0, sizeof(coeficientes));
    trans.tx_buffer = NULL;
    trans.rx_buffer = NULL;
    trans.user = NULL;
    trans.cmd = MS5611_CMD_RESET; //RESET

    spi_device_transmit(spi2, &trans); //Mando reset
    delayMs(5); //reset tarda 2.8 ms en recargar
    for(uint8_t i = 0 ; i < 8 ; i++)
    {
        trans.cmd = 0xA0 + (i * 2),
        trans.length = 16;
        trans.tx_buffer = NULL;
        trans.rx_buffer = buffer_rom;
        trans.user = NULL;
        spi_device_transmit(spi2, &trans);
        delayMs(1000);
        coeficientes[i]|= buffer_rom[0] << 8 | buffer_rom[1];
        printf("coeficiente %d = %d\n", i, coeficientes[i]);
    }
    delayMs(100);

}
void app_main(void)
{
    float dt2;
    float OFFSET;
    uint32_t aux1, aux2, aux3, aux4;
    spi_init();
    while (1)
    {
        read_prom();
        uint8_t buffer[1] = {0};
        trans.tx_buffer = NULL;
        trans.rx_buffer = NULL;
        trans.user = NULL;
        trans.cmd = MS5611_CMD_TEMP_CONV;
        spi_device_transmit(spi2, &trans); //se manda conversion D1

        delayMs(1000);

        trans.length = 24;
        trans.tx_buffer = NULL;
        trans.rx_buffer = buffer;
        trans.user = NULL;
        trans.cmd = MS5611_CMD_ADC_READ;
        spi_device_transmit(spi2, &trans); //Se manda lectura del adc

        aux1 = (uint32_t)coeficientes[5];
        aux2 = (uint32_t)coeficientes[6];
        aux3 = (uint32_t)coeficientes[2];
        aux4 = (uint32_t)coeficientes[4];
        printf("aux1 = %ld\taux2 = %ld\n", aux1, aux2);
        uint32_t temp_raw = (buffer[0] << 16) | (buffer[1] << 8) | buffer[2];
        uint32_t dt = temp_raw - ( aux1 * 256 );
        dt2 = (float)dt * (aux2 / 8388608.00);
        float TEMP =  2000.00 + dt2;
        OFFSET = (aux3 * 65536) + ((aux4 * dt)/ 128);
        printf("temp_raw = %ld\n", temp_raw);
        printf("dt = %ld\n", dt);
        printf("dt2 = %f\n", dt2);
        printf("TEMP = %f\n", TEMP/100);

        delayMs(2500);
    }

}