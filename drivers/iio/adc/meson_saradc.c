/*
 * Amlogic Meson Successive Approximation Register (SAR) A/D Converter
 *
 * Copyright (C) 2016 Martin Blumenstingl <martin.blumenstingl@googlemail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/bitfield.h>
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/reset.h>
#include <linux/regmap.h>
#include <linux/iio/iio.h>

#define SAR_ADC_REG0						0x00
	#define SAR_ADC_REG0_PANEL_DETECT			BIT(31)
	#define SAR_ADC_REG0_BUSY_MASK				GENMASK(30, 28)
	#define SAR_ADC_REG0_DELTA_BUSY				BIT(30)
	#define SAR_ADC_REG0_AVG_BUSY				BIT(29)
	#define SAR_ADC_REG0_SAMPLE_BUSY			BIT(28)
	#define SAR_ADC_REG0_FIFO_FULL				BIT(27)
	#define SAR_ADC_REG0_FIFO_EMPTY				BIT(26)
	#define SAR_ADC_REG0_FIFO_COUNT_MASK			GENMASK(25, 21)
	#define SAR_ADC_REG0_ADC_BIAS_CTRL_MASK			GENMASK(20, 19)
	#define SAR_ADC_REG0_CURR_CHAN_ID_MASK			GENMASK(18, 16)
	#define SAR_ADC_REG0_ADC_TEMP_SEN_SEL			BIT(15)
	#define SAR_ADC_REG0_SAMPLING_STOP			BIT(14)
	#define SAR_ADC_REG0_CHAN_DELTA_EN_MASK			GENMASK(13, 12)
	#define SAR_ADC_REG0_DETECT_IRQ_POL			BIT(10)
	#define SAR_ADC_REG0_DETECT_IRQ_EN			BIT(9)
	#define SAR_ADC_REG0_FIFO_CNT_IRQ_MASK			GENMASK(8, 4)
	#define SAR_ADC_REG0_FIFO_IRQ_EN			BIT(3)
	#define SAR_ADC_REG0_SAMPLING_START			BIT(2)
	#define SAR_ADC_REG0_CONTINUOUS_EN			BIT(1)
	#define SAR_ADC_REG0_SAMPLE_ENGINE_ENABLE		BIT(0)

#define SAR_ADC_CHAN_LIST					0x04
	#define SAR_ADC_CHAN_LIST_MAX_INDEX_MASK		GENMASK(26, 24)
	#define SAR_ADC_CHAN_CHAN_ENTRY_MASK(_chan)		\
					(GENMASK(2, 0) << (_chan * 3))

#define SAR_ADC_AVG_CNTL					0x08
	#define SAR_ADC_AVG_CNTL_AVG_MODE_SHIFT(_chan)		\
					(16 + (_chan * 2))
	#define SAR_ADC_AVG_CNTL_AVG_MODE_MASK(_chan)		\
					(GENMASK(17, 16) << (_chan * 2))
	#define SAR_ADC_AVG_CNTL_NUM_SAMPLES_SHIFT(_chan)	\
					(0 + (_chan * 2))
	#define SAR_ADC_AVG_CNTL_NUM_SAMPLES_MASK(_chan)	\
					(GENMASK(1, 0) << (_chan * 2))

#define SAR_ADC_REG3						0x0c
	#define SAR_ADC_REG3_CNTL_USE_SC_DLY			BIT(31)
	#define SAR_ADC_REG3_CLK_EN				BIT(30)
	#define SAR_ADC_REG3_BL30_INITIALIZED			BIT(28)
	#define SAR_ADC_REG3_CTRL_CONT_RING_COUNTER_EN		BIT(27)
	#define SAR_ADC_REG3_CTRL_SAMPLING_CLOCK_PHASE		BIT(26)
	#define SAR_ADC_REG3_CTRL_CHAN7_MUX_SEL_MASK		GENMASK(25, 23)
	#define SAR_ADC_REG3_DETECT_EN				BIT(22)
	#define SAR_ADC_REG3_ADC_EN				BIT(21)
	#define SAR_ADC_REG3_PANEL_DETECT_COUNT_MASK		GENMASK(20, 18)
	#define SAR_ADC_REG3_PANEL_DETECT_FILTER_TB_MASK	GENMASK(17, 16)
	#define SAR_ADC_REG3_ADC_CLK_DIV_SHIFT			10
	#define SAR_ADC_REG3_ADC_CLK_DIV_WIDTH			5
	#define SAR_ADC_REG3_ADC_CLK_DIV_MASK			GENMASK(15, 10)
	#define SAR_ADC_REG3_BLOCK_DLY_SEL_MASK			GENMASK(9, 8)
	#define SAR_ADC_REG3_BLOCK_DLY_MASK			GENMASK(7, 0)

#define SAR_ADC_DELAY						0x10
	#define SAR_ADC_DELAY_INPUT_DLY_SEL_MASK		GENMASK(25, 24)
	#define SAR_ADC_DELAY_BL30_BUSY				BIT(15)
	#define SAR_ADC_DELAY_KERNEL_BUSY			BIT(14)
	#define SAR_ADC_DELAY_INPUT_DLY_CNT_MASK		GENMASK(23, 16)
	#define SAR_ADC_DELAY_SAMPLE_DLY_SEL_MASK		GENMASK(9, 8)
	#define SAR_ADC_DELAY_SAMPLE_DLY_CNT_MASK		GENMASK(7, 0)

#define SAR_ADC_LAST_RD						0x14
	#define SAR_ADC_LAST_RD_LAST_CHANNEL1_MASK		GENMASK(23, 16)
	#define SAR_ADC_LAST_RD_LAST_CHANNEL0_MASK		GENMASK(9, 0)

#define SAR_ADC_FIFO_RD						0x18
	#define SAR_ADC_FIFO_RD_CHAN_ID_MASK			GENMASK(14, 12)
	#define SAR_ADC_FIFO_RD_SAMPLE_VALUE_MASK		GENMASK(11, 0)

#define SAR_ADC_AUX_SW						0x1c
	#define SAR_ADC_AUX_SW_MUX_SEL_CHAN_MASK(_chan)		\
					(GENMASK(10, 8) << ((_chan - 2) * 2))
	#define SAR_ADC_AUX_SW_VREF_P_MUX			BIT(6)
	#define SAR_ADC_AUX_SW_VREF_N_MUX			BIT(5)
	#define SAR_ADC_AUX_SW_MODE_SEL				BIT(4)
	#define SAR_ADC_AUX_SW_YP_DRIVE_SW			BIT(3)
	#define SAR_ADC_AUX_SW_XP_DRIVE_SW			BIT(2)
	#define SAR_ADC_AUX_SW_YM_DRIVE_SW			BIT(1)
	#define SAR_ADC_AUX_SW_XM_DRIVE_SW			BIT(0)

#define SAR_ADC_CHAN_10_SW					0x20
	#define SAR_ADC_CHAN_10_SW_CHAN1_MUX_SEL_MASK		GENMASK(25, 23)
	#define SAR_ADC_CHAN_10_SW_CHAN1_VREF_P_MUX		BIT(22)
	#define SAR_ADC_CHAN_10_SW_CHAN1_VREF_N_MUX		BIT(21)
	#define SAR_ADC_CHAN_10_SW_CHAN1_MODE_SEL		BIT(20)
	#define SAR_ADC_CHAN_10_SW_CHAN1_YP_DRIVE_SW		BIT(19)
	#define SAR_ADC_CHAN_10_SW_CHAN1_XP_DRIVE_SW		BIT(18)
	#define SAR_ADC_CHAN_10_SW_CHAN1_YM_DRIVE_SW		BIT(17)
	#define SAR_ADC_CHAN_10_SW_CHAN1_XM_DRIVE_SW		BIT(16)
	#define SAR_ADC_CHAN_10_SW_CHAN0_MUX_SEL_MASK		GENMASK(9, 7)
	#define SAR_ADC_CHAN_10_SW_CHAN0_VREF_P_MUX		BIT(6)
	#define SAR_ADC_CHAN_10_SW_CHAN0_VREF_N_MUX		BIT(5)
	#define SAR_ADC_CHAN_10_SW_CHAN0_MODE_SEL		BIT(4)
	#define SAR_ADC_CHAN_10_SW_CHAN0_YP_DRIVE_SW		BIT(3)
	#define SAR_ADC_CHAN_10_SW_CHAN0_XP_DRIVE_SW		BIT(2)
	#define SAR_ADC_CHAN_10_SW_CHAN0_YM_DRIVE_SW		BIT(1)
	#define SAR_ADC_CHAN_10_SW_CHAN0_XM_DRIVE_SW		BIT(0)

#define SAR_ADC_DETECT_IDLE_SW					0x24
	#define SAR_ADC_DETECT_IDLE_SW_DETECT_SW_EN		BIT(26)
	#define SAR_ADC_DETECT_IDLE_SW_DETECT_MODE_MUX_MASK	GENMASK(25, 23)
	#define SAR_ADC_DETECT_IDLE_SW_DETECT_MODE_VREF_P_MUX	BIT(22)
	#define SAR_ADC_DETECT_IDLE_SW_DETECT_MODE_VREF_N_MUX	BIT(21)
	#define SAR_ADC_DETECT_IDLE_SW_DETECT_MODE_SEL		BIT(20)
	#define SAR_ADC_DETECT_IDLE_SW_DETECT_MODE_YP_DRIVE_SW	BIT(19)
	#define SAR_ADC_DETECT_IDLE_SW_DETECT_MODE_XP_DRIVE_SW	BIT(18)
	#define SAR_ADC_DETECT_IDLE_SW_DETECT_MODE_YM_DRIVE_SW	BIT(17)
	#define SAR_ADC_DETECT_IDLE_SW_DETECT_MODE_XM_DRIVE_SW	BIT(16)
	#define SAR_ADC_DETECT_IDLE_SW_IDLE_MODE_MUX_SEL_MASK	GENMASK(9, 7)
	#define SAR_ADC_DETECT_IDLE_SW_IDLE_MODE_VREF_P_MUX	BIT(6)
	#define SAR_ADC_DETECT_IDLE_SW_IDLE_MODE_VREF_N_MUX	BIT(5)
	#define SAR_ADC_DETECT_IDLE_SW_IDLE_MODE_SEL		BIT(4)
	#define SAR_ADC_DETECT_IDLE_SW_IDLE_MODE_YP_DRIVE_SW	BIT(3)
	#define SAR_ADC_DETECT_IDLE_SW_IDLE_MODE_XP_DRIVE_SW	BIT(2)
	#define SAR_ADC_DETECT_IDLE_SW_IDLE_MODE_YM_DRIVE_SW	BIT(1)
	#define SAR_ADC_DETECT_IDLE_SW_IDLE_MODE_XM_DRIVE_SW	BIT(0)

#define SAR_ADC_DELTA_10					0x28
	#define SAR_ADC_DELTA_10_TEMP_SEL			BIT(27)
	#define SAR_ADC_DELTA_10_TS_REVE1			BIT(26)
	#define SAR_ADC_DELTA_10_CHAN1_DELTA_VALUE_SHIFT	16
	#define SAR_ADC_DELTA_10_CHAN1_DELTA_VALUE_MASK		GENMASK(25, 16)
	#define SAR_ADC_DELTA_10_TS_REVE0			BIT(15)
	#define SAR_ADC_DELTA_10_TS_C_SHIFT			11
	#define SAR_ADC_DELTA_10_TS_C_MASK			GENMASK(14, 11)
	#define SAR_ADC_DELTA_10_TS_VBG_EN			BIT(10)
	#define SAR_ADC_DELTA_10_CHAN0_DELTA_VALUE_SHIFT	0
	#define SAR_ADC_DELTA_10_CHAN0_DELTA_VALUE_MASK		GENMASK(9, 0)

/* NOTE: registers from here are undocumented (the vendor Linux kernel driver
 * and u-boot source served as reference). These only seem to be relevant on
 * GXBB and newer.
 */
