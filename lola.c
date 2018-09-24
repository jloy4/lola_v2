#include <stdio.h>
#include <espressif/esp_wifi.h>
#include <espressif/esp_sta.h>
#include <esp/uart.h>
#include <esp8266.h>
#include <FreeRTOS.h>
#include <FreeRTOSConfig.h>
#include <task.h>
#include <string.h>

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include <homekit/homekit.h>
#include <homekit/characteristics.h>
#include "wifi.h"

#include <tsl2561/tsl2561.h>
#include <wificfg/wificfg.h>

/* ---------- WIFI HANDLER ------------------ */
/* ------------------------------------------ */
static void wifi_init() {
  struct sdk_station_config wifi_config = {
    .ssid = WIFI_SSID,
    .password = WIFI_PASSWORD,
  };

  sdk_wifi_set_opmode(STATION_MODE);
  sdk_wifi_station_set_config(&wifi_config);
  sdk_wifi_station_connect();
}

void wifi_config() {
  uint32_t port = 80;
  const wificfg_dispatch *dispatch;
  wificfg_init(port, dispatch);
  //wificfg_wait_until_sta_connected();
}


/* ---------- LIGHT SENSOR HANDLER ---------- */
/* ------------------------------------------ */
#define WEB_SERVER "lola-light.firebaseio.com"
#define WEB_URL "https://lola-light.firebaseio.com/users.json"

char request[300];
char details[80];
char headers[200];
#define PUB_MSG_LEN 48

