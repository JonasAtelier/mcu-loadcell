/*
 * Shared by the chip drivers inside this library. Not public API.
 *
 * Two layers sit under the chips, the way regmap sits under a kernel driver:
 *
 *   - The bus kind is chosen at run time. A chip holds a struct loadcell_bus
 *     and reaches it only through bus->ops, so a driver can sit on I2C, SPI
 *     or a UART. Every category library offers all three kinds, used or not:
 *     the NAU7802 is on I2C.
 *   - The platform is chosen at link time. loadcell_port_esp.c and
 *     loadcell_port_stm.c each define the ops tables, the attach functions,
 *     the pins, the microsecond delay, the interrupt mask and the waits
 *     below for their platform; the other one compiles to nothing.
 *
 * The HX711 is not a bus chip: it is two pins, a clock the MCU drives and a
 * data line it reads, and sits outside the bus layer on the pin, delay and
 * mask helpers.
 *
 * ESP_PLATFORM, which ESP-IDF defines for every component, and the HAL's own
 * module switches are defined or not, never 0 or 1, so they are the one place
 * #ifdef is right; this library's CONFIG_LOADCELL_* options are always tested
 * with #if.
 */
#ifndef LOADCELL_PRIV_H
#define LOADCELL_PRIV_H

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The most bytes after cmd: tx on I2C and UART, tx and rx together on SPI. */
#define LOADCELL_BUS_MAX_LEN	31

struct loadcell_bus;

enum loadcell_bus_kind {
	LOADCELL_BUS_I2C,
	LOADCELL_BUS_SPI,
	LOADCELL_BUS_UART,
};

struct loadcell_bus_ops {
	enum loadcell_bus_kind kind;
	/*
	 * One transaction: cmd, then tx_len bytes of tx, then rx_len bytes
	 * into rx. cmd is the register address, with any SPI read bit already
	 * set by the chip, and the first byte of a datagram on a UART.
	 *
	 *   I2C:  rx_len ? cmd, repeated start, read : cmd and tx, one write;
	 *         a bare cmd is a one-byte write. Sending tx and then reading
	 *         is -EINVAL.
	 *   SPI:  CS low, cmd, tx, rx, CS high -- high again even when the
	 *         transfer failed.
	 *   UART: drop stale input, send cmd and tx, then wait for rx_len
	 *         bytes; fewer within the timeout is -ETIMEDOUT. For a line
	 *         where the MCU does not hear itself; see exchange().
	 *
	 * On SPI more than LOADCELL_BUS_MAX_LEN bytes after cmd is -EINVAL; on
	 * I2C and UART only tx is bounded so, and rx on the HAL by its 16-bit
	 * count. 0 or a negative errno.
	 */
	int (*transfer)(struct loadcell_bus *bus, uint8_t cmd,
			const uint8_t *tx, size_t tx_len, uint8_t *rx,
			size_t rx_len);
	/*
	 * len bytes of tx out while len bytes come back into rx, rx[i] the
	 * one that answered tx[i]. More than 1 + LOADCELL_BUS_MAX_LEN bytes, or
	 * none, is -EINVAL. NULL on I2C.
	 *
	 *   SPI:  one frame, full duplex: CS low, the bytes at the same clocks,
	 *         CS high -- high again even when the transfer failed. For a
	 *         part whose answer to one frame comes back in the next.
	 *   UART: drop stale input, send tx, and collect what the line gave
	 *         back meanwhile. On a single wire the MCU hears its own
	 *         transmission; this is how a chip takes that echo off the
	 *         line, so that the answer which follows starts clean. Fewer
	 *         than len bytes back within the timeout is -ETIMEDOUT.
	 */
	int (*exchange)(struct loadcell_bus *bus, const uint8_t *tx,
			uint8_t *rx, size_t len);
	/*
	 * rx_len bytes, with nothing sent and nothing dropped first: the
	 * answer that follows an exchange(). Fewer within the timeout is
	 * -ETIMEDOUT; rx is then undefined. NULL on I2C and SPI.
	 */
	int (*receive)(struct loadcell_bus *bus, uint8_t *rx, size_t rx_len);
	/* Undo the attach. The bus itself is the app's and is left alone. */
	void (*detach)(struct loadcell_bus *bus);
};

#ifdef ESP_PLATFORM
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "driver/uart.h"
#include "esp_err.h"

/* The I2C argument of every I2C attach on this platform. */
typedef i2c_master_bus_handle_t loadcell_i2c_bus;

/*
 * One device on one bus, as its driver holds it. LOADCELL_HAVE_BUS says the
 * platform has at least one bus kind, so the struct exists.
 */
#define LOADCELL_HAVE_BUS
struct loadcell_bus {
	const struct loadcell_bus_ops *ops;
	uint32_t timeout_ms;
	union {
		i2c_master_dev_handle_t i2c;
		spi_device_handle_t spi;
		uart_port_t uart;
	};
};

/* A pin the driver configures and drives or reads. */
struct loadcell_pin {
	gpio_num_t gpio;
};