#define SAR_ADC_REG11						0x2c
	#define SAR_ADC_REG11_BANDGAP_EN			BIT(13)

#define SAR_ADC_REG13						0x34
	#define SAR_ADC_REG13_UNKNOWN_CALIBRATION_MASK		GENMASK(13, 8)

#define SAR_ADC_MAX_FIFO_SIZE		32
#define SAR_ADC_NUM_CHANNELS		ARRAY_SIZE(meson_saradc_iio_channels)
#define SAR_ADC_VALUE_MASK(_priv)	(BIT(_priv->resolution) - 1)

#define MESON_SAR_ADC_CHAN(_chan, _type) {				\
	.type = _type,							\
	.indexed = true,						\
	.channel = _chan,						\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |			\
				BIT(IIO_CHAN_INFO_AVERAGE_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),		\
	.datasheet_name = "SAR_ADC_CH"#_chan,				\
}

/* the hardware supports IIO_VOLTAGE for channel 6 as well. we can ignore that
 * since it is always connected to the temperature sensor within the chip
 * itself and there are enough other usable channels left.
 */
static const struct iio_chan_spec meson_saradc_iio_channels[] = {
	MESON_SAR_ADC_CHAN(0, IIO_VOLTAGE),
	MESON_SAR_ADC_CHAN(1, IIO_VOLTAGE),
	MESON_SAR_ADC_CHAN(2, IIO_VOLTAGE),
	MESON_SAR_ADC_CHAN(3, IIO_VOLTAGE),
	MESON_SAR_ADC_CHAN(4, IIO_VOLTAGE),
	MESON_SAR_ADC_CHAN(5, IIO_VOLTAGE),
	MESON_SAR_ADC_CHAN(6, IIO_TEMP),
	MESON_SAR_ADC_CHAN(7, IIO_VOLTAGE),
	IIO_CHAN_SOFT_TIMESTAMP(8),
};

