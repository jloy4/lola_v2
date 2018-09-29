#include <stdio.h>
#include <espressif/esp_wifi.h>
#include <espressif/esp_sta.h>
#include <espressif/esp_system.h>
#include <esp/uart.h>
#include <esp8266.h>
#include <FreeRTOS.h>
#include <task.h>

#include <unistd.h>
#include <string.h>
#include <time.h>

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "lwip/api.h"

#include <homekit/homekit.h>
#include <homekit/characteristics.h>
#include "wifi.h"

#include <tsl2561/tsl2561.h>
#include <wificfg/wificfg.h>
#include "pwm.h"

#include "bearssl.h"

const int potar_gpio = 5;
uint8_t pwm_pin[1] = {2};
bool led_on = false;      //oled status
int led_brightness = 100; //brightness is scaled from 0 to 100 

bool bright_request = false;
bool new_post_request = false;
BaseType_t xPostReturned;
TaskHandle_t xPostHandle = NULL;


double lux_back = 0;
double lux_front = 0;

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


/* ------------ FIREBASE HANDLER ------------ */
/* ------------------------------------------ */
#define WEB_SERVER "lola-light.firebaseio.com"
#define WEB_PORT "443"
#define WEB_URL "https://lola-light.firebaseio.com/data.json"

#define CLOCK_SECONDS_PER_MINUTE (60UL)
#define CLOCK_MINUTES_PER_HOUR (60UL)
#define CLOCK_HOURS_PER_DAY (24UL)
#define CLOCK_SECONDS_PER_HOUR (CLOCK_MINUTES_PER_HOUR*CLOCK_SECONDS_PER_MINUTE)
#define CLOCK_SECONDS_PER_DAY (CLOCK_HOURS_PER_DAY*CLOCK_SECONDS_PER_HOUR)

#define GET_REQUEST "GET /data.json HTTP/1.1\r\nHost: "WEB_SERVER"\r\nUser-Agent: ESP8266\r\n\r\n"

char request[300];
char content[80];

static int sock_read(void *ctx, unsigned char *buf, size_t len) {
	for (;;) {
		ssize_t rlen;

		rlen = read(*(int *)ctx, buf, len);
		if (rlen <= 0) {
			if (rlen < 0 && errno == EINTR) {
				continue;
			}
			return -1;
		}
		return (int)rlen;
	}
}

static int sock_write(void *ctx, const unsigned char *buf, size_t len) {
	for (;;) {
		ssize_t wlen;

		wlen = write(*(int *)ctx, buf, len);
		if (wlen <= 0) {
			if (wlen < 0 && errno == EINTR) {
				continue;
			}
			return -1;
		}
		return (int)wlen;
	}
}

static const unsigned char TA0_DN[] = {
	0x30, 0x68, 0x31, 0x0B, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13,
	0x02, 0x55, 0x53, 0x31, 0x13, 0x30, 0x11, 0x06, 0x03, 0x55, 0x04, 0x08,
	0x0C, 0x0A, 0x43, 0x61, 0x6C, 0x69, 0x66, 0x6F, 0x72, 0x6E, 0x69, 0x61,
	0x31, 0x16, 0x30, 0x14, 0x06, 0x03, 0x55, 0x04, 0x07, 0x0C, 0x0D, 0x4D,
	0x6F, 0x75, 0x6E, 0x74, 0x61, 0x69, 0x6E, 0x20, 0x56, 0x69, 0x65, 0x77,
	0x31, 0x13, 0x30, 0x11, 0x06, 0x03, 0x55, 0x04, 0x0A, 0x0C, 0x0A, 0x47,
	0x6F, 0x6F, 0x67, 0x6C, 0x65, 0x20, 0x4C, 0x4C, 0x43, 0x31, 0x17, 0x30,
	0x15, 0x06, 0x03, 0x55, 0x04, 0x03, 0x0C, 0x0E, 0x66, 0x69, 0x72, 0x65,
	0x62, 0x61, 0x73, 0x65, 0x69, 0x6F, 0x2E, 0x63, 0x6F, 0x6D
};

