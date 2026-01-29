// GPIO functions on hifi4.
//
// Copyright (C) 2025  James Turton <james.turton@gmx.com>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "gpio.h" // gpio_out_write
#include "generic/misc.h"
#include "command.h"
#include "sched.h" // sched_shutdown

DECL_ENUMERATION_RANGE("pin", "PB0", 1*32, 16); //13 + 3 ADC pins
DECL_ENUMERATION_RANGE("pin", "PC0", 2*32, 8);
DECL_ENUMERATION_RANGE("pin", "PD0", 3*32, 23);
DECL_ENUMERATION_RANGE("pin", "PE0", 4*32, 18);
DECL_ENUMERATION_RANGE("pin", "PF0", 5*32, 7);
DECL_ENUMERATION_RANGE("pin", "PG0", 6*32, 19);

DECL_CONSTANT("ADC_MAX", 4095); // 12bit adc

struct gpio_out gpio_out_setup(uint8_t pin, uint8_t val) {
    gpio_init();
    uint8_t port = pin / 32;
    uint8_t pad = pin % 32;
    gpio_pin_t gpio_pin = GPIO_PIN(port, pad);
    struct gpio_out g = { .pin=gpio_pin };
    gpio_out_reset(g, val);
    return g;
}
void gpio_out_reset(struct gpio_out g, uint8_t val) {
    gpio_set_mode(g.pin, GPIO_MODE_OUTPUT);
    gpio_out_write(g, val);
}
void gpio_out_toggle_noirq(struct gpio_out g) {
    gpio_toggle(g.pin);
}
void gpio_out_toggle(struct gpio_out g) {
    gpio_out_toggle_noirq(g);
}
void gpio_out_write(struct gpio_out g, uint8_t val) {
    gpio_write(g.pin, val);
}
struct gpio_in gpio_in_setup(uint8_t pin, int8_t pull_up) {
    gpio_init();
    uint8_t port = pin / 32;
    uint8_t pad = pin % 32;
    gpio_pin_t gpio_pin = GPIO_PIN(port, pad);
    struct gpio_in g = { .pin=gpio_pin };
    gpio_in_reset(g, pull_up);
    return g;
}
void gpio_in_reset(struct gpio_in g, int8_t pull_up) {
    gpio_set_mode(g.pin, GPIO_MODE_INPUT);
    gpio_set_pull(g.pin, pull_up ? GPIO_PULL_UP : GPIO_PULL_NONE);
}
uint8_t gpio_in_read(struct gpio_in g) {
    return gpio_read(g.pin);
}
struct gpio_pwm gpio_pwm_setup(uint8_t pin, uint32_t cycle_time, uint8_t val) {
    return (struct gpio_pwm){.pin=pin};
}
void gpio_pwm_write(struct gpio_pwm g, uint8_t val) {
}

struct gpio_adc gpio_adc_setup(uint8_t pin) {
    // Valid ADC pins PB13-PB15 (channel 0-2)
    if (pin < (32+13) || pin > (32+13+3))
        shutdown("Not a valid ADC pin");

    gpadc_channel_t chan = (gpadc_channel_t)(pin - (32+13));

    // Initialize the GPADC
    gpadc_init(1000);
    gpadc_channel_enable(chan);

    // Small delay for ADC to start converting
    for (volatile int i = 0; i < 10000; i++);

    return (struct gpio_adc){ .chan=chan };
}
uint32_t gpio_adc_sample(struct gpio_adc g) {
    // No delay needed - continuous mode always has fresh data
    return 0;
}
uint16_t gpio_adc_read(struct gpio_adc g) {
    // Direct register read - same as original DSP implementation
    return gpadc_read_data(g.chan);
}
void gpio_adc_cancel_sample(struct gpio_adc g) {
    // Keep channel running; cancellation is a no-op for this ADC
}

struct spi_config
spi_setup(uint32_t bus, uint8_t mode, uint32_t rate)
{
    return (struct spi_config){ };
}
void
spi_prepare(struct spi_config config)
{
}
void
spi_transfer(struct spi_config config, uint8_t receive_data
             , uint8_t len, uint8_t *data)
{
}
