#pragma once

#include "esphome/components/gps/gps.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace gps_pps {

class GPSPPSTime : public time::RealTimeClock, public gps::GPSListener {
 public:
  void set_pps_pin(InternalGPIOPin *pin) { this->pps_pin_ = pin; }
  void set_clock_offset_sensor(sensor::Sensor *s) { this->clock_offset_sensor_ = s; }
  void set_pps_drift_sensor(sensor::Sensor *s) { this->pps_drift_sensor_ = s; }

  void setup() override;
  void loop() override;
  void update() override;
  void dump_config() override;

  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  // Returns true when PPS-disciplined time is active and a pulse arrived recently
  bool is_synchronized() const;

  void on_update(TinyGPSPlus &tiny_gps) override;

 protected:
  void apply_pps_correction_();
  void set_pps_time_(time_t epoch, int32_t compensation_us = 0);

  InternalGPIOPin *pps_pin_{nullptr};
  sensor::Sensor *clock_offset_sensor_{nullptr};
  sensor::Sensor *pps_drift_sensor_{nullptr};

  // Set by PPS ISR; read and cleared in loop()
  volatile bool pps_flag_{false};
  volatile uint32_t last_pps_micros_{0};

  // Last GPS epoch from NMEA; written in on_update(), read in apply_pps_correction_()
  volatile time_t last_gps_epoch_{0};
  volatile bool gps_time_valid_{false};

  bool pps_synced_{false};
  bool has_gps_time_{false};
  uint32_t pps_count_{0};
  uint32_t prev_pps_micros_{0};
  uint32_t last_pps_millis_{0};

  int64_t last_drift_us_{0};
  int64_t last_clock_offset_us_{0};

  // Non-adjtime platforms: accumulated per-second crystal drift estimate
  int32_t drift_compensation_us_{0};

  // ESP-IDF adjtime path: fixed-point x256 running mean of measured drift
  int64_t drift_mean_x256_{0};

  // If no PPS pulse arrives within this window, is_synchronized() returns false
  static const uint32_t PPS_TIMEOUT_MS = 10000;

  static void IRAM_ATTR pps_isr(GPSPPSTime *self);
};

}  // namespace gps_pps
}  // namespace esphome
