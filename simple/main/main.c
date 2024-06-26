#include <stdio.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_client.h"
// #include "my_data.h"

#include <esp_event.h>
#include <esp_system.h>
#include <esp_https_server.h>
#include <time.h>
#include "esp_sntp.h"

#include "dht.h"

#define DHT_PIN GPIO_NUM_10 
#define HTTP_GET_QUERY_LENGTH 256

static const char *TAG = "HTTPS";

static const dht_sensor_type_t sensor_type = DHT_TYPE_DHT11;

typedef struct {
    float temperature;
    float humidity;
    int timestamp;
} sensor_data_t;

sensor_data_t *params;

// Define client certificate
extern const uint8_t ClientCert_pem_start[] asm("_binary_ClientCert_pem_start");
extern const uint8_t ClientCert_pem_end[]   asm("_binary_ClientCert_pem_end");

// Define server certificates
extern const unsigned char ServerCert_pem_start[] asm("_binary_ServerCert_pem_start");
extern const unsigned char ServerCert_pem_end[] asm("_binary_ServerCert_pem_end");
extern const unsigned char ServerKey_pem_start[] asm("_binary_ServerKey_pem_start");
extern const unsigned char ServerKey_pem_end[] asm("_binary_ServerKey_pem_end");

// obtain time
void obtain_time(void) {

    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();

    // Wait for time to be set
    time_t now = 0;
    struct tm timeinfo = {0};
    int retry = 0;
    const int retry_count = 10;
    while (timeinfo.tm_year < (2023 - 1900) && ++retry < retry_count) {
        printf("Waiting for system time to be set... (%d/%d)\n", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        time(&now);
        localtime_r(&now, &timeinfo);
    }

    if (retry >= retry_count) {
        printf("Failed to obtain time after multiple attempts\n");
        return;
    }

    printf("Current time: %s", asctime(&timeinfo));
}

// WiFi
static void wifi_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    switch (event_id)
    {
    case WIFI_EVENT_STA_START:
        printf("WiFi connecting ... \n");
        break;
    case WIFI_EVENT_STA_CONNECTED:
        printf("WiFi connected ... \n");
        break;
    case WIFI_EVENT_STA_DISCONNECTED:
        printf("WiFi lost connection ... \n");
        break;
    case IP_EVENT_STA_GOT_IP:
        printf("WiFi got IP ... \n\n");
        break;
    default:
        break;
    }
}

void wifi_connection()
{
    // 1 - Wi-Fi/LwIP Init Phase
    esp_netif_init();                    // TCP/IP initiation 					s1.1
    esp_event_loop_create_default();     // event loop 			                s1.2
    esp_netif_create_default_wifi_sta(); // WiFi station 	                    s1.3
    wifi_init_config_t wifi_initiation = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wifi_initiation); // 					                    s1.4
    // 2 - Wi-Fi Configuration Phase
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL);
    wifi_config_t wifi_configuration = {
        .sta = {
            .ssid = "Gavran",
            .password = "20121967"}};
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_configuration);
    // 3 - Wi-Fi Start Phase
    esp_wifi_start();
    // 4- Wi-Fi Connect Phase
    esp_wifi_connect();
}

// Server
static esp_err_t server_get_handler(httpd_req_t *req)
{
    const char resp[] = "Server GET Response .................";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t server_post_handler(httpd_req_t *req)
{
    char content[100];
    size_t recv_size = MIN(req->content_len, sizeof(content));
    int ret = httpd_req_recv(req, content, recv_size);

    // If no data is send the error will be:
    // W (88470) httpd_uri: httpd_uri: uri handler execution failed
    printf("\nServer POST content: %s\n", content);

    if (ret <= 0)
    { /* 0 return value indicates connection closed */
        /* Check if timeout occurred */
        if (ret == HTTPD_SOCK_ERR_TIMEOUT)
        {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    /* Send a simple response */
    const char resp[] = "Server POST Response .................";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

static const httpd_uri_t server_uri_get = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = server_get_handler
};

static const httpd_uri_t server_uri_post = {
    .uri = "/",
    .method = HTTP_POST,
    .handler = server_post_handler
};

static httpd_handle_t start_webserver(void)
{
    // Start the httpd server
    ESP_LOGI(TAG, "Starting server");
    
    httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
    httpd_handle_t server = NULL;
    
    config.cacert_pem = ServerCert_pem_start;
    config.cacert_len = ServerCert_pem_end - ServerCert_pem_start;
    config.prvtkey_pem = ServerKey_pem_start;
    config.prvtkey_len = ServerKey_pem_end - ServerKey_pem_start;

    esp_err_t ret = httpd_ssl_start(&server, &config);
    if (ESP_OK != ret)
    {
        ESP_LOGI(TAG, "Error starting server!");
        return NULL;
    }

    // Set URI handlers
    ESP_LOGI(TAG, "Registering URI handlers");
    httpd_register_uri_handler(server, &server_uri_get);
    httpd_register_uri_handler(server, &server_uri_post);
    return server;
}

// Client
esp_err_t client_event_get_handler(esp_http_client_event_handle_t evt)
{
    switch (evt->event_id)
    {
    case HTTP_EVENT_ON_DATA:
        printf("Client HTTP_EVENT_ON_DATA: %.*s\n", evt->data_len, (char *)evt->data);
        break;

    default:
        break;
    }
    return ESP_OK;
}

static void client_post_rest_function(float humidity, float temperature)
{
    esp_http_client_config_t config_post = {
        .url = "https://sem2-29aae-default-rtdb.europe-west1.firebasedatabase.app/esp32.json",
        .method = HTTP_METHOD_POST,
        .cert_pem = (const char *)ClientCert_pem_start,
        .event_handler = client_event_get_handler};
        
    esp_http_client_handle_t client = esp_http_client_init(&config_post);

    char post_data[HTTP_GET_QUERY_LENGTH];

    int timestamp = (int)time(NULL);

    sprintf(post_data, 
    "{\"humidity\":%.2f,\"temperature\":%.2f,\"timestamp\":%d}",
        humidity,
        temperature,
        timestamp
    );

    printf("post data: %s\n", post_data);

    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    esp_http_client_set_header(client, "Content-Type", "application/json");

    esp_http_client_perform(client);
    esp_http_client_cleanup(client);
}



void app_main(void)
{
    nvs_flash_init();
    wifi_connection();

    vTaskDelay(2000 / portTICK_PERIOD_MS);
    printf("WIFI was initiated ...........\n\n");

    printf("Start server:\n\n");
    start_webserver();

    vTaskDelay(2000 / portTICK_PERIOD_MS);
    printf("Start client:\n\n");

    esp_err_t dht_ok;
    float humidity = 0;
    float temperature = 0;

    obtain_time();

    while(1) {
        dht_ok = dht_read_float_data(sensor_type, DHT_PIN, &humidity, &temperature) != ESP_OK;
        if(dht_ok == ESP_OK) {
            client_post_rest_function(humidity, temperature);
        } else {
            printf("Error reading DHT22 sensor\n");
        }

        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}