static const unsigned char TA0_RSA_N[] = {
	0x92, 0x48, 0xF8, 0x76, 0xFE, 0x78, 0xE8, 0xFF, 0x94, 0xE1, 0x37, 0xC0,
	0xD7, 0x42, 0xC3, 0x6B, 0x09, 0x64, 0x72, 0x1F, 0x6E, 0x51, 0x55, 0xCA,
	0x38, 0x57, 0xB3, 0x50, 0xFF, 0xC8, 0x5C, 0x45, 0x47, 0x77, 0x98, 0x63,
	0x0B, 0x82, 0xBA, 0x51, 0x7F, 0x4A, 0xA5, 0x4E, 0xE5, 0xC6, 0xF7, 0x66,
	0xF8, 0xA7, 0xFD, 0x98, 0x49, 0x1D, 0x1C, 0xE7, 0xD8, 0x5D, 0xA5, 0x3E,
	0x2A, 0x04, 0x7B, 0x4F, 0xE9, 0x8D, 0xA5, 0x4E, 0x0F, 0x2D, 0x1E, 0x0A,
	0x11, 0xB2, 0xD6, 0x16, 0x09, 0xF5, 0x42, 0x25, 0x86, 0x38, 0x49, 0x4B,
	0xC9, 0xC1, 0xE0, 0x2C, 0xF2, 0xF6, 0x0F, 0xA8, 0x16, 0x7E, 0x2A, 0x7F,
	0xA9, 0xF7, 0x6A, 0xD6, 0x69, 0x46, 0xB7, 0x80, 0xE9, 0x97, 0x31, 0xAE,
	0x41, 0x62, 0x7D, 0x77, 0x0B, 0x5F, 0xC8, 0xF4, 0xDF, 0xDC, 0x94, 0xAE,
	0x3A, 0x2A, 0x87, 0x06, 0xA7, 0x2E, 0x37, 0x79, 0xF7, 0xD4, 0x67, 0xEE,
	0x76, 0x51, 0xCB, 0x3A, 0x25, 0x48, 0x83, 0xAA, 0x5F, 0xCA, 0x62, 0xD2,
	0x15, 0x54, 0x0F, 0xCD, 0x7F, 0x7A, 0x77, 0xB0, 0x7A, 0x69, 0x27, 0x52,
	0x5C, 0x7A, 0xE7, 0x1F, 0xD1, 0xA5, 0x8A, 0xE2, 0x2A, 0xAB, 0xF2, 0x81,
	0x76, 0x94, 0xDF, 0x48, 0x07, 0x67, 0xE2, 0x3A, 0xF7, 0xA7, 0xEB, 0x34,
	0x3A, 0xC7, 0x3B, 0xBA, 0xA2, 0x94, 0xE3, 0x0F, 0x6D, 0xAD, 0xF3, 0xDF,
	0x53, 0xB9, 0xD4, 0xE9, 0xB6, 0x10, 0x37, 0x2C, 0x24, 0x44, 0x1E, 0x6F,
	0x57, 0x55, 0x4D, 0xBE, 0x3A, 0x0F, 0x4F, 0x46, 0x2E, 0x9D, 0xD3, 0x82,
	0x0A, 0xDB, 0x7C, 0x31, 0xC1, 0x7E, 0x5B, 0x90, 0xDE, 0x4C, 0x81, 0xFE,
	0xAA, 0xBC, 0x63, 0xA0, 0x82, 0x02, 0x8D, 0xB0, 0x14, 0x26, 0xB5, 0x91,
	0xD6, 0xD1, 0x00, 0xEE, 0x11, 0x84, 0x14, 0x23, 0x5A, 0xE7, 0xAC, 0x93,
	0xAE, 0xFF, 0x37, 0xCF
};

static const unsigned char TA0_RSA_E[] = {
	0x01, 0x00, 0x01
};

static const br_x509_trust_anchor TAs[1] = {
	{
		{ (unsigned char *)TA0_DN, sizeof TA0_DN },
		0,
		{
			BR_KEYTYPE_RSA,
			{ .rsa = {
				(unsigned char *)TA0_RSA_N, sizeof TA0_RSA_N,
				(unsigned char *)TA0_RSA_E, sizeof TA0_RSA_E,
			} }
		}
	}
};

