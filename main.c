#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_tls.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_wifi.h"
#include "esp_http_client.h"
#include "freertos/event_groups.h"
#include "driver/spi_master.h"


/*Threshold*/
#define limit "Temperatura mayor a 30°C"
#define limit_temp 30
float TEMP_FINAL=0;
char tempascii[20];

/*HTTP buffer*/
#define MAX_HTTP_RECV_BUFFER 1024
#define MAX_HTTP_OUTPUT_BUFFER 2048

/*WIFI configuration*/
#define ESP_WIFI_SSID      "iPhone"
#define ESP_WIFI_PASS      "max12345"
#define ESP_MAXIMUM_RETRY  10

/*Telegram configuration*/
#define TOKEN "6013276786:AAFvixIXLyd3h2XCEC001b7x3XblHpP7Ogg"
char url_string[512] = "https://api.telegram.org/bot";
// Using in the task strcat(url_string,TOKEN)); the main direct from the url will be in url_string
//The chat id that will receive the message
#define chat_ID1 "@se23telegram"
#define chat_ID2 "6125826391"

//Pin connected to a led
#define LED (GPIO_NUM_13)

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;

#define CHIP_SELECT 15  //BLANCO
#define SCLK_PIN    2   //VERDE
#define MISO_PIN    4   //AMARILLO
#define MOSI_PIN    5   //AZUL

#define MS5611_CMD_RESET 0x1E
#define MS5611_CMD_ADC_READ 0x00
#define MS5611_CMD_TEMP_CONV 0x58 //TEMPERATURA OSR = 4096
#define MS5611_CMD_PRESS_CONV 0x48 //PRESION OSR = 4096

spi_device_handle_t spi2;
spi_transaction_t trans;
uint16_t coeficientes[8] = {0};
uint8_t cmd;

/* Root cert for extracted from:
 *
 * https://github.com/witnessmenow/Universal-Arduino-Telegram-Bot/blob/master/src/TelegramCertificate.h

   To embed it in the app binary, the PEM file is named
   in the component.mk COMPONENT_EMBED_TXTFILES variable.
*/
extern const char telegram_certificate_pem_start[] asm("_binary_telegram_certificate_pem_start");
extern const char telegram_certificate_pem_end[]   asm("_binary_telegram_certificate_pem_end");

void reverse(char* str, int len)
{
    int i = 0, j = len - 1, temp;
    while (i < j) {
        temp = str[i];
        str[i] = str[j];
        str[j] = temp;
        i++;
        j--;
    }
}
 
// Converts a given integer x to string str[].
// d is the number of digits required in the output.
// If d is more than the number of digits in x,
// then 0s are added at the beginning.
int intToStr(int x, char str[], int d)
{
    int i = 0;
    while (x) {
        str[i++] = (x % 10) + '0';
        x = x / 10;
    }
 
    // If number of digits required is more, then
    // add 0s at the beginning
    while (i < d)
        str[i++] = '0';
 
    reverse(str, i);
    str[i] = '\0';
    return i;
}
 
// Converts a floating-point/double number to a string.
void ftoa(float n, char* res, int afterpoint)
{
    // Extract integer part
    int ipart = (int)n;
 
    // Extract floating part
    float fpart = n - (float)ipart;
 
    // convert integer part to string
    int i = intToStr(ipart, res, 0);
 
    // check for display option after point
    if (afterpoint != 0) {
        res[i] = '.'; // add dot
 
        // Get the value of fraction part upto given no.
        // of points after dot. The third parameter
        // is needed to handle cases like 233.007
        fpart = fpart * pow(10, afterpoint);
 
        intToStr((int)fpart, res + i + 1, afterpoint);
    }
}

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


static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            printf( "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        printf("connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        //printf( "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void) {
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = ESP_WIFI_SSID,
            .password = ESP_WIFI_PASS,
            /* Setting a password implies station will connect to all security modes including WEP/WPA.
             * However these modes are deprecated and not advisable to be used. Incase your Access point
             * doesn't support WPA2, these mode can be enabled by commenting below line */
	     .threshold.authmode = WIFI_AUTH_WPA2_PSK,

            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    printf( "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        printf( "connected to ap SSID:%s password:%s",
                 ESP_WIFI_SSID, ESP_WIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        printf( "Failed to connect to SSID:%s, password:%s",
                 ESP_WIFI_SSID, ESP_WIFI_PASS);
    } else {
        printf("UNEXPECTED EVENT");
    }

    /* The event will not be processed after unregister */
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
    vEventGroupDelete(s_wifi_event_group);
}


esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    static char *output_buffer;  // Buffer to store response of http request from event handler
    static int output_len;       // Stores number of bytes read
    /*switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            printf( "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            printf( "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            printf( "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            printf( "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            printf( "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // If user_data buffer is configured, copy the response into the buffer
                if (evt->user_data) {
                    memcpy(evt->user_data + output_len, evt->data, evt->data_len);
                } else {
                    if (output_buffer == NULL) {
                        output_buffer = (char *) malloc(esp_http_client_get_content_length(evt->client));
                        output_len = 0;
                        if (output_buffer == NULL) {
                            printf( "Failed to allocate memory for output buffer");
                            return ESP_FAIL;
                        }
                    }
                    memcpy(output_buffer + output_len, evt->data, evt->data_len);
                }
                output_len += evt->data_len;
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            printf( "HTTP_EVENT_ON_FINISH");
            if (output_buffer != NULL) {
                // Response is accumulated in output_buffer. Uncomment the below line to print the accumulated response
                // _BUFFER_HEX(, output_buffer, output_len);
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_DISCONNECTED:
            printf( "HTTP_EVENT_DISCONNECTED");
            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error(evt->data, &mbedtls_err, NULL);
            if (err != 0) {
                if (output_buffer != NULL) {
                    free(output_buffer);
                    output_buffer = NULL;
                }
                output_len = 0;
                printf( "Last esp error code: 0x%x", err);
                printf( "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            break;
    }*/
    return ESP_OK;
}


/*
 *  http_native_request() demonstrates use of low level APIs to connect to a server,
 *  make a http request and read response. Event handler is not used in this case.
 *  Note: This approach should only be used in case use of low level APIs is required.
 *  The easiest way is to use esp_http_perform()
 */
static void http_native_request(void) {
    char output_buffer[MAX_HTTP_OUTPUT_BUFFER] = "";   // Buffer to store response of http request
    int content_length = 0;
    esp_http_client_config_t config = {
        .url = "http://httpbin.org/get",
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // GET Request
    esp_http_client_set_method(client, HTTP_METHOD_GET);
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        printf( "Failed to open HTTP connection: %s", esp_err_to_name(err));
    } else {
        content_length = esp_http_client_fetch_headers(client);
        if (content_length < 0) {
            printf( "HTTP client fetch headers failed");
        } else {
            int data_read = esp_http_client_read_response(client, output_buffer, MAX_HTTP_OUTPUT_BUFFER);
            if (data_read >= 0) {
                printf( "HTTP GET Status = %d, content_length = %lld",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
                //_BUFFER_HEX(, output_buffer, strlen(output_buffer));
                for(int i = 0; i < esp_http_client_get_content_length(client); i++) {
                    putchar(output_buffer[i]);
                }
                putchar('\r');
                putchar('\n');
            } else {
                printf( "Failed to read response");
            }
        }
    }
    esp_http_client_close(client);

    // POST Request
    const char *post_data = "{\"field1\":\"value1\"}";
    esp_http_client_set_url(client, "http://httpbin.org/post");
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    err = esp_http_client_open(client, strlen(post_data));
    if (err != ESP_OK) {
        printf( "Failed to open HTTP connection: %s", esp_err_to_name(err));
    } else {
        int wlen = esp_http_client_write(client, post_data, strlen(post_data));
        if (wlen < 0) {
            printf( "Write failed");
        }
        int data_read = esp_http_client_read_response(client, output_buffer, MAX_HTTP_OUTPUT_BUFFER);
        if (data_read >= 0) {
            printf( "HTTP GET Status = %d, content_length = %lld",
            esp_http_client_get_status_code(client),
            esp_http_client_get_content_length(client));
            //_BUFFER_HEX(, output_buffer, strlen(output_buffer));
            for(int i = 0; i < esp_http_client_get_content_length(client); i++) {
                putchar(output_buffer[i]);
            }
            putchar('\r');
            putchar('\n');
        } else {
            printf( "Failed to read response");
        }
    }
    esp_http_client_cleanup(client);
}


static void https_telegram_getMe_perform(void) {
	char buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};   // Buffer to store response of http request
	char url[512] = "";
    esp_http_client_config_t config = {
        .url = "https://api.telegram.org",
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .event_handler = _http_event_handler,
        .cert_pem = telegram_certificate_pem_start,
        .user_data = buffer,        // Pass address of local buffer to get response
    };
    /* Creating the string of the url*/
    //Copy the url+TOKEN
    strcat(url,url_string);
    //Adding the method
    strcat(url,"/getMe");
    //printf( "url es: %s",url);
    //printf( "Iniciare");
    esp_http_client_handle_t client = esp_http_client_init(&config);
    //You set the real url for the request
    esp_http_client_set_url(client, url);
    //printf( "Selecting the http method");
    esp_http_client_set_method(client, HTTP_METHOD_GET);
    //printf( "Perform");
    esp_err_t err = esp_http_client_perform(client);

    //printf( "Revisare");
    if (err == ESP_OK) {
        printf( "HTTPS Status = %d, content_length = %lld",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
        printf( "Desde Perform el output es: %s",buffer);
    } else {
        printf( "Error perform http request %s", esp_err_to_name(err));
    }

    printf( "Cerrar Cliente");
    esp_http_client_close(client);
    printf( "Limpiare");
    esp_http_client_cleanup(client);
}

static void https_telegram_getMe_native_get(void) {

	/*	Partiendo de http_native_request
	 *  http_native_request() demonstrates use of low level APIs to connect to a server,
	 *  make a http request and read response. Event handler is not used in this case.
	 *  Note: This approach should only be used in case use of low level APIs is required.
	 *  The easiest way is to use esp_http_perform()
	 */

    char output_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};   // Buffer to store response of http request
    int content_length = 0;
    char url[512] = "";
    esp_http_client_config_t config = {
        .url = "https://api.telegram.org",
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .event_handler = _http_event_handler,
        .cert_pem = telegram_certificate_pem_start,
    };
    printf( "Iniciare 2");
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // GET Request
    printf( "Method");
    esp_http_client_set_method(client, HTTP_METHOD_GET);
    printf( "Open");
    /* Creating the string of the url*/
    //Copy the url+TOKEN
    strcat(url,url_string);
    //Adding the method
    strcat(url,"/getMe");
    //printf( "url string es: %s",url);
    //You set the real url for the request
    esp_http_client_set_url(client, url);
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        printf( "Failed to open HTTP connection: %s", esp_err_to_name(err));
    } else {
        printf( "Fetch");
        content_length = esp_http_client_fetch_headers(client);
        if (content_length < 0) {
            printf( "HTTP client fetch headers failed");
        } else {
            printf( "Response");
            int data_read = esp_http_client_read_response(client, output_buffer, MAX_HTTP_OUTPUT_BUFFER);
            if (data_read >= 0) {
                printf( "HTTP GET Status = %d, content_length = %lld",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
                //_BUFFER_CHAR(2, output_buffer, strlen(output_buffer));
            //    _BUFFER_HEX(2, output_buffer, strlen(output_buffer));
                for(int i = 0; i < esp_http_client_get_content_length(client); i++) {
                    putchar(output_buffer[i]);
                }
                putchar('\r');
                putchar('\n');
            } else {
                printf( "Failed to read response");
            }
        }
    }
    printf( "Cerrar Cliente");
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    printf( "Desde perform esp_get_free_heap_size: %ld", esp_get_free_heap_size ());
}

