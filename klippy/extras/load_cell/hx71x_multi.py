# HX71x Multi-Sensor Fusion Support
#
# Configures N standard HX711/HX717 sensors and a firmware-side fusion group
# that averages their readings. Reuses all existing sensor_hx71x.c
# infrastructure (timers, bit-bang, capture task).
#
# Copyright (C) 2026 Timo V
#
# This file may be distributed under the terms of the GNU GPLv3 license.
import logging

from .. import bulk_sensor
from .hx71x import HX71xBase, UPDATE_INTERVAL

MAX_MULTI_SENSORS = 4

CHIP_PARAMS = {
    "hx711": ("hx711", {80: 80, 10: 10}, 80,
              {"A-128": 1, "B-32": 2, "A-64": 3}, "A-128"),
    "hx717": ("hx717", {320: 320, 80: 80, 20: 20, 10: 10}, 320,
              {"A-128": 1, "B-64": 2, "A-64": 3, "B-8": 4}, "A-128"),
}


class HX71xMulti(HX71xBase):
    def __init__(self, config):
        self.printer = printer = config.get_printer()
        self.name = config.get_name().split()[-1]
        self.last_error_count = 0
        self.consecutive_fails = 0
        # Overall sensor config
        chip_name, sps_opts, sps_def, gain_opts, gain_def = \
            config.getchoice("chip", CHIP_PARAMS)
        self.sensor_type = "hx71x_multi (%s)" % chip_name
        self.sps = config.getchoice("sample_rate", sps_opts, default=sps_def)
        self.gain_channel = int(
            config.getchoice("gain", gain_opts, default=gain_def)
        )
        self.sensor_count = config.getint(
            "sensor_count", minval=1, maxval=MAX_MULTI_SENSORS
        )
        self._effective_sps = self.sps * self.sensor_count
        # Per-sensor config
        ppins = printer.lookup_object("pins")
        self.mcu = None
        pin_cfgs = []
        for i in range(self.sensor_count):
            p = "sensor_%d_" % i
            dp = ppins.lookup_pin(config.get(p + "dout_pin"))
            sp = ppins.lookup_pin(config.get(p + "sclk_pin"))
            inv = config.getboolean(p + "invert", default=False)
            if sp["chip"] is not dp["chip"]:
                raise config.error(
                    "%s: sensor_%d pins must be on same MCU" % (self.name, i))
            if self.mcu is None:
                self.mcu = dp["chip"]
            elif dp["chip"] is not self.mcu:
                raise config.error(
                    "%s: All sensors must be on the same MCU" % self.name)
            pin_cfgs.append((dp["pin"], sp["pin"], inv))
        # Allocate OIDs: one per sensor + one for fusion group
        sensor_oids = [self.mcu.create_oid()
                       for _ in range(self.sensor_count)]
        self.oid = self.mcu.create_oid()  # fusion group OID
        # Configure individual sensors (reuses existing config_hx71x)
        for i, (dpin, spin, _inv) in enumerate(pin_cfgs):
            self.mcu.add_config_cmd(
                "config_hx71x oid=%d gain_channel=%d dout_pin=%s sclk_pin=%s"
                % (sensor_oids[i], self.gain_channel, dpin, spin))
        # Configure fusion group
        self.mcu.add_config_cmd(
            "config_hx71x_fusion oid=%d" % self.oid)
        for i, (_dpin, _spin, inv) in enumerate(pin_cfgs):
            self.mcu.add_config_cmd(
                "hx71x_fusion_add oid=%d hx71x_oid=%d invert=%d"
                % (self.oid, sensor_oids[i], int(inv)))
        self.mcu.add_config_cmd(
            "query_hx71x_fusion oid=%d rest_ticks=0" % self.oid,
            on_restart=True)
        # Bulk sensor setup
        chip_smooth = self._effective_sps * UPDATE_INTERVAL * 2
        self.ffreader = bulk_sensor.FixedFreqReader(
            self.mcu, chip_smooth, "<i")
        self.batch_bulk = bulk_sensor.BatchBulkHelper(
            self.printer, self._process_batch, self._start_measurements,
            self._finish_measurements, UPDATE_INTERVAL)
        self.query_hx71x_cmd = self.attach_probe_cmd = None
        self.mcu.register_config_callback(self._build_config)

    def _build_config(self):
        self.query_hx71x_cmd = self.mcu.lookup_command(
            "query_hx71x_fusion oid=%c rest_ticks=%u")
        self.attach_probe_cmd = self.mcu.lookup_command(
            "hx71x_fusion_attach_load_cell_probe oid=%c"
            " load_cell_probe_oid=%c")
        self.ffreader.setup_query_command(
            "query_hx71x_fusion_status oid=%c",
            oid=self.oid, cq=self.mcu.alloc_command_queue())

    def get_samples_per_second(self) -> int:
        return self._effective_sps

    def _start_measurements(self):
        self.consecutive_fails = 0
        self.last_error_count = 0
        rest_ticks = self.mcu.seconds_to_clock(1.0 / (10.0 * self.sps))
        self.query_hx71x_cmd.send([self.oid, rest_ticks])
        logging.info(
            "%s starting '%s' measurements", self.sensor_type, self.name)
        self.ffreader.note_start()

HX71X_MULTI_SENSOR_TYPE = {"hx71x_multi": HX71xMulti}