enum meson_saradc_avg_mode {
	NO_AVERAGING = 0x0,
	MEAN_AVERAGING = 0x1,
	MEDIAN_AVERAGING = 0x2,
};

enum meson_saradc_num_samples {
	ONE_SAMPLE = 0x0,
	TWO_SAMPLES = 0x1,
	FOUR_SAMPLES = 0x2,
	EIGHT_SAMPLES = 0x3,
};

enum meson_saradc_chan7_mux_sel {
	CHAN7_MUX_VSS = 0x0,
	CHAN7_MUX_VDD_DIV4 = 0x1,
	CHAN7_MUX_VDD_DIV2 = 0x2,
	CHAN7_MUX_VDD_MUL3_DIV4 = 0x3,
	CHAN7_MUX_VDD = 0x4,
	CHAN7_MUX_CH7_INPUT = 0x7,
};

struct meson_saradc_priv {
	struct regmap			*regmap;
	struct clk			*clkin;
	struct clk			*core_clk;
	struct clk			*sana_clk;
	struct clk			*adc_sel_clk;
	struct clk			*adc_clk;
	struct clk_gate			clk_gate;
	struct clk			*adc_div_clk;
	struct clk_divider		clk_div;
	struct completion		completion;
	u8				resolution;
	bool				bl30_managed;
};