#define TAs_NUM   1

static unsigned char bearssl_buffer[BR_SSL_BUFSIZE_MONO];

static br_ssl_client_context sc;
static br_x509_minimal_context xc;
static br_sslio_context ioc;

void post_task(void *pvParameters) {
printf("POSTING DATA\n");

	//wait 40*500=20,000ms to make sure no other post request has been sent
	/*new_post_request = false;
	for (int i=0; i<40; i++) {
		if (new_post_request) vTaskDelete(NULL);
		vTaskDelay(200 / portTICK_PERIOD_MS);
		printf("%d ", i);
	}
	new_post_request = true;*/

	//int successes = 0, failures = 0;
	int provisional_time = 0;

	const struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
	};
	struct addrinfo *res = NULL;
	int dns_err = 0;
	do {
		if (res)
			freeaddrinfo(res);
		vTaskDelay(1000 / portTICK_PERIOD_MS);
		dns_err = getaddrinfo(WEB_SERVER, WEB_PORT, &hints, &res);
	} while(dns_err != 0 || res == NULL);

	int fd = socket(res->ai_family, res->ai_socktype, 0);
	if (fd < 0) {
		freeaddrinfo(res);
		printf("socket failed\n");
		//failures++;
		return;
	}

	printf("Initializing BearSSL... ");
	br_ssl_client_init_full(&sc, &xc, TAs, TAs_NUM);

	br_ssl_engine_set_buffer(&sc.eng, bearssl_buffer, sizeof bearssl_buffer, 0);

	for (int i = 0; i < 10; i++) {
		int rand = hwrand();
		br_ssl_engine_inject_entropy(&sc.eng, &rand, 4);
	}

	br_ssl_client_reset(&sc, WEB_SERVER, 0);

	br_sslio_init(&ioc, &sc.eng, sock_read, &fd, sock_write, &fd);
	printf("done.\r\n");

	//FIXME: set date & time using epoch time precompiler flag for now 
	provisional_time = CONFIG_EPOCH_TIME + (xTaskGetTickCount()/configTICK_RATE_HZ);
	xc.days = (provisional_time / CLOCK_SECONDS_PER_DAY) + 719528;
	xc.seconds = provisional_time % CLOCK_SECONDS_PER_DAY;
	printf("Time: %02i:%02i\r\n",
			(int)(xc.seconds / CLOCK_SECONDS_PER_HOUR),
			(int)((xc.seconds % CLOCK_SECONDS_PER_HOUR)/CLOCK_SECONDS_PER_MINUTE)
			);

	if (connect(fd, res->ai_addr, res->ai_addrlen) != 0)
	{
		close(fd);
		freeaddrinfo(res);
		printf("connect failed\n");
		//failures++;
		return;
	}
	printf("Connected\r\n");

	request[0] = "\0";
	content[0] = "\0";

	snprintf(content, 80, "{\"sensors\":{\"brightness\":%d,\"lux_back\":%.1f,\"lux_front\":%.1f}}", led_brightness, lux_back, lux_front);

	snprintf(request, 300, "POST /data.json HTTP/1.1\r\nHost: "WEB_SERVER"\r\nUser-Agent: ESP8266\r\nContent-Length: %d\r\nContent-Type: application/x-www-form-urlencoded\r\n\r\n%s", strlen(content), content);
		printf(request);

		if (br_sslio_write_all(&ioc, request, strlen(request)) != BR_ERR_OK) {
			close(fd);
			freeaddrinfo(res);
			printf("br_sslio_write_all failed: %d\r\n", br_ssl_engine_last_error(&sc.eng));
			//failures++;
			return;
		}

		br_sslio_flush(&ioc);

		/*for (;;)
		{
			int rlen;
			unsigned char buf[128];

			bzero(buf, 128);
			// Leave the final byte for zero termination
			rlen = br_sslio_read(&ioc, buf, sizeof(buf) - 1);
printf("\nrlen: %d\n", rlen);

			if (rlen < 0) {
				break;
			}
			if (rlen > 0) {
				printf("%s", buf);
			}
		}*/

		if (br_ssl_engine_last_error(&sc.eng) != BR_ERR_OK) {
			close(fd);
			freeaddrinfo(res);
			printf("failure, error = %d\r\n", br_ssl_engine_last_error(&sc.eng));
			//failures++;
			return;
		}

		printf("\r\n\r\nfree heap pre  = %u\r\n", xPortGetFreeHeapSize());

		close(fd);
		freeaddrinfo(res);

		printf("free heap post = %u\r\n", xPortGetFreeHeapSize());

		//successes++;
		printf("data posted");
		
		new_post_request = false;
		vTaskDelete(NULL);
}


