#include <stdio.h>
#include <espressif/esp_wifi.h>
#include <espressif/esp_sta.h>
#include <esp/uart.h>
#include <esp8266.h>
#include <FreeRTOS.h>
#include <FreeRTOSConfig.h>
#include <task.h>

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
  //while (1);
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

/* ---------- MAIN ---------- */
/* --------------------------------- */
void user_init(void) {
  uart_set_baud(0, 115200);

  led_init();
  //wifi_init();
  wifi_config();
  if (sdk_wifi_station_get_connect_status() == STATION_GOT_IP) {
    homekit_server_init(&config);
  }
  //tsl_init();
  //read_lux();
}