static const struct regmap_config meson_saradc_regmap_config = {
	.reg_bits = 8,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = SAR_ADC_REG13,
};

static unsigned int meson_saradc_get_fifo_count(struct iio_dev *indio_dev)
{
	struct meson_saradc_priv *priv = iio_priv(indio_dev);
	u32 regval;

	regmap_read(priv->regmap, SAR_ADC_REG0, &regval);

	return FIELD_GET(SAR_ADC_REG0_FIFO_COUNT_MASK, regval);
}

static int meson_saradc_wait_busy_clear(struct iio_dev *indio_dev)
{
	struct meson_saradc_priv *priv = iio_priv(indio_dev);
	int regval, timeout = 10000;

	do {
		udelay(1);
		regmap_read(priv->regmap, SAR_ADC_REG0, &regval);
	} while (FIELD_GET(SAR_ADC_REG0_BUSY_MASK, regval) && timeout--);

	if (timeout < 0)
		return -ETIMEDOUT;

	return 0;
}

static int meson_saradc_read_raw_sample(struct iio_dev *indio_dev,
					const struct iio_chan_spec *chan,
					int *val)
{
	struct meson_saradc_priv *priv = iio_priv(indio_dev);
	int ret, regval, fifo_chan, fifo_val, sum = 0, count = 0;

	ret = meson_saradc_wait_busy_clear(indio_dev);
	if (ret)
		return ret;

	regmap_read(priv->regmap, SAR_ADC_REG0, &regval);

	while (meson_saradc_get_fifo_count(indio_dev) > 0 &&
	       count < SAR_ADC_MAX_FIFO_SIZE) {
		regmap_read(priv->regmap, SAR_ADC_FIFO_RD, &regval);

		fifo_chan = FIELD_GET(SAR_ADC_FIFO_RD_CHAN_ID_MASK, regval);
		if (fifo_chan == chan->channel) {
			fifo_val = FIELD_GET(SAR_ADC_FIFO_RD_SAMPLE_VALUE_MASK,
					     regval) & SAR_ADC_VALUE_MASK(priv);
			sum += fifo_val;
			count++;
		}
	}

	if (!count)
		return -ENOENT;

	*val = sum / count;

	return 0;
}

static void meson_saradc_set_averaging(struct iio_dev *indio_dev,
				       const struct iio_chan_spec *chan,
				       enum meson_saradc_avg_mode mode,
				       enum meson_saradc_num_samples samples)
{
	struct meson_saradc_priv *priv = iio_priv(indio_dev);
	u32 val;

	val = samples << SAR_ADC_AVG_CNTL_NUM_SAMPLES_SHIFT(chan->channel);
	regmap_update_bits(priv->regmap, SAR_ADC_AVG_CNTL,
			   SAR_ADC_AVG_CNTL_NUM_SAMPLES_MASK(chan->channel),
			   val);

	val = mode << SAR_ADC_AVG_CNTL_AVG_MODE_SHIFT(chan->channel);
	regmap_update_bits(priv->regmap, SAR_ADC_AVG_CNTL,
			   SAR_ADC_AVG_CNTL_AVG_MODE_MASK(chan->channel), val);
}

static void meson_saradc_enable_channel(struct iio_dev *indio_dev,
					const struct iio_chan_spec *chan)
{
	struct meson_saradc_priv *priv = iio_priv(indio_dev);
	u32 regval;

	/* the SAR ADC engine allows sampling multiple channels at the same
	 * time. to keep it simple we're only working with one *internal*
	 * channel, which starts counting at index 0 (which means: count = 1).
	 */
	regval = FIELD_PREP(SAR_ADC_CHAN_LIST_MAX_INDEX_MASK, 0);
	regmap_update_bits(priv->regmap, SAR_ADC_CHAN_LIST,
			   SAR_ADC_CHAN_LIST_MAX_INDEX_MASK, regval);

	/* map channel index 0 to the channel which we want to read */
	regval = FIELD_PREP(SAR_ADC_CHAN_CHAN_ENTRY_MASK(0), chan->channel);
	regmap_update_bits(priv->regmap, SAR_ADC_CHAN_LIST,
			   SAR_ADC_CHAN_CHAN_ENTRY_MASK(0), regval);

	regval = FIELD_PREP(SAR_ADC_DETECT_IDLE_SW_DETECT_MODE_MUX_MASK,
			    chan->channel);
	regmap_update_bits(priv->regmap, SAR_ADC_DETECT_IDLE_SW,
			   SAR_ADC_DETECT_IDLE_SW_DETECT_MODE_MUX_MASK,
			   regval);

	regval = FIELD_PREP(SAR_ADC_DETECT_IDLE_SW_IDLE_MODE_MUX_SEL_MASK,
			    chan->channel);
	regmap_update_bits(priv->regmap, SAR_ADC_DETECT_IDLE_SW,
			   SAR_ADC_DETECT_IDLE_SW_IDLE_MODE_MUX_SEL_MASK,
			   regval);
}

