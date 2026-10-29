#pragma once
// Airzone zone as a climate entity (Innobus Pro6 / AZ6 Modbus map).
//
// Zone z uses registers z*256 + n:
//   n = 0  flags: bit 2 = zone on
//   n = 3  setpoint x10 (writable, function 06)
//   n = 8  probe temperature x10
// System register 0, bits 0-8 = mode of the whole installation:
//   0 stop, 1 cool, 3 fan, 4 heat (air), 5/258 heat, 6 dry.
// The mode is shared by every zone: choosing Heat on one zone switches the
// installation to heat for all of them (as the Airzone thermostats do).
#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/modbus_controller/modbus_controller.h"
#include <cmath>

namespace esphome {
namespace airzone {

static const char *const TAG = "airzone.zone";

class AirzoneZone : public climate::Climate, public Component {
 public:
  void set_controller(modbus_controller::ModbusController *c) { this->controller_ = c; }
  void set_zone(int zone) { this->zone_ = zone; }
  void set_temperature_sensor(sensor::Sensor *s) { this->temperature_ = s; }
  void set_setpoint_sensor(sensor::Sensor *s) { this->setpoint_ = s; }
  void set_zone_register(sensor::Sensor *s) { this->zone_reg_ = s; }
  void set_system_register(sensor::Sensor *s) { this->system_reg_ = s; }
  void set_demand_sensor(binary_sensor::BinarySensor *s) { this->demand_ = s; }
  void set_limits(float lo, float hi) { this->min_ = lo; this->max_ = hi; }

  void setup() override {
    auto refresh = [this](float) { this->sync_(); };
    this->temperature_->add_on_state_callback(refresh);
    this->setpoint_->add_on_state_callback(refresh);
    this->zone_reg_->add_on_state_callback(refresh);
    this->system_reg_->add_on_state_callback(refresh);
    if (this->demand_ != nullptr)
      this->demand_->add_on_state_callback([this](bool) { this->sync_(); });
  }

  float get_setup_priority() const override { return setup_priority::DATA; }

  climate::ClimateTraits traits() override {
    climate::ClimateTraits t;
    t.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE | climate::CLIMATE_SUPPORTS_ACTION);
    t.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_HEAT, climate::CLIMATE_MODE_COOL,
                           climate::CLIMATE_MODE_DRY, climate::CLIMATE_MODE_FAN_ONLY});
    t.set_visual_min_temperature(this->min_);
    t.set_visual_max_temperature(this->max_);
    t.set_visual_target_temperature_step(0.5f);
    t.set_visual_current_temperature_step(0.1f);
    return t;
  }

 protected:
  void control(const climate::ClimateCall &call) override {
    if (call.get_target_temperature().has_value()) {
      float t = clamp(*call.get_target_temperature(), this->min_, this->max_);
      t = roundf(t * 2.0f) / 2.0f;
      this->write_(this->zone_ * 256 + 3, (uint16_t) lroundf(t * 10.0f));
      this->target_temperature = t;
    }
    if (call.get_mode().has_value() && !std::isnan(this->zone_reg_->state)) {
      auto mode = *call.get_mode();
      uint16_t zr = (uint16_t) this->zone_reg_->state;
      if (mode == climate::CLIMATE_MODE_OFF) {
        this->write_(this->zone_ * 256, zr & ~0x4);
      } else {
        int code = mode_code_(mode);
        if (!std::isnan(this->system_reg_->state)) {
          uint16_t sr = (uint16_t) this->system_reg_->state;
          if (system_mode_(sr & 0x1FF) != mode)
            this->write_(0, (sr & ~0x1FF) | code);
        }
        if (!(zr & 0x4))
          this->write_(this->zone_ * 256, zr | 0x4);
      }
      this->mode = mode;
    }
    this->publish_state();
  }

  void write_(uint16_t address, uint16_t value) {
    ESP_LOGI(TAG, "zone %d: register 0x%04X <- 0x%04X", this->zone_, address, value);
    this->controller_->queue_command(
        modbus_controller::ModbusCommandItem::create_write_single_command(this->controller_, address, value));
    // Fast feedback: read the central again right after the write and once more after 3 s
    // (named timeouts, so several writes in a row trigger a single read-back).
    this->set_timeout("reread_now", 150, [this]() { this->controller_->update(); });
    this->set_timeout("reread_late", 3000, [this]() { this->controller_->update(); });
  }

  static int mode_code_(climate::ClimateMode m) {
    switch (m) {
      case climate::CLIMATE_MODE_COOL: return 1;
      case climate::CLIMATE_MODE_FAN_ONLY: return 3;
      case climate::CLIMATE_MODE_DRY: return 6;
      default: return 4;  // heat, air only
    }
  }

  static climate::ClimateMode system_mode_(int code) {
    switch (code) {
      case 1: case 9: return climate::CLIMATE_MODE_COOL;
      case 3: return climate::CLIMATE_MODE_FAN_ONLY;
      case 4: case 5: case 258: return climate::CLIMATE_MODE_HEAT;
      case 6: return climate::CLIMATE_MODE_DRY;
      default: return climate::CLIMATE_MODE_OFF;  // 0 = stop
    }
  }

  void sync_() {
    if (!std::isnan(this->temperature_->state))
      this->current_temperature = this->temperature_->state;
    if (!std::isnan(this->setpoint_->state))
      this->target_temperature = this->setpoint_->state;
    if (!std::isnan(this->zone_reg_->state) && !std::isnan(this->system_reg_->state)) {
      bool on = ((int) this->zone_reg_->state) & 0x4;
      auto sys = system_mode_(((int) this->system_reg_->state) & 0x1FF);
      this->mode = on ? sys : climate::CLIMATE_MODE_OFF;
    }
    bool demand = this->demand_ != nullptr && this->demand_->state;
    switch (this->mode) {
      case climate::CLIMATE_MODE_OFF: this->action = climate::CLIMATE_ACTION_OFF; break;
      case climate::CLIMATE_MODE_HEAT: this->action = demand ? climate::CLIMATE_ACTION_HEATING : climate::CLIMATE_ACTION_IDLE; break;
      case climate::CLIMATE_MODE_COOL: this->action = demand ? climate::CLIMATE_ACTION_COOLING : climate::CLIMATE_ACTION_IDLE; break;
      case climate::CLIMATE_MODE_DRY: this->action = demand ? climate::CLIMATE_ACTION_DRYING : climate::CLIMATE_ACTION_IDLE; break;
      default: this->action = demand ? climate::CLIMATE_ACTION_FAN : climate::CLIMATE_ACTION_IDLE; break;
    }
    this->publish_state();
  }

  modbus_controller::ModbusController *controller_{nullptr};
  int zone_{1};
  sensor::Sensor *temperature_{nullptr};
  sensor::Sensor *setpoint_{nullptr};
  sensor::Sensor *zone_reg_{nullptr};
  sensor::Sensor *system_reg_{nullptr};
  binary_sensor::BinarySensor *demand_{nullptr};
  float min_{18.0f};
  float max_{30.0f};
};

}  // namespace airzone
}  // namespace esphome
