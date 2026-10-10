CC ?= cc
# gnu11, not the c11 the original drivers' tests used: the loadcell_* log
# macros use `, ##__VA_ARGS__`.
CFLAGS += -std=gnu11 -Wall -Wextra -Werror -pedantic
# Signed overflow usually wraps anyway, so a test of wrapping arithmetic
# passes on the broken code too. UBSan makes it abort instead.
CFLAGS += -fsanitize=undefined -fno-sanitize-recover=all
# src/ for the common private header, and every chip folder, because the
# tests include a chip's private headers by plain name.
CPPFLAGS += -I../include -I../src $(addprefix -I,$(wildcard ../src/*/))

# Two suites, one per platform: the same chip drivers through
# src/loadcell_port_esp.c against idf_stubs/ (esp/), and through
# src/loadcell_port_stm.c against hal_stubs/ (stm/). esp/test_hx711.c and
# esp/test_nau7802.c are the original drivers' own tests, and
# test_<chip>_math.c their maths tests. The cases written for this library
# live once, in *_cases.h, and both platforms run them against their own
# fakes. The maths tests touch no bus or platform API and run once.

# What ESP-IDF passes: ESP_PLATFORM, and every chip on.
ESP_CPPFLAGS := -Iidf_stubs -DESP_PLATFORM \
		-DCONFIG_LOADCELL_HX711=1 -DCONFIG_LOADCELL_NAU7802=1
# What stm-forge passes: the family header the code includes, the HAL modules
# an app's hal_conf would enable, and every chip option on
# (LOADCELL_<CHIP> = y).
STM_CPPFLAGS := -Ihal_stubs \
		-DCMSIS_device_header='"stm32_hal_stub.h"' \
		-DHAL_I2C_MODULE_ENABLED -DHAL_SPI_MODULE_ENABLED \
		-DHAL_UART_MODULE_ENABLED \
		-DCONFIG_LOADCELL_HX711=1 -DCONFIG_LOADCELL_NAU7802=1 \
		-DCONFIG_LOADCELL_DEBUG=1

# The core and the stubs every binary of a suite links.
CORE     := ../src/loadcell.c ../src/loadcell_log.c
ESP_CORE := $(CORE) ../src/loadcell_port_esp.c idf_stubs/idf_stubs.c
STM_CORE := $(CORE) ../src/loadcell_port_stm.c hal_stubs/hal_stubs.c

HX711   := ../src/hx711/hx711.c
NAU7802 := ../src/nau7802/nau7802.c

MATH_TESTS := test_hx711_math test_nau7802_math
# Plus one port test each, for the bus ops, pins, delay, mask and waits.
ESP_TESTS  := $(addprefix esp/,test_core test_hx711 test_hx711_shared \
		test_nau7802 test_nau7802_shared test_port_esp)
STM_TESTS  := $(addprefix stm/,test_core test_hx711 test_nau7802 \
		test_port_stm)
TESTS      := $(MATH_TESTS) $(ESP_TESTS) $(STM_TESTS)

.PHONY: all clean test

all: $(TESTS)

test_hx711_math: test_hx711_math.c
test_nau7802_math: test_nau7802_math.c

esp/test_core: esp/test_core.c $(ESP_CORE)
esp/test_hx711: esp/test_hx711.c $(HX711) $(ESP_CORE)
esp/test_hx711_shared: esp/test_hx711_shared.c $(HX711) $(ESP_CORE)
esp/test_nau7802: esp/test_nau7802.c $(NAU7802) $(ESP_CORE)
esp/test_nau7802_shared: esp/test_nau7802_shared.c $(NAU7802) $(ESP_CORE)
esp/test_port_esp: esp/test_port_esp.c $(ESP_CORE)

stm/test_core: stm/test_core.c $(STM_CORE)
stm/test_hx711: stm/test_hx711.c $(HX711) $(STM_CORE)
stm/test_nau7802: stm/test_nau7802.c $(NAU7802) $(STM_CORE)
stm/test_port_stm: stm/test_port_stm.c $(STM_CORE)

$(ESP_TESTS): CPPFLAGS += $(ESP_CPPFLAGS)
$(STM_TESTS): CPPFLAGS += $(STM_CPPFLAGS)

# Lets this library's driver tests count calloc() and free(), to prove a
# failed init leaks nothing. GNU ld; only the drivers' own references are
# wrapped. The original drivers' tests wrap nothing, as they never did.
$(addprefix esp/,test_hx711_shared test_nau7802_shared) \
$(addprefix stm/,test_hx711 test_nau7802): \
	LDFLAGS += -Wl,--wrap=calloc -Wl,--wrap=free
# Every printf() in a binary that links the HAL stubs also lands in
# stub_log_last, so a test can check what was logged.
$(STM_TESTS): LDFLAGS += -Wl,--wrap=printf

$(TESTS):
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(filter %.c,$^) -o $@ -lm

test: $(TESTS)
	@set -e; for t in $(TESTS); do echo "./$$t"; ./$$t; done

clean:
	rm -f $(TESTS)

# Headers are prerequisites so that editing one rebuilds. Without this, `make
# test` reruns a stale binary and reports green for code it never compiled.
# Recipes use $(filter %.c,$^) so the headers never reach the compiler.
HDRS  := $(wildcard ../include/*.h ../src/*.h ../src/*/*.h)
STUBS := $(wildcard idf_stubs/*.h idf_stubs/*/*.h hal_stubs/*.h)
$(TESTS): $(HDRS) $(STUBS) $(wildcard *_cases.h)