static void meson_saradc_set_channel7_mux(struct iio_dev *indio_dev,
					  enum meson_saradc_chan7_mux_sel sel)
{
	struct meson_saradc_priv *priv = iio_priv(indio_dev);
	u32 regval;

	regval = FIELD_PREP(SAR_ADC_REG3_CTRL_CHAN7_MUX_SEL_MASK, sel);
	regmap_update_bits(priv->regmap, SAR_ADC_REG3,
			   SAR_ADC_REG3_CTRL_CHAN7_MUX_SEL_MASK, regval);

	usleep_range(10, 20);
}

static void meson_saradc_start_sample_engine(struct iio_dev *indio_dev)
{
	struct meson_saradc_priv *priv = iio_priv(indio_dev);

	regmap_update_bits(priv->regmap, SAR_ADC_REG0,
			   SAR_ADC_REG0_SAMPLE_ENGINE_ENABLE,
			   SAR_ADC_REG0_SAMPLE_ENGINE_ENABLE);

	regmap_update_bits(priv->regmap, SAR_ADC_REG0,
			   SAR_ADC_REG0_SAMPLING_START,
			   SAR_ADC_REG0_SAMPLING_START);
}

static void meson_saradc_stop_sample_engine(struct iio_dev *indio_dev)
{
	struct meson_saradc_priv *priv = iio_priv(indio_dev);

	regmap_update_bits(priv->regmap, SAR_ADC_REG0,
			   SAR_ADC_REG0_SAMPLING_STOP,
			   SAR_ADC_REG0_SAMPLING_STOP);

	/* wait until all modules are stopped */
	meson_saradc_wait_busy_clear(indio_dev);

	regmap_update_bits(priv->regmap, SAR_ADC_REG0,
			   SAR_ADC_REG0_SAMPLE_ENGINE_ENABLE, 0);
}

static void meson_saradc_lock(struct iio_dev *indio_dev)
{
	struct meson_saradc_priv *priv = iio_priv(indio_dev);
	int val;

	mutex_lock(&indio_dev->mlock);

	/* prevent BL30 from using the SAR ADC while we are using it */
	regmap_update_bits(priv->regmap, SAR_ADC_DELAY,
			   SAR_ADC_DELAY_KERNEL_BUSY,
			   SAR_ADC_DELAY_KERNEL_BUSY);

	/* wait until BL30 releases it's lock (so we can use the SAR ADC) */
	do {
		udelay(1);
		regmap_read(priv->regmap, SAR_ADC_DELAY, &val);
	} while (val & SAR_ADC_DELAY_BL30_BUSY);
}

static void meson_saradc_unlock(struct iio_dev *indio_dev)
{
	struct meson_saradc_priv *priv = iio_priv(indio_dev);

	/* allow BL30 to use the SAR ADC again */
	regmap_update_bits(priv->regmap, SAR_ADC_DELAY,
			   SAR_ADC_DELAY_KERNEL_BUSY, 0);

	mutex_unlock(&indio_dev->mlock);
}

static int meson_saradc_get_sample(struct iio_dev *indio_dev,
				   const struct iio_chan_spec *chan,
				   enum meson_saradc_avg_mode avg_mode,
				   enum meson_saradc_num_samples avg_samples,
				   int *val)
{
	int ret, tmp;

	meson_saradc_lock(indio_dev);

	/* clear old values from the FIFO buffer, ignoring errors */
	meson_saradc_read_raw_sample(indio_dev, chan, &tmp);

	meson_saradc_set_averaging(indio_dev, chan, avg_mode, avg_samples);

	meson_saradc_enable_channel(indio_dev, chan);

	meson_saradc_start_sample_engine(indio_dev);
	ret = meson_saradc_read_raw_sample(indio_dev, chan, val);
	meson_saradc_stop_sample_engine(indio_dev);

	meson_saradc_unlock(indio_dev);

	if (ret) {
		dev_warn(&indio_dev->dev,
			 "failed to read sample for channel %d: %d\n",
			 chan->channel, ret);
		return ret;
	}

	return IIO_VAL_INT;
}

