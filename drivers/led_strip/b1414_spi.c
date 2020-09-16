/*
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT everlight_b1414_spi

#include <drivers/led_strip.h>

#include <string.h>

#define LOG_LEVEL CONFIG_LED_STRIP_LOG_LEVEL
#include <logging/log.h>
LOG_MODULE_REGISTER(b1414_spi);

#include <zephyr.h>
#include <device.h>
#include <drivers/spi.h>
#include <sys/math_extras.h>
#include <sys/util.h>

/* spi-one-frame and spi-zero-frame in DT are for 8-bit frames. */
#define SPI_FRAME_BITS 8

/*
 * Delay time to make sure the strip has latched a signal.
 *
 * Despite datasheet claims, a 6 microsecond delay is enough to reset
 * the strip. Delay for 8 usec just to be safe.
 */
#define RESET_DELAY_USEC 250

/*
 * SPI master configuration:
 *
 * - mode 0 (the default), 8 bit, MSB first (arbitrary), one-line SPI
 * - no shenanigans (don't hold CS, don't hold the device lock, this
 *   isn't an EEPROM)
 */
#define SPI_OPER (SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB | \
		  SPI_WORD_SET(SPI_FRAME_BITS) | SPI_LINES_SINGLE)

#define BYTES_PER_PX (24)

struct b1414_spi_data {
	const struct device *spi;
};

struct b1414_spi_cfg {
	struct spi_config spi_cfg;
	uint8_t *px_buf;
	size_t px_buf_size;
	uint8_t one_frame;
	uint8_t zero_frame;
};

static struct b1414_spi_data *dev_data(const struct device *dev)
{
	return dev->data;
}

static const struct b1414_spi_cfg *dev_cfg(const struct device *dev)
{
	return dev->config;
}

/*
 * Serialize an 8-bit color channel value into an equivalent sequence
 * of SPI frames, MSbit first, where a one bit becomes SPI frame
 * one_frame, and zero bit becomes zero_frame.
 */
static inline void b1414_spi_ser(uint8_t buf[8], uint8_t color,
				  const uint8_t one_frame, const uint8_t zero_frame)
{
	int i;

	for (i = 0; i < 8; i++) {
		buf[i] = color & BIT(7 - i) ? one_frame : zero_frame;
	}
}

/*
 * Returns true if and only if cfg->px_buf is big enough to convert
 * num_pixels RGB color values into SPI frames.
 */
static inline bool num_pixels_ok(const struct b1414_spi_cfg *cfg,
				 size_t num_pixels)
{
	size_t nbytes;
	bool overflow;

	overflow = size_mul_overflow(num_pixels, BYTES_PER_PX, &nbytes);
	return !overflow && (nbytes <= cfg->px_buf_size);
}

/*
 * Latch the programmed color values.
 */
static inline void b1414_reset_delay(void)
{
	k_usleep(RESET_DELAY_USEC);
}

static int b1414_strip_update_rgb(const struct device *dev,
				   struct led_rgb *pixels,
				   size_t num_pixels)
{
	const struct b1414_spi_cfg *cfg = dev_cfg(dev);
	const uint8_t one = cfg->one_frame, zero = cfg->zero_frame;
	struct spi_buf buf = {
		.buf = cfg->px_buf,
		.len = cfg->px_buf_size,
	};
	const struct spi_buf_set tx = {
		.buffers = &buf,
		.count = 1
	};
	uint8_t *px_buf = cfg->px_buf;
	size_t i;
	int rc;

	if (!num_pixels_ok(cfg, num_pixels)) {
		return -ENOMEM;
	}

	/*
	 * Convert pixel data into SPI frames. Each frame has pixel
	 * data in BGR on-wire format
	 */
	for (i = 0; i < num_pixels; i++) {
		b1414_spi_ser(px_buf, pixels[i].b, one, zero);
		b1414_spi_ser(px_buf + 8, pixels[i].g, one, zero);
		b1414_spi_ser(px_buf + 16, pixels[i].r, one, zero);
		px_buf += 24;
	}

	/*
	 * Display the pixel data.
	 */
	rc = spi_write(dev_data(dev)->spi, &cfg->spi_cfg, &tx);
	b1414_reset_delay();

	return rc;
}

static int b1414_strip_update_channels(const struct device *dev,
					uint8_t *channels,
					size_t num_channels)
{
	LOG_ERR("update_channels not implemented");
	return -ENOTSUP;
}

static const struct led_strip_driver_api b1414_spi_api = {
	.update_rgb = b1414_strip_update_rgb,
	.update_channels = b1414_strip_update_channels,
};

#define B1414_SPI_NUM_PIXELS(idx) \
	(DT_INST_PROP(idx, chain_length))
#define B1414_SPI_BUS(idx) \
	(DT_INST_BUS_LABEL(idx))
#define B1414_SPI_SLAVE(idx) \
	(DT_INST_REG_ADDR(idx))
#define B1414_SPI_FREQ(idx) \
	(DT_INST_PROP(idx, spi_max_frequency))
#define B1414_SPI_ONE_FRAME(idx) \
	(DT_INST_PROP(idx, spi_one_frame))
#define B1414_SPI_ZERO_FRAME(idx)\
	(DT_INST_PROP(idx, spi_zero_frame))
#define B1414_SPI_BUFSZ(idx) \
	(BYTES_PER_PX * B1414_SPI_NUM_PIXELS(idx))

#define B1414_SPI_DEVICE(idx)						\
									\
static struct b1414_spi_data b1414_spi_##idx##_data;			\
									\
static uint8_t b1414_spi_##idx##_px_buf[B1414_SPI_BUFSZ(idx)];		\
									\
static const struct b1414_spi_cfg b1414_spi_##idx##_cfg = {		\
	.spi_cfg = {							\
		.frequency = B1414_SPI_FREQ(idx),			\
		.operation = SPI_OPER,					\
		.slave = B1414_SPI_SLAVE(idx),				\
		.cs = NULL,						\
	},								\
	.px_buf = b1414_spi_##idx##_px_buf,				\
	.px_buf_size = B1414_SPI_BUFSZ(idx),				\
	.one_frame = B1414_SPI_ONE_FRAME(idx),				\
	.zero_frame = B1414_SPI_ZERO_FRAME(idx),			\
};									\
									\
static int b1414_spi_##idx##_init(const struct device *dev)		\
{									\
	struct b1414_spi_data *data = dev_data(dev);			\
									\
	data->spi = device_get_binding(B1414_SPI_BUS(idx));		\
	if (!data->spi) {						\
		LOG_ERR("SPI device %s not found",			\
			B1414_SPI_BUS(idx));				\
		return -ENODEV;						\
	}								\
									\
	return 0;							\
}									\
									\
DEVICE_DT_INST_DEFINE(idx,						\
		      b1414_spi_##idx##_init,				\
		      device_pm_control_nop,				\
		      &b1414_spi_##idx##_data,				\
		      &b1414_spi_##idx##_cfg,				\
		      POST_KERNEL,					\
		      CONFIG_LED_STRIP_INIT_PRIORITY,			\
		      &b1414_spi_api);

DT_INST_FOREACH_STATUS_OKAY(B1414_SPI_DEVICE)
