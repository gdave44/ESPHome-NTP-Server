#include "gps_pps.h"
#include "esphome/core/log.h"
#include <sys/time.h>
#include <cstdlib>

namespace esphome {
namespace gps_pps {

static const char *const TAG = "gps_pps";

void IRAM_ATTR GPSPPSTime::pps_isr(GPSPPSTime *self) {
  self->last_pps_micros_ = micros();
  self->pps_flag_ = true;
}

void GPSPPSTime::setup() {
  ESP_LOGI(TAG, "Setting up GPS PPS time source...");
  this->pps_pin_->setup();
  this->pps_pin_->attach_interrupt(&GPSPPSTime::pps_isr, this, gpio::INTERRUPT_RISING_EDGE);
}

void GPSPPSTime::loop() {
  if (this->pps_flag_) {
    this->pps_flag_ = false;
    this->apply_pps_correction_();
  }
}

void GPSPPSTime::set_pps_time_(time_t epoch, int32_t compensation_us) {
  int32_t elapsed_us = static_cast<int32_t>(micros() - this->last_pps_micros_);
  int32_t adjusted_us = elapsed_us - compensation_us;

  struct timeval tv;
  tv.tv_sec = epoch;
  tv.tv_usec = (adjusted_us > 0) ? adjusted_us : 0;

  struct timezone tz = {0, 0};
  settimeofday(&tv, &tz);
}

void GPSPPSTime::apply_pps_correction_() {
  if (!this->gps_time_valid_) {
    ESP_LOGD(TAG, "PPS pulse received but no valid GPS time yet");
    return;
  }

  if (this->last_gps_epoch_ == 0) {
    ESP_LOGD(TAG, "PPS pulse skipped, waiting for NMEA epoch");
    return;
  }

  this->pps_count_++;
  this->last_pps_millis_ = millis();

  // Detect ISR latency from deviation of the PPS interval from 1,000,000 µs.
  // A UART ISR that preempts the PPS ISR causes a late micros() capture.
  int32_t isr_latency_us = 0;
  if (this->pps_count_ > 1) {
    uint32_t interval = this->last_pps_micros_ - this->prev_pps_micros_;
    int32_t deviation = static_cast<int32_t>(interval) - 1000000;
    if (deviation > 500 && deviation < 10000) {
      isr_latency_us = deviation;
      ESP_LOGD(TAG, "PPS ISR latency detected: %d µs", isr_latency_us);
    }
  }
  this->prev_pps_micros_ = this->last_pps_micros_;

  // PPS marks the start of the second AFTER the NMEA sentence's epoch
  time_t corrected_epoch = this->last_gps_epoch_ + 1;
  this->last_gps_epoch_ = corrected_epoch;

  // Measure system clock error at the PPS edge
  uint32_t isr_delay_us = micros() - this->last_pps_micros_;
  struct timeval current_tv;
  gettimeofday(&current_tv, nullptr);

  int64_t system_us_at_pps = static_cast<int64_t>(current_tv.tv_sec) * 1000000LL
                              + current_tv.tv_usec - isr_delay_us - isr_latency_us;
  int64_t expected_us = static_cast<int64_t>(corrected_epoch) * 1000000LL;
  this->last_drift_us_ = system_us_at_pps - expected_us;

  if (!this->pps_synced_) {
    this->set_pps_time_(corrected_epoch);
    this->last_drift_us_ = 0;
    this->pps_synced_ = true;
    this->time_sync_callback_.call();
    ESP_LOGI(TAG, "PPS-disciplined time synchronized");

  } else if (this->last_drift_us_ > 500000 || this->last_drift_us_ < -500000) {
    // Large jump: GPS was reconfigured or PPS gap. Re-sync from scratch.
    ESP_LOGW(TAG, "Large drift (%lld µs) — re-syncing from NMEA+PPS",
             (long long) this->last_drift_us_);
    this->pps_synced_ = false;
    this->gps_time_valid_ = false;
    this->has_gps_time_ = false;
    this->last_gps_epoch_ = 0;
    this->last_drift_us_ = 0;
    this->drift_mean_x256_ = 0;

  } else {
#ifdef USE_ESP_IDF
    // adjtime() slews the clock gradually — no jumps while serving NTP
    if (this->last_drift_us_ > -500 && this->last_drift_us_ < 500) {
      // Overcorrect by the running mean to centre the sawtooth error around zero,
      // halving peak error seen by NTP clients
      int64_t overcorrection = this->drift_mean_x256_ / 256;
      int64_t correction_us = -(this->last_drift_us_ + overcorrection);

      struct timeval delta;
      delta.tv_sec = correction_us / 1000000LL;
      delta.tv_usec = correction_us % 1000000LL;
      adjtime(&delta, nullptr);

      this->drift_mean_x256_ += (this->last_drift_us_ * 256 - this->drift_mean_x256_) / 64;
      this->last_clock_offset_us_ = this->last_drift_us_;
    } else {
      ESP_LOGD(TAG, "Drift spike filtered: %lld µs", (long long) this->last_drift_us_);
      this->last_clock_offset_us_ += this->drift_mean_x256_ / 256;
    }
#else
    // Arduino: hard-correct every pulse with accumulated drift pre-compensation
    this->set_pps_time_(corrected_epoch, this->drift_compensation_us_ - isr_latency_us);

    if (isr_latency_us == 0 && this->last_drift_us_ > -50 && this->last_drift_us_ < 50) {
      this->drift_compensation_us_ += static_cast<int32_t>(this->last_drift_us_) / 4;
    } else {
      ESP_LOGD(TAG, "Drift spike filtered from EMA: %lld µs", (long long) this->last_drift_us_);
    }
    this->last_clock_offset_us_ = this->last_drift_us_;
#endif
  }

  ESP_LOGI(TAG, "PPS #%lu epoch=%ld drift=%lld µs",
           (unsigned long) this->pps_count_,
           (long) corrected_epoch,
           (long long) this->last_drift_us_);
}

void GPSPPSTime::on_update(TinyGPSPlus &tiny_gps) {
  if (!tiny_gps.time.isValid() || !tiny_gps.date.isValid() ||
      tiny_gps.date.year() < 2025) {
    return;
  }

  ESPTime val{};
  val.year = tiny_gps.date.year();
  val.month = tiny_gps.date.month();
  val.day_of_month = tiny_gps.date.day();
  val.day_of_week = 1;
  val.day_of_year = 1;
  val.hour = tiny_gps.time.hour();
  val.minute = tiny_gps.time.minute();
  val.second = tiny_gps.time.second();
  val.recalc_timestamp_utc(false);

  this->gps_time_valid_ = true;

  // Once PPS is synced, it owns the epoch counter — don't overwrite with stale NMEA.
  // Also skip if a PPS ISR is pending to avoid a +1s race.
  if (!this->pps_synced_ && !this->pps_flag_) {
    this->last_gps_epoch_ = val.timestamp;
  }

  // Set coarse time once as fallback until PPS disciplines the clock
  if (!this->pps_synced_ && !this->has_gps_time_) {
    this->synchronize_epoch_(val.timestamp);
    this->has_gps_time_ = true;
    ESP_LOGI(TAG, "GPS coarse time set: %04d-%02d-%02d %02d:%02d:%02d (waiting for PPS)",
             val.year, val.month, val.day_of_month, val.hour, val.minute, val.second);
  }
}

void GPSPPSTime::update() {
  if (this->clock_offset_sensor_ != nullptr && this->pps_synced_) {
    this->clock_offset_sensor_->publish_state(static_cast<float>(this->last_clock_offset_us_));
  }
  if (this->pps_drift_sensor_ != nullptr && this->pps_synced_) {
    this->pps_drift_sensor_->publish_state(static_cast<float>(this->last_drift_us_));
  }

  if (this->pps_synced_) {
    auto t = this->now();
    ESP_LOGD(TAG, "Time: %04d-%02d-%02d %02d:%02d:%02d",
             t.year, t.month, t.day_of_month, t.hour, t.minute, t.second);
  }
}

bool GPSPPSTime::is_synchronized() const {
  return this->pps_synced_ && (millis() - this->last_pps_millis_) < PPS_TIMEOUT_MS;
}

void GPSPPSTime::dump_config() {
  ESP_LOGCONFIG(TAG, "GPS PPS Time:");
  LOG_PIN("  PPS Pin: ", this->pps_pin_);
  ESP_LOGCONFIG(TAG, "  PPS Synced: %s", YESNO(this->pps_synced_));
}

}  // namespace gps_pps
}  // namespace esphome