static int meson_saradc_iio_info_read_raw(struct iio_dev *indio_dev,
					  const struct iio_chan_spec *chan,
					  int *val, int *val2, long mask)
{
	struct meson_saradc_priv *priv = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		return meson_saradc_get_sample(indio_dev, chan, NO_AVERAGING,
					       ONE_SAMPLE, val);
		break;

	case IIO_CHAN_INFO_AVERAGE_RAW:
		return meson_saradc_get_sample(indio_dev, chan, MEAN_AVERAGING,
					       EIGHT_SAMPLES, val);
		break;

	case IIO_CHAN_INFO_SCALE:
		*val = 1800;
		*val2 = priv->resolution;
		return IIO_VAL_FRACTIONAL_LOG2;

	default:
		return -EINVAL;
	}
}

static irqreturn_t meson_saradc_isr(int irq, void *data)
{
	struct iio_dev *indio_dev = data;
	struct meson_saradc_priv *priv = iio_priv(indio_dev);

	complete(&priv->completion);

	return IRQ_HANDLED;
}

static int meson_saradc_clk_init(struct iio_dev *indio_dev, void __iomem *base)
{
	struct meson_saradc_priv *priv = iio_priv(indio_dev);
	struct clk_init_data init;
	char clk_name[32];
	const char *clk_div_parents[1];

	snprintf(clk_name, sizeof(clk_name), "%s#adc_div",
		 of_node_full_name(indio_dev->dev.of_node));
	init.name = devm_kstrdup(&indio_dev->dev, clk_name, GFP_KERNEL);
	init.flags = 0;
	init.ops = &clk_divider_ops;
	clk_div_parents[0] = __clk_get_name(priv->clkin);
	init.parent_names = clk_div_parents;
	init.num_parents = ARRAY_SIZE(clk_div_parents);

	priv->clk_div.reg = base + SAR_ADC_REG3;
	priv->clk_div.shift = SAR_ADC_REG3_ADC_CLK_DIV_SHIFT;
	priv->clk_div.width = SAR_ADC_REG3_ADC_CLK_DIV_WIDTH;
	priv->clk_div.hw.init = &init;
	priv->clk_div.flags = 0;

	priv->adc_div_clk = devm_clk_register(&indio_dev->dev,
					      &priv->clk_div.hw);
	if (WARN_ON(IS_ERR(priv->adc_div_clk)))
		return PTR_ERR(priv->adc_div_clk);

	snprintf(clk_name, sizeof(clk_name), "%s#adc_en",
		 of_node_full_name(indio_dev->dev.of_node));
	init.name = devm_kstrdup(&indio_dev->dev, clk_name, GFP_KERNEL);
	init.flags = 0;
	init.ops = &clk_gate_ops;
	init.parent_names = NULL;
	init.num_parents = 0;

	priv->clk_gate.reg = base + SAR_ADC_REG3;
	priv->clk_gate.bit_idx = fls(SAR_ADC_REG3_CLK_EN);
	priv->clk_gate.hw.init = &init;

	priv->adc_clk = devm_clk_register(&indio_dev->dev, &priv->clk_gate.hw);
	if (WARN_ON(IS_ERR(priv->adc_clk)))
		return PTR_ERR(priv->adc_clk);

	return 0;
}

static int meson_saradc_init(struct iio_dev *indio_dev)
{
	struct meson_saradc_priv *priv = iio_priv(indio_dev);
	int regval, ret;

	/* make sure we start at CH7 input */
	meson_saradc_set_channel7_mux(indio_dev, CHAN7_MUX_CH7_INPUT);

	regmap_read(priv->regmap, SAR_ADC_REG3, &regval);

	priv->bl30_managed = !!(regval & SAR_ADC_REG3_BL30_INITIALIZED);
	if (priv->bl30_managed) {
		dev_info(&indio_dev->dev, "already initialized by BL30\n");
		return 0;
	}

	dev_info(&indio_dev->dev, "initializing SAR ADC\n");

	meson_saradc_stop_sample_engine(indio_dev);

	/* update the channel 6 MUX to select the temperature sensor */
	regmap_update_bits(priv->regmap, SAR_ADC_REG0,
			SAR_ADC_REG0_ADC_TEMP_SEN_SEL,
			SAR_ADC_REG0_ADC_TEMP_SEN_SEL);

	/* disable all channels by default */
	regmap_write(priv->regmap, SAR_ADC_CHAN_LIST, 0x0);

	regmap_update_bits(priv->regmap, SAR_ADC_REG3,
			   SAR_ADC_REG3_CTRL_SAMPLING_CLOCK_PHASE, 0);
	regmap_update_bits(priv->regmap, SAR_ADC_REG3,
			   SAR_ADC_REG3_CNTL_USE_SC_DLY,
			   SAR_ADC_REG3_CNTL_USE_SC_DLY);

	/* delay between two samples = (10+1) * 1uS */
	regmap_update_bits(priv->regmap, SAR_ADC_DELAY,
			   SAR_ADC_DELAY_INPUT_DLY_CNT_MASK,
			   FIELD_PREP(SAR_ADC_DELAY_SAMPLE_DLY_CNT_MASK, 10));
	regmap_update_bits(priv->regmap, SAR_ADC_DELAY,
			   SAR_ADC_DELAY_SAMPLE_DLY_SEL_MASK,
			   FIELD_PREP(SAR_ADC_DELAY_SAMPLE_DLY_SEL_MASK, 0));

	/* delay between two samples = (10+1) * 1uS */
	regmap_update_bits(priv->regmap, SAR_ADC_DELAY,
			   SAR_ADC_DELAY_INPUT_DLY_CNT_MASK,
			   FIELD_PREP(SAR_ADC_DELAY_INPUT_DLY_CNT_MASK, 10));
	regmap_update_bits(priv->regmap, SAR_ADC_DELAY,
			   SAR_ADC_DELAY_INPUT_DLY_SEL_MASK,
			   FIELD_PREP(SAR_ADC_DELAY_INPUT_DLY_SEL_MASK, 1));

	ret = clk_prepare_enable(priv->adc_sel_clk);
	if (ret) {
		dev_err(&indio_dev->dev, "failed to enable adc clk\n");
		return ret;
	}

	ret = clk_set_parent(priv->adc_sel_clk, priv->clkin);
	if (ret) {
		dev_err(&indio_dev->dev,
			"failed to set adc parent to clkin\n");
		goto err_sel;
	}

	ret = clk_prepare_enable(priv->adc_div_clk);
	if (ret) {
		dev_err(&indio_dev->dev, "failed to enable adc clk\n");
		goto err_sel;
	}

	ret = clk_set_rate(priv->adc_div_clk, 1200000);
	if (ret) {
		dev_err(&indio_dev->dev, "failed to set adc clock rate\n");
		goto err_div;
	}

err_div:
	clk_disable_unprepare(priv->adc_div_clk);
err_sel:
	clk_disable_unprepare(priv->adc_sel_clk);
	return ret;
}

