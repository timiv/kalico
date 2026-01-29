/**
 * @file gpadc.c
 * @brief GPADC driver implementation for Allwinner R528/T113 HiFi4 DSP
 *
 * Copyright (C) 2025  James Turton <james.turton@gmx.com>
 * This file may be distributed under the terms of the GNU GPLv3 license.
 *
 * This implements the General Purpose ADC driver with interrupt support.
 * T113 has 1 channel, D1 has 2 channels, R329/T507 have 4 channels.
 */

#include "hal.h"

/*============================================================================
 * Private Variables
 *============================================================================*/

/*============================================================================
 * Private Functions
 *============================================================================*/

static inline void gpadc_write_reg(uint32_t offset, uint32_t value)
{
    REG32(GPADC_BASE + offset) = value;
}

static inline uint32_t gpadc_read_reg(uint32_t offset)
{
    return REG32(GPADC_BASE + offset);
}

/*============================================================================
 * GPADC Initialization
 *============================================================================*/

void gpadc_init(uint32_t sample_rate)
{
    uint32_t reg_val;

    /* Enable clock and deassert reset */
    ccu_gpadc_enable();

    /* Delay for clock to stabilize */
    for (volatile int i = 0; i < 10000; i++);

    /*
     * Configure sample rate
     * The sample rate is determined by: fs_div and tacq
     * Actual sample rate = HOSC / ((fs_div + 1) * (tacq + 1))
     * HOSC is typically 24MHz
     *
     * The default values used here are the initialization value of the
     * register:
     * - fs_div = 479 -> 50kHz sample rate
     * - tacq = 47 -> 2 µs acquire time.
     */

    uint32_t fs_div = 479;

    /* Adjust fs_div based on desired sample rate */
    if (sample_rate > 0 && sample_rate <= CLK_FREQ_HOSC) {
        /* Calculate: sample_rate = hosc_freq / (fs_div + 1)
         * fs_div = host_freq / sample_rate - 1*/
        fs_div = CLK_FREQ_HOSC / sample_rate;
    }

    reg_val = gpadc_read_reg(GPADC_SR_CON);
    reg_val &= ~GPADC_SR_CON_FS_DIV_MASK;
    reg_val |= (fs_div << 16);
    gpadc_write_reg(GPADC_SR_CON, reg_val);

    /* Disable all channels initially */
    gpadc_write_reg(GPADC_CS_EN, 0);

    /* Clear any pending interrupts */
    gpadc_write_reg(GPADC_FIFO_INTS, 0xFFFFFFFF);
    gpadc_write_reg(GPADC_DATAL_INTS, 0xFFFFFFFF);
    gpadc_write_reg(GPADC_DATAH_INTS, 0xFFFFFFFF);
    gpadc_write_reg(GPADC_DATA_INTS, 0xFFFFFFFF);

    /* Disable all interrupts initially */
    gpadc_write_reg(GPADC_FIFO_INTC, 0);
    gpadc_write_reg(GPADC_DATAL_INTC, 0);
    gpadc_write_reg(GPADC_DATAH_INTC, 0);
    gpadc_write_reg(GPADC_DATA_INTC, 0);

    /* Set continuous mode, enable calibration, and turn on ADC */

    /* Enable calibration */
    reg_val = gpadc_read_reg(GPADC_CTRL);
    reg_val |= GPADC_CTRL_ADC_CALI_EN;
    gpadc_write_reg(GPADC_CTRL, reg_val);

    /* Select continous mode */
    reg_val = gpadc_read_reg(GPADC_CTRL);
    reg_val &= ~GPADC_CTRL_WORK_MODE_MASK;
    reg_val |= GPADC_CTRL_WORK_MODE_CONT;
    gpadc_write_reg(GPADC_CTRL, reg_val);

    /* Enable ADC */
    reg_val = gpadc_read_reg(GPADC_CTRL);
    reg_val |= GPADC_CTRL_ADC_EN;
    gpadc_write_reg(GPADC_CTRL, reg_val);
}

/*============================================================================
 * Channel Enable/Disable
 *============================================================================*/

void gpadc_channel_enable(gpadc_channel_t channel)
{
    if (channel >= GPADC_MAX_CHANNELS) {
        return;
    }

    uint32_t cs_en = gpadc_read_reg(GPADC_CS_EN);
    cs_en |= GPADC_CS_EN_CH(channel);
    cs_en |= GPADC_CS_EN_CMP(channel);
    gpadc_write_reg(GPADC_CS_EN, cs_en);
}

void gpadc_channel_disable(gpadc_channel_t channel)
{
    if (channel >= GPADC_MAX_CHANNELS) {
        return;
    }

    uint32_t cs_en = gpadc_read_reg(GPADC_CS_EN);
    cs_en &= ~GPADC_CS_EN_CH(channel);
    cs_en &= ~GPADC_CS_EN_CMP(channel);
    gpadc_write_reg(GPADC_CS_EN, cs_en);
}

uint16_t gpadc_read_data(gpadc_channel_t channel)
{
    if (channel >= GPADC_MAX_CHANNELS) {
        return 0;
    }

    uint32_t data = gpadc_read_reg(GPADC_CH0_DATA + channel * 4);
    return (uint16_t)(data & GPADC_CH0_DATA_MASK);
}