static void https_telegram_sendMessage_native_get(void) {


	/* Format for sending messages
	https://api.telegram.org/bot[BOT_TOKEN]/sendMessage?chat_id=[CHANNEL_NAME]&text=[MESSAGE_TEXT]

	For public groups you can use
	https://api.telegram.org/bot[BOT_TOKEN]/sendMessage?chat_id=@GroupName&text=hello%20world
	For private groups you have to use the chat id (which also works with public groups)
	https://api.telegram.org/bot[BOT_TOKEN]/sendMessage?chat_id=-1234567890123&text=hello%20world

	You can add your chat_id or group name, your api key and use your browser to send those messages
	The %20 is the hexa for the space
	*/

    char output_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};   // Buffer to store response of http request
    int content_length = 0;
    char url[512] = "";
    esp_http_client_config_t config = {
        .url = "https://api.telegram.org",
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .event_handler = _http_event_handler,
        .cert_pem = telegram_certificate_pem_start,
    };

    printf( "Iniciare");
    esp_http_client_handle_t client = esp_http_client_init(&config);
    printf( "Enviare un mensaje a un chat");
    printf( "Open");
    /* Creating the string of the url*/
    //Copy the url+TOKEN
    strcat(url,url_string);
    //Then you concatenate the method with the information
    strcat(url,"/sendMessage?chat_id=");
    strcat(url,chat_ID1);
    /* Now you add the text*/
    strcat(url,"&text=");
    //Between every word you have to put %20 for the space (maybe there is another way for this)
    strcat(url,"Text%20to%20send%20to%20the%20chat");
    printf( "url string es: %s",url);
    //You set the real url for the request
    esp_http_client_set_url(client, url);

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        printf( "Failed to open HTTP connection: %s", esp_err_to_name(err));
    } else {
        printf( "Fetch 2");
        content_length = esp_http_client_fetch_headers(client);
        if (content_length < 0) {
            printf( "HTTP client fetch headers failed");
        } else {
            printf( "Response 2");
            int data_read = esp_http_client_read_response(client, output_buffer, MAX_HTTP_OUTPUT_BUFFER);
            if (data_read >= 0) {
                printf( "HTTP GET Status = %d, content_length = %lld",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
                //_BUFFER_CHAR(2, output_buffer, 188);
                //_BUFFER_HEX(2, output_buffer, strlen(output_buffer));
                for(int i = 0; i < esp_http_client_get_content_length(client); i++) {
                    putchar(output_buffer[i]);
                }
                putchar('\r');
                putchar('\n');
            } else {
                printf( "Failed to read response");
            }
        }
    }
    printf( "Limpiare");
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    printf( "esp_get_free_heap_size: %ld", esp_get_free_heap_size ());
}