static int meson_saradc_hw_enable(struct iio_dev *indio_dev)
{
	struct meson_saradc_priv *priv = iio_priv(indio_dev);
	int ret;

	ret = clk_prepare_enable(priv->core_clk);
	if (ret) {
		dev_err(&indio_dev->dev, "failed to enable core clk\n");
		return ret;
	}

	ret = clk_prepare_enable(priv->sana_clk);
	if (ret) {
		clk_disable_unprepare(priv->core_clk);
		dev_err(&indio_dev->dev, "failed to enable sana clk\n");
		return ret;
	}

	regmap_update_bits(priv->regmap, SAR_ADC_REG11,
			   SAR_ADC_REG11_BANDGAP_EN, SAR_ADC_REG11_BANDGAP_EN);
	regmap_update_bits(priv->regmap, SAR_ADC_REG3, SAR_ADC_REG3_ADC_EN,
			   SAR_ADC_REG3_ADC_EN);
	udelay(5);
	regmap_update_bits(priv->regmap, SAR_ADC_REG3, SAR_ADC_REG3_CLK_EN, // FIXME: should not be needed
			   SAR_ADC_REG3_CLK_EN);

	ret = clk_prepare_enable(priv->adc_clk);
	if (ret) {
		clk_disable_unprepare(priv->sana_clk);
		clk_disable_unprepare(priv->core_clk);
		dev_err(&indio_dev->dev, "failed to enable adc_en clk\n");
		return ret;
	}

	return 0;
}

static void meson_saradc_hw_disable(struct iio_dev *indio_dev)
{
	struct meson_saradc_priv *priv = iio_priv(indio_dev);

	clk_disable_unprepare(priv->adc_clk);

	regmap_update_bits(priv->regmap, SAR_ADC_REG3, SAR_ADC_REG3_CLK_EN, 0); // FIXME: should not be needed
	regmap_update_bits(priv->regmap, SAR_ADC_REG3, SAR_ADC_REG3_ADC_EN, 0);
	regmap_update_bits(priv->regmap, SAR_ADC_REG11,
			   SAR_ADC_REG11_BANDGAP_EN, 0);

	clk_disable_unprepare(priv->sana_clk);
	clk_disable_unprepare(priv->core_clk);
}

static const struct iio_info meson_saradc_iio_info = {
	.read_raw = meson_saradc_iio_info_read_raw,
	.driver_module = THIS_MODULE,
};

static const struct of_device_id meson_saradc_of_match[] = {
	{
		.compatible = "amlogic,meson8b-saradc",
		.data = (void *)10,
	}, {
		.compatible = "amlogic,meson-gxbb-saradc",
		.data = (void *)10,
	}, {
		.compatible = "amlogic,meson-gxl-saradc",
		.data = (void *)12,
	},
	{},
};
MODULE_DEVICE_TABLE(of, meson_saradc_of_match);

