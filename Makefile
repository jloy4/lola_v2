PROGRAM = lola

EXTRA_COMPONENTS = \
	extras/http-parser \
	extras/tsl2561 \
	extras/i2c \
	extras/wificfg \
	extras/dhcpserver \
	extras/bearssl \
	$(abspath ../../components/wolfssl) \
	$(abspath ../../components/cJSON) \
	$(abspath ../../components/homekit) \

FLASH_SIZE ?= 32

EXTRA_CFLAGS += -I../.. -DHOMEKIT_SHORT_APPLE_UUIDS -DCONFIG_EPOCH_TIME=$(shell date --utc '+%s')

include $(SDK_PATH)/common.mk

monitor:
	$(FILTEROUTPUT) --port $(ESPPORT) --baud 115200 --elf $(PROGRAM_OUT)