static void https_telegram_sendMessage_perform_post(void) {


	/* Format for sending messages
	https://api.telegram.org/bot[BOT_TOKEN]/sendMessage?chat_id=[CHANNEL_NAME]&text=[MESSAGE_TEXT]

	For public groups you can use
	https://api.telegram.org/bot[BOT_TOKEN]/sendMessage?chat_id=@GroupName&text=hello%20world
	For private groups you have to use the chat id (which also works with public groups)
	https://api.telegram.org/bot[BOT_TOKEN]/sendMessage?chat_id=-1234567890123&text=hello%20world

	You can add your chat_id or group name, your api key and use your browser to send those messages
	The %20 is the hexa for the space

	The format for the json is: {"chat_id":852596694,"text":"Message using post"}
	*/

	char url[512] = "";
    char output_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};   // Buffer to store response of http request
    esp_http_client_config_t config = {
        .url = "https://api.telegram.org",
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .event_handler = _http_event_handler,
        .cert_pem = telegram_certificate_pem_start,
		.user_data = output_buffer,
    };
    //POST
    printf( "Iniciare");
    esp_http_client_handle_t client = esp_http_client_init(&config);

    /* Creating the string of the url*/
    //Copy the url+TOKEN
    strcat(url,url_string);
    //Passing the method
    strcat(url,"/sendMessage");
    //printf( "url string es: %s",url);
    //You set the real url for the request
    esp_http_client_set_url(client, url);


	printf( "Enviare POST");
	/*Here you add the text and the chat id
	 * The format for the json for the telegram request is: {"chat_id":123456789,"text":"Here goes the message"}
	  */
	// The example had this, but to add the chat id easierly I decided not to use a pointer
	//const char *post_data = "{\"chat_id\":852596694,\"text\":\"Envio de post\"}";
	char post_data[512] = "";
    ftoa(TEMP_FINAL, tempascii, 2);
	sprintf(post_data,"{\"chat_id\":%s,\"text\":\"%s - Sensor: %s°C\"}",chat_ID2,limit,tempascii);
    //printf( "El json es es: %s",post_data);
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        printf( "HTTP POST Status = %d, content_length = %lld",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
        printf( "Desde Perform el output es: %s",output_buffer);

    } else {
        printf( "HTTP POST request failed: %s", esp_err_to_name(err));
    }

    printf( "Limpiare");
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    printf( "esp_get_free_heap_size: %ld", esp_get_free_heap_size ());
}


static void http_test_task(void *pvParameters) {
    /* Creating the string of the url*/
    // You concatenate the host with the Token so you only have to write the method
    float dt2;
    float OFFSET;
    uint32_t aux1, aux2, aux3, aux4;
	strcat(url_string,TOKEN);
    printf( "Wait 2 second before start");
    vTaskDelay(2000 / portTICK_PERIOD_MS);

    printf( "https_telegram_getMe_perform");
    https_telegram_getMe_perform();
    /* The functions https_telegram_getMe_native_get and https_telegram_sendMessage_native_get usually reboot the esp32 at when you use it after another and
     *  the second one finish, but I don't know why. Either way, it still send the message and obtain the getMe response, but the perform way is better
     *  for both options, especially for sending message with Json.*/
    //printf( "https_telegram_getMe_native_get");
    //https_telegram_getMe_native_get();
    //printf( "https_telegram_sendMessage_native_get");
    //https_telegram_sendMessage_native_get();
    read_prom();
    while(1){
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
        TEMP_FINAL =  2000.00 + dt2;
        OFFSET = (aux3 * 65536) + ((aux4 * dt)/ 128);
        printf("temp_raw = %ld\n", temp_raw);
        printf("dt = %ld\n", dt);
        printf("dt2 = %f\n", dt2);
        TEMP_FINAL = TEMP_FINAL/100;
        printf("TEMP = %f\n", TEMP_FINAL);
        if(TEMP_FINAL >= limit_temp && TEMP_FINAL < 33)
        {
            https_telegram_sendMessage_perform_post();
        }
        delayMs(1000);
    }
    vTaskDelete(NULL);
}




void app_main(void)
{
    //Initialize NVS
    float dt2;
    float OFFSET;
    uint32_t aux1, aux2, aux3, aux4;
    spi_init();
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    printf( "ESP_WIFI_MODE_STA");
    wifi_init_sta();

    xTaskCreatePinnedToCore(&http_test_task, "http_test_task", 8192*4, NULL, 5, NULL,1);
    
}