static int meson_saradc_probe(struct platform_device *pdev)
{
	struct meson_saradc_priv *priv;
	struct iio_dev *indio_dev;
	struct resource *res;
	void __iomem *base;
	const struct of_device_id *match;
	int ret;
	int irq;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*priv));
	if (!indio_dev) {
		dev_err(&pdev->dev, "failed allocating iio device\n");
		return -ENOMEM;
	}

	priv = iio_priv(indio_dev);

	match = of_match_device(meson_saradc_of_match, &pdev->dev);
	priv->resolution = (unsigned long)match->data;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	priv->regmap = devm_regmap_init_mmio(&pdev->dev, base,
					     &meson_saradc_regmap_config);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	init_completion(&priv->completion);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "no irq resource?\n");
		return irq;
	}

	ret = devm_request_irq(&pdev->dev, irq, meson_saradc_isr, 0,
			       dev_name(&pdev->dev), indio_dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed requesting irq %d\n", irq);
		return ret;
	}

	priv->clkin = devm_clk_get(&pdev->dev, "clkin");
	if (IS_ERR(priv->clkin)) {
		dev_err(&pdev->dev, "failed to get clkin\n");
		return PTR_ERR(priv->clkin);
	}

	priv->core_clk = devm_clk_get(&pdev->dev, "core");
	if (IS_ERR(priv->core_clk)) {
		dev_err(&pdev->dev, "failed to get core clk\n");
		return PTR_ERR(priv->core_clk);
	}

	priv->sana_clk = devm_clk_get(&pdev->dev, "sana");
	if (IS_ERR(priv->sana_clk)) {
		if (PTR_ERR(priv->sana_clk) == -ENOENT) {
			priv->sana_clk = NULL;
		} else {
			dev_err(&pdev->dev, "failed to get sana clk\n");
			return PTR_ERR(priv->sana_clk);
		}
	}

	priv->adc_clk = devm_clk_get(&pdev->dev, "adc_clk");
	if (IS_ERR(priv->adc_clk)) {
		if (PTR_ERR(priv->adc_clk) == -ENOENT) {
			priv->adc_clk = NULL;
		} else {
			dev_err(&pdev->dev, "failed to get adc clk\n");
			return PTR_ERR(priv->adc_clk);
		}
	}

	priv->adc_div_clk = devm_clk_get(&pdev->dev, "adc_div");
	if (IS_ERR(priv->adc_div_clk)) {
		if (PTR_ERR(priv->adc_div_clk) == -ENOENT) {
			priv->adc_div_clk = NULL;
		} else {
			dev_err(&pdev->dev, "failed to get adc_div clk\n");
			return PTR_ERR(priv->adc_div_clk);
		}
	}

	priv->adc_sel_clk = devm_clk_get(&pdev->dev, "adc_sel");
	if (IS_ERR(priv->adc_sel_clk)) {
		if (PTR_ERR(priv->adc_sel_clk) == -ENOENT) {
			priv->adc_sel_clk = NULL;
		} else {
			dev_err(&pdev->dev, "failed to get adc_sel clk\n");
			return PTR_ERR(priv->adc_sel_clk);
		}
	}

	/* on pre-GXBB SoCs the SAR ADC itself provides these clocks: */
	if (!priv->adc_clk && !priv->adc_div_clk) {
		ret = meson_saradc_clk_init(indio_dev, base);
		if (ret)
			return ret;
	}

	ret = meson_saradc_init(indio_dev);
	if (ret)
		goto err;

	ret = meson_saradc_hw_enable(indio_dev);
	if (ret)
		goto err_init;

	platform_set_drvdata(pdev, indio_dev);

	indio_dev->name = dev_name(&pdev->dev);
	indio_dev->dev.parent = &pdev->dev;
	indio_dev->dev.of_node = pdev->dev.of_node;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &meson_saradc_iio_info;

	indio_dev->channels = meson_saradc_iio_channels;
	indio_dev->num_channels = SAR_ADC_NUM_CHANNELS;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto err_hw;

	return 0;

err_hw:
	meson_saradc_hw_disable(indio_dev);
err_init:
	if (!priv->bl30_managed) {
		clk_disable_unprepare(priv->adc_div_clk);
		clk_disable_unprepare(priv->adc_sel_clk);
	}
err:
	return ret;
}

static int meson_saradc_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct meson_saradc_priv *priv = iio_priv(indio_dev);

	meson_saradc_hw_disable(indio_dev);
	iio_device_unregister(indio_dev);

	if (!priv->bl30_managed) {
		clk_disable_unprepare(priv->adc_div_clk);
		clk_disable_unprepare(priv->adc_sel_clk);
	}

	return 0;
}

static struct platform_driver meson_saradc_driver = {
	.probe		= meson_saradc_probe,
	.remove		= meson_saradc_remove,
	.driver		= {
		.name	= "meson-saradc",
		.of_match_table = meson_saradc_of_match,
	},
};

module_platform_driver(meson_saradc_driver);

MODULE_AUTHOR("Martin Blumenstingl <martin.blumenstingl@googlemail.com>");
MODULE_DESCRIPTION("Amlogic Meson SAR ADC driver");
MODULE_LICENSE("GPL v2");