/*
 * esp_err_t -> negative errno, at the bus boundary and nowhere else.
 * ESP_ERR_NOT_FOUND is -ENXIO (nothing at that address), not -ENODEV
 * (something answered but it is the wrong chip).
 */
static inline int loadcell_errno(esp_err_t err)
{
	switch (err) {
	case ESP_OK:
		return 0;
	case ESP_ERR_INVALID_ARG:
		return -EINVAL;
	case ESP_ERR_TIMEOUT:
		return -ETIMEDOUT;
	case ESP_ERR_NOT_FOUND:
		return -ENXIO;
	case ESP_ERR_INVALID_SIZE:
		return -EMSGSIZE;
	case ESP_ERR_NO_MEM:
		return -ENOMEM;
	case ESP_ERR_NOT_SUPPORTED:
		return -EOPNOTSUPP;
	default:
		return -EIO;
	}
}

/*
 * negative errno -> esp_err_t, for the chips' handle APIs, which return
 * esp_err_t on ESP-IDF as the original drivers did. The codes the originals
 * returned come back as they were: -ENODEV and -ENXIO as
 * ESP_ERR_NOT_FOUND; -ERANGE (taring a saturated reading) and -EPROTO (the
 * NAU7802 reporting a failed calibration) as ESP_ERR_INVALID_STATE. The bus
 * layer's own mapping is lossy in this direction: an IDF code with no errno
 * of its own came up as -EIO and goes back as ESP_FAIL.
 */
static inline esp_err_t loadcell_esp_err(int err)
{
	switch (err) {
	case 0:
		return ESP_OK;
	case -EINVAL:
		return ESP_ERR_INVALID_ARG;
	case -ENOMEM:
		return ESP_ERR_NO_MEM;
	case -ETIMEDOUT:
		return ESP_ERR_TIMEOUT;
	case -EOPNOTSUPP:
		return ESP_ERR_NOT_SUPPORTED;
	case -ENODEV:
	case -ENXIO:
		return ESP_ERR_NOT_FOUND;
	case -EMSGSIZE:
		return ESP_ERR_INVALID_SIZE;
	case -ERANGE:
	case -EPROTO:
		return ESP_ERR_INVALID_STATE;
	default:
		return ESP_FAIL;
	}
}

#else /* STM32 HAL */
#include CMSIS_device_header

#if !defined(HAL_I2C_MODULE_ENABLED) && CONFIG_LOADCELL_NAU7802
#error "the NAU7802 driver needs the HAL I2C module: #define HAL_I2C_MODULE_ENABLED in the app's stm32<family>xx_hal_conf.h, with stm32<family>xx_hal_i2c.c in the build"
#endif

#ifdef HAL_I2C_MODULE_ENABLED
typedef I2C_HandleTypeDef *loadcell_i2c_bus;
#endif

#if defined(HAL_I2C_MODULE_ENABLED) || defined(HAL_SPI_MODULE_ENABLED) || \
    defined(HAL_UART_MODULE_ENABLED)
#define LOADCELL_HAVE_BUS
struct loadcell_bus {
	const struct loadcell_bus_ops *ops;
	uint32_t timeout_ms;
	union {
#ifdef HAL_I2C_MODULE_ENABLED
		struct {
			I2C_HandleTypeDef *handle;
			uint8_t address;	/* 7-bit */
		} i2c;
#endif
#ifdef HAL_SPI_MODULE_ENABLED
		struct {
			SPI_HandleTypeDef *handle;
			GPIO_TypeDef *cs_port;	/* app's output, idle high */
			uint16_t cs_pin;
		} spi;
#endif
#ifdef HAL_UART_MODULE_ENABLED
		UART_HandleTypeDef *uart;
#endif
	};
};
#endif

/* A pin the app has set up, input or output; the driver drives or reads it. */
struct loadcell_pin {
	GPIO_TypeDef *port;
	uint16_t pin;
};

/*
 * HAL_StatusTypeDef -> negative errno, at the bus boundary and nowhere else.
 * HAL_BUSY -- the bus or the handle in use, or a handle never initialised --
 * is -EAGAIN. A NACK in the middle of a transfer is HAL_ERROR like any other
 * fault, so -EIO.
 */
static inline int loadcell_hal_errno(HAL_StatusTypeDef status)
{
	switch (status) {
	case HAL_OK:
		return 0;
	case HAL_TIMEOUT:
		return -ETIMEDOUT;
	case HAL_BUSY:
		return -EAGAIN;
	default:
		return -EIO;
	}
}

#endif /* STM32 HAL */

/*
 * The chips' handle APIs return esp_err_t on ESP-IDF, as the original
 * drivers did, and 0 or a negative errno on the HAL. The drivers work in
 * errno and convert once, where each public handle function returns.
 */
#ifdef ESP_PLATFORM
typedef esp_err_t loadcell_handle_ret;
#define LOADCELL_HANDLE_RET(err)	loadcell_esp_err(err)
#else
typedef int loadcell_handle_ret;
#define LOADCELL_HANDLE_RET(err)	(err)
#endif

