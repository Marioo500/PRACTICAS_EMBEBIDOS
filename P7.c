#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_system.h>
#include <esp_http_server.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <sys/param.h>
#include <string.h>
#include "time.h"
#include "esp_eth.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


#define EXAMPLE_ESP_WIFI_SSID      "PRACTICA_7"
#define EXAMPLE_ESP_WIFI_PASS      "12345678"
#define EXAMPLE_MAX_STA_CONN       10

#define LED_GPIO 2

bool led_state = 1;

void command_process(char *command, char *cmd_selected)
{
    char string[100];
    srand(time(NULL));
    printf("Comando recibido: %s\n", command);
    int comand = strtol(command, NULL, 16);

    printf("Comando seleccionado: 0x%02X\n", comand);
    
    switch (comand)
    {
        case 0x10:
            itoa(xTaskGetTickCount(),string,10);
            printf("Comando 0x10\n\tTimestamp: %s\n", string);
            break;
        case 0x11:
            if(led_state)
            {
                printf("Estado de LED: %s\n", "ON");
            }
            else
            {
                printf("Estado de LED: %s\n", "OFF");
            }
            break;
        case 0x12:
            itoa(rand()%30, string, 10);
            printf("Comando 0x12\n\tTemperatura: %d\n", 25);
            break;
        case 0x13:
            led_state = !led_state;
            gpio_set_level(LED_GPIO, led_state);
            strcpy(string, "Led Invertido");
            printf("%s\n", string);
            break;
        default:
            break;
    }
}

static esp_err_t index_get_handler(httpd_req_t *req)
{
    char*  buf;
    size_t buf_len;

    /* Get header value string length and allocate memory for length + 1,
     * extra byte for null termination */
    buf_len = httpd_req_get_hdr_value_len(req, "Host") + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        /* Copy null terminated value string into buffer */
        if (httpd_req_get_hdr_value_str(req, "Host", buf, buf_len) == ESP_OK) {
            printf("Found header => Host: %s", buf);
        }
        free(buf);
    }

    /* Read URL query string length and allocate memory for length + 1,
     * extra byte for null termination */
    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            printf("Found URL query => %s", buf);
            char param[5];
            char string[100];

            if (httpd_query_key_value(buf, "command", param, sizeof(param)) == ESP_OK) {
               printf("Found URL query parameter => %s", param);
                command_process(param, string, NULL);

                httpd_resp_set_hdr(req, "Response", string);

                const char* resp_str = (const char*) req->user_ctx;
                httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
            }
        }
        free(buf);
    }

    /* Send response with custom headers and body set as the
     * string passed in user context*/
    const char* resp_str = (const char*) req->user_ctx;
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);

    /* After sending the HTTP response the old HTTP request
     * headers are lost. Check if HTTP request headers can be read now. */
    if (httpd_req_get_hdr_value_len(req, "Host") == 0) {
       printf("Request headers lost");
    }
    return ESP_OK;
}

static esp_err_t index_post_handler(httpd_req_t *req){
    char content[100];
    char cmd_selected[100];

    size_t recv_size = MIN(req->content_len, sizeof(content));

    int ret = httpd_req_recv(req, content, recv_size);

    if(ret <= 0){
        if(ret == HTTPD_SOCK_ERR_TIMEOUT){
            httpd_resp_send_408(req);
        }

        return ESP_FAIL;
    }

    char resp[5] = {content[8],content[9],content[10],content[11], '\0'}; 

    command_process(resp, string, cmd_selected);

    const char* resp_str = (const char*) req->user_ctx;
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static const httpd_uri_t index_post = {
    .uri = "/index",
    .method = HTTP_POST,
    .handler = index_post_handler,
    .user_ctx = "<!DOCTYPE html>\
                    <html lang=\"en\">\
                        <head>\
                            <meta charset=\"UTF-8\">\
                            <meta http-equiv=\"X-UA-Compatible\" content=\"IE=edge\">\
                            <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\
                            <title>HOLA</title>\
                        </head>\
                        <body>\
                            <h1>PRESIONA UN COMANDO </h1>\
                            <form action=\"/index\" method=\"post\">\
                                <button name=\"command\" value=\"0x10\">0x10</button>\
                                <button name=\"command\" value=\"0x11\">0x11</button>\
                                <button name=\"command\" value=\"0x12\">0x12</button>\
                                <button name=\"command\" value=\"0x13\">0x13</button>\
                            </form>\
                        </body>\
                    </html>"
};

static const httpd_uri_t index_get = {
    .uri       = "/index",
    .method    = HTTP_GET,
    .handler   = index_get_handler,
    .user_ctx  = "<!DOCTYPE html>\
                    <html lang=\"en\">\
                        <head>\
                            <meta charset=\"UTF-8\">\
                            <meta http-equiv=\"X-UA-Compatible\" content=\"IE=edge\">\
                            <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\
                            <title>HOLA</title>\
                        </head>\
                        <body>\
                            <h1>PRESIONA UN COMANDO </h1>\
                            <form action=\"/index\" method=\"post\">\
                                <button name=\"command\" value=\"0x10\">0x10</button>\
                                <button name=\"command\" value=\"0x11\">0x11</button>\
                                <button name=\"command\" value=\"0x12\">0x12</button>\
                                <button name=\"command\" value=\"0x13\">0x13</button>\
                            </form>\
                        </body>\
                    </html>"
};

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    // Iniciar el servidor httpd 
    printf("Iniciando el servidor en el puerto: '%d'\n", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Manejadores de URI
        printf("Registrando manejadores de URI\n");
        httpd_register_uri_handler(server, &index_get);
        httpd_register_uri_handler(server, &index_post);
        return server;
    }

    printf("Error starting server!\n");
    return NULL;
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        printf("estacion se unio, AID=%d\n", event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        printf("estacion se desconecto, AID=%d\n", event->aid);
    }
}

esp_err_t wifi_init_softap(void)
{
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
            .password = EXAMPLE_ESP_WIFI_PASS,
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    printf("Inicializacion de softAP terminada. SSID: %s password: %s",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    return ESP_OK;
}

void gpio_init(void)
{
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GPIO, 1);
    led_state = 1;
}

void app_main(void)
{
    httpd_handle_t server = NULL;
    gpio_init();
    ESP_ERROR_CHECK(nvs_flash_init());
    esp_netif_init();

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    printf("init softAP");
    ESP_ERROR_CHECK(wifi_init_softap());

    server =  start_webserver();
}