void http_post_task(void *pvParameters) {
  int successes = 0;
  int failures = 0;
  printf("HTTP post task starting...\r\n");

  while(1) {
//    sdk_wifi_set_sleep_type(NONE_SLEEP);
    const struct addrinfo hints = {
      .ai_family = AF_INET,
      .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;

    printf("Running DNS lookup for %s...\r\n", WEB_SERVER);
    int err = getaddrinfo(WEB_SERVER, "443", &hints, &res);

    if(err != 0 || res == NULL) {
      printf("DNS lookup failed err=%d res=%p\r\n", err, res);
      if(res)
        freeaddrinfo(res);
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      failures++;
      continue;
    }
    /* Note: inet_ntoa is non-reentrant, look at ipaddr_ntoa_r for "real" code */
    struct in_addr *addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
    printf("DNS lookup succeeded. IP=%s\r\n", inet_ntoa(*addr));

    int s = socket(res->ai_family, res->ai_socktype, 0);
    if(s < 0) {
      printf("... Failed to allocate socket.\r\n");
      freeaddrinfo(res);
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      failures++;
      continue;
    }

    printf("... allocated socket\r\n");

    if(connect(s, res->ai_addr, res->ai_addrlen) != 0) {
      close(s);
      freeaddrinfo(res);
      printf("... socket connect failed.\r\n");
      vTaskDelay(4000 / portTICK_PERIOD_MS);
      failures++;
      continue;
    }

    printf("... connected\r\n");
    freeaddrinfo(res);
    request[0] = "\0";
    details[0] = "\0";
    snprintf(details, 80, "{\"sensors\": {\"lux_front\": %.1f, \"lux_back\": %.1f, \"brightness\": %d}}", 20.1, 100.2, 26);

    snprintf(request, 300, "PUT /data.json HTTP/1.1\r\nHost: %s\r\nUser-Agent: esp-open-rtos/0.1 esp8266\r\nAccept: */*\r\nContent-Length: %d\r\nContent-Type: application/x-www-form-urlencoded\r\n\r\n%s", WEB_SERVER, strlen(details), details);
    printf(request);
    printf("\nLength of request: %d\n", strlen(request));
    if (write(s, request, strlen(request)) < 0) {
      printf("... socket send failed\r\n");
      close(s);
      vTaskDelay(4000 / portTICK_PERIOD_MS);
      failures++;
      continue;
    }
    printf("... socket send success\r\n");

    static char recv_buf[200];
    int r;
    do {
      printf("receiving...");
      bzero(recv_buf, 200);
      r = read(s, recv_buf, 199);
      if(r > 0) {
        printf("%s", recv_buf);
      }
    } while(r > 0);

    printf("... done reading from socket. Last read return=%d errno=%d\r\n", r, errno);
    if(r != 0)
      failures++;
    else
      successes++;
    close(s);
    printf("successes = %d failures = %d\r\n", successes, failures);
    //sdk_wifi_set_sleep_type(WIFI_SLEEP_LIGHT);
    vTaskDelay(10000 / portTICK_PERIOD_MS);
    printf("\r\nStarting again!\r\n");
  }
}

/* ---------- LIGHT SENSOR HANDLER ---------- */
/* ------------------------------------------ */
tsl2561_t *device;

void tsl_init() {
  tsl2561_init(device);
  tsl2561_set_integration_time(device, TSL2561_INTEGRATION_13MS);
  tsl2561_set_gain(device, TSL2561_GAIN_1X);
}

void read_lux() {
  //  uint32_t *lux = malloc(sizeof(uint32_t));
  uint32_t lux = 0;
  tsl2561_read_lux(device, &lux);
  printf("LUX LEVEL %u lux\n", lux);
}

/* ---------- OLED HANDLER ------------------ */
/* ------------------------------------------ */
const int led_gpio = 2;     
bool led_on = false;        //oled status
float led_brightness = 100; //brightness is scaled from 0 to 100 

void led_write(bool on) {
  gpio_write(led_gpio, on ? 0 : 1);
}

void led_set() {
  gpio_write(led_gpio, led_brightness);
}

void led_init() {
  printf("Initializing OLED panel...\n");
  gpio_enable(led_gpio, GPIO_OUTPUT);
  led_write(led_on);
}

void led_identify_task(void *_args) {
  for (int i=0; i<3; i++) {
    for (int j=0; j<2; j++) {
      led_write(true);
      vTaskDelay(100 / portTICK_PERIOD_MS);
      led_write(false);
      vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    vTaskDelay(250 / portTICK_PERIOD_MS);
  }

  led_write(led_on);

  vTaskDelete(NULL);
}

void led_identify(homekit_value_t _value) {
  printf("LED identify\n");
  xTaskCreate(led_identify_task, "LED identify", 128, NULL, 2, NULL);
}

/* ---------- HOMEKIT HANDLER ---------- */
/* --------------------------------- */
homekit_value_t led_on_get() {
  return HOMEKIT_BOOL(led_on);
}

homekit_value_t led_brightness_get() {
  return HOMEKIT_INT(led_brightness);
}

void led_on_set(homekit_value_t value) {
  if (value.format != homekit_format_bool) {
    printf("Invalid value format: %d\n", value.format);
    return;
  }

  led_on = value.bool_value;
  led_write(led_on);
}

void led_brightness_set(homekit_value_t value) {
  if (value.format != homekit_format_int) {
    printf("Invalid brightness-value format: %d\n", value.format);
    return;
  }
  led_brightness = value.int_value;
  led_set();
}

homekit_accessory_t *accessories[] = {
  HOMEKIT_ACCESSORY(.id=1, .category=homekit_accessory_category_lightbulb, .services=(homekit_service_t*[]){
      HOMEKIT_SERVICE(ACCESSORY_INFORMATION, .characteristics=(homekit_characteristic_t*[]){
          HOMEKIT_CHARACTERISTIC(NAME, "LOLA"),
          HOMEKIT_CHARACTERISTIC(MANUFACTURER, "SBOONS"),
          HOMEKIT_CHARACTERISTIC(SERIAL_NUMBER, "037A2BABF19D"),
          HOMEKIT_CHARACTERISTIC(MODEL, "OLED"),
          HOMEKIT_CHARACTERISTIC(FIRMWARE_REVISION, "0.1"),
          HOMEKIT_CHARACTERISTIC(IDENTIFY, led_identify),
          NULL
          }),
      HOMEKIT_SERVICE(LIGHTBULB, .primary=true, .characteristics=(homekit_characteristic_t*[]){
          HOMEKIT_CHARACTERISTIC(NAME, "OLED"),
          HOMEKIT_CHARACTERISTIC(
              ON, false,
              .getter=led_on_get,
              .setter=led_on_set
              ),
          HOMEKIT_CHARACTERISTIC(
              BRIGHTNESS, 100,
              .getter = led_brightness_get,
              .setter = led_brightness_set
              ),
          NULL
          }),
      NULL
  }),
  NULL
};

homekit_server_config_t config = {
  .accessories = accessories,
  .password = "111-11-111"
};

BaseType_t xReturned;
TaskHandle_t xHandle = NULL;

void get_wifi_status() {
  while (sdk_wifi_station_get_connect_status() != STATION_GOT_IP) {
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
  led_write(1);
  wificfg_got_sta_connect();
  homekit_server_init(&config); //start homekit only if wifi available
  if (xReturned == pdPASS) vTaskDelete(xHandle); //stop checking wifi status
}

void homekit_init() {
  xReturned = xTaskCreate(get_wifi_status, "WifiStatus", 500, NULL, 2, &xHandle);
}

/* ---------- MAIN ---------- */
/* --------------------------------- */


void user_init(void) {
  uart_set_baud(0, 115200);

  led_init();
  //wifi_init(); //use if wifi credentials are provided in "wifi.h"
  wifi_config(); //use if no credentials provided
  homekit_init();
  xTaskCreate(&http_post_task, "PostTask", 512, NULL, 2, NULL);
  //tsl_init();
  //read_lux();
}