/* ---------- LIGHT SENSOR HANDLER ---------- */
/* ------------------------------------------ */
tsl2561_t *device;

void tsl_init() {
	printf("Initializing light sensors...");
  tsl2561_init(&device);
	printf("init done...");
  tsl2561_set_integration_time(&device, TSL2561_INTEGRATION_13MS);
  tsl2561_set_gain(&device, TSL2561_GAIN_1X);
	printf("Done\n");
	vTaskDelete(NULL);
}

void read_lux() {
  //  uint32_t *lux = malloc(sizeof(uint32_t));
  uint32_t lux = 0;
  tsl2561_read_lux(device, &lux);
  printf("LUX LEVEL %u lux\n", lux);
}

/* ---------- OLED HANDLER ------------------ */
/* ------------------------------------------ */
#define MIN_POTAR 21
#define MAX_POTAR 1024
#define NOISE     10

void led_set() {
	if (led_brightness >= 0 && led_brightness <= 100) 
		pwm_set_duty(led_brightness*UINT16_MAX/100);	
}

void led_write(bool on, bool post) {
	printf("bright_request: %d, post: %d, new_post: %d\n", bright_request, post, new_post_request);
	if (!bright_request) {
		if (!on) {
			led_brightness = 0;
			led_set();
		} else {
			if (led_brightness != 0) led_set();
			else {
				led_brightness = 100;
				led_set();
			}
		}
		if (post && !new_post_request) {
			xPostReturned = xTaskCreate(post_task, "PostTask", 2048, NULL, 2, NULL);
			if (xPostReturned == pdPASS) new_post_request = true; //handles the case where the task is not created
			//post_task(NULL);
		}
	}	
	else bright_request = false;
}

uint16_t read_0 = MAX_POTAR;
uint16_t read_1 = MAX_POTAR;
void potar_task() {
	while (1) {
		read_1 = sdk_system_adc_read();
		//printf("POTAR: %d\n", read_1);
		if (read_1 > read_0 + NOISE || read_1 < read_0 - NOISE) {
			if (read_1 < MIN_POTAR) {
				led_on = false;
				led_brightness = 0;
			}
			else {
				led_on = true;
				led_brightness = read_1*100/(MAX_POTAR-MIN_POTAR);
			}
			led_set();
			read_0 = read_1;
		}
		vTaskDelay(100 / portTICK_PERIOD_MS);	
	}
}

void led_init() {
  printf("Initializing OLED panel...");
	pwm_init(1, pwm_pin, true);
	pwm_set_freq(1000);
	pwm_set_duty(UINT16_MAX/2);
	pwm_start();
	printf("Done\n");
  //led_write(led_on);
}

void led_identify_task(void *_args) {
  for (int i=0; i<3; i++) {
    for (int j=0; j<2; j++) {
      led_write(true, false);
      vTaskDelay(100 / portTICK_PERIOD_MS);
      led_write(false, false);
      vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    vTaskDelay(250 / portTICK_PERIOD_MS);
  }

  led_write(led_on, false);

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
	led_write(led_on, true);
}

void led_brightness_set(homekit_value_t value) {
  if (value.format != homekit_format_int) {
    printf("Invalid brightness-value format: %d\n", value.format);
    return;
  }

  led_brightness = value.int_value;
	bright_request = true;
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
	//led_write(true, false);
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
  wifi_init(); //use if wifi credentials are provided in "wifi.h"
  //wifi_config(); //use if no credentials provided
  homekit_init();
	//xTaskCreate(potar_task, "Potar", 500, NULL, 2, NULL);
  //xTaskCreate(tsl_init, "TSLInit", 512, NULL, 2, NULL);
  //read_lux();
}