#ifdef LOADCELL_HAVE_BUS
/* bus->ops->exchange(), or -EOPNOTSUPP on a kind that has none. */
static inline int loadcell_bus_exchange(struct loadcell_bus *bus,
					const uint8_t *tx, uint8_t *rx,
					size_t len)
{
	if (!bus->ops->exchange)
		return -EOPNOTSUPP;
	return bus->ops->exchange(bus, tx, rx, len);
}

/* bus->ops->receive(), or -EOPNOTSUPP on a kind that has none. */
static inline int loadcell_bus_receive(struct loadcell_bus *bus, uint8_t *rx,
				       size_t rx_len)
{
	if (!bus->ops->receive)
		return -EOPNOTSUPP;
	return bus->ops->receive(bus, rx, rx_len);
}

static inline void loadcell_bus_detach(struct loadcell_bus *bus)
{
	bus->ops->detach(bus);
}
#endif

#if defined(ESP_PLATFORM) || defined(HAL_I2C_MODULE_ENABLED)
/*
 * Is anything at this 7-bit address? -ENXIO when nothing ACKs -- nothing at
 * that address -- not -ENODEV, which means something answered but it is the
 * wrong chip. A NACK in the middle of a transfer is a plain -EIO on both
 * platforms, which is why a driver decides presence here, never by its
 * first transfer.
 */
int loadcell_i2c_probe(loadcell_i2c_bus handle, uint8_t address,
		       uint32_t timeout_ms);

/*
 * Bind bus to the I2C device at address. On ESP-IDF the driver adds the
 * device at scl_hz, 0 for 400 kHz; on the HAL the clock is the handle's and
 * scl_hz is ignored. Undo with loadcell_bus_detach(). Every attach leaves
 * bus->ops NULL when it fails, so a bus with ops is one with something to
 * detach.
 */
int loadcell_i2c_attach(struct loadcell_bus *bus, loadcell_i2c_bus handle,
			uint8_t address, uint32_t scl_hz, uint32_t timeout_ms);
#endif

#ifdef ESP_PLATFORM
/*
 * Add the SPI device on host's cs_pin, in SPI mode 0-3 at clock_hz. The
 * host is the app's, already spi_bus_initialize()d.
 */
int loadcell_spi_attach(struct loadcell_bus *bus, spi_host_device_t host,
			int cs_pin, uint32_t clock_hz, uint8_t mode);

/* port's driver is the app's, already installed and configured. */
int loadcell_uart_attach(struct loadcell_bus *bus, uart_port_t port,
			 uint32_t timeout_ms);
#else
#ifdef HAL_SPI_MODULE_ENABLED
/*
 * Bind bus to the SPI device behind cs_port/cs_pin, and deselect it, so the
 * first transfer starts on a falling edge. Mode, clock and frame are the
 * handle's; the chip select is an output the app has set up.
 */
int loadcell_spi_attach(struct loadcell_bus *bus, SPI_HandleTypeDef *handle,
			GPIO_TypeDef *cs_port, uint16_t cs_pin,
			uint32_t timeout_ms);
#endif
#ifdef HAL_UART_MODULE_ENABLED
/* handle is the app's, already HAL_UART_Init()ed. */
int loadcell_uart_attach(struct loadcell_bus *bus, UART_HandleTypeDef *handle,
			 uint32_t timeout_ms);
#endif
#endif

/*
 * The HX711's two pins. On ESP-IDF loadcell_pin_config() makes the GPIO an
 * input or an output with gpio_config(), as esp-hx711-loadcell did; on the
 * HAL the app has set the pin up and this does nothing. Then the driver
 * drives it with loadcell_pin_set() or reads it with loadcell_pin_get().
 */
int loadcell_pin_config(const struct loadcell_pin *pin, bool output);
void loadcell_pin_set(const struct loadcell_pin *pin, bool high);
bool loadcell_pin_get(const struct loadcell_pin *pin);

/*
 * Busy-wait at least us microseconds, interrupts masked or not. ESP-IDF:
 * esp_rom_delay_us(). HAL: a weak default that counts SysTick; see
 * loadcell_port_stm.c.
 */
void loadcell_delay_us(uint32_t us);

/*
 * Mask interrupts around a few microseconds that must not stretch, and put
 * them back as they were. ESP-IDF: portENTER/EXIT_CRITICAL on one spinlock,
 * as esp-hx711-loadcell did (the return value is 0 and unused). HAL: PRIMASK
 * saved, set, and restored.
 */
uint32_t loadcell_irq_off(void);
void loadcell_irq_restore(uint32_t state);

/*
 * Polling a status bit until a deadline, the way esp-tmc5240-stepper did:
 * loadcell_tick_now() starts the clock, loadcell_expired() says whether
 * limit_ms has surely passed -- at once for 0, never for UINT32_MAX -- and
 * loadcell_poll_wait() pauses between two polls, at least one tick.
 */
uint32_t loadcell_tick_now(void);
bool loadcell_expired(uint32_t start, uint32_t limit_ms);
void loadcell_poll_wait(uint32_t ms);

#endif /* LOADCELL_PRIV_H */
