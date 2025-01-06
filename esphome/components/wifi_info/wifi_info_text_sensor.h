#pragma once

#include "esphome/core/component.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/wifi/wifi_component.h"
#ifdef USE_WIFI
#include <array>

namespace esphome {
namespace wifi_info {

class IPAddressWiFiInfo : public PollingComponent, public text_sensor::TextSensor {
 public:
  void update() override {
    auto ips = wifi::global_wifi_component->wifi_sta_ip_addresses();
    if (ips != this->last_ips_) {
      this->last_ips_ = ips;
      this->publish_state(ips[0].str());
      uint8_t sensor = 0;
      for (auto &ip : ips) {
        if (ip.is_set()) {
          if (this->ip_sensors_[sensor] != nullptr) {
            this->ip_sensors_[sensor]->publish_state(ip.str());
          }
          sensor++;
        }
      }
    }
  }
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }
  std::string unique_id() override { return get_mac_address() + "-wifiinfo-ip"; }
  void dump_config() override;
  void add_ip_sensors(uint8_t index, text_sensor::TextSensor *s) { this->ip_sensors_[index] = s; }

 protected:
  network::IPAddresses last_ips_;
  std::array<text_sensor::TextSensor *, 5> ip_sensors_;
};

class DNSAddressWifiInfo : public PollingComponent, public text_sensor::TextSensor {
 public:
  void update() override {
    auto dns_one = wifi::global_wifi_component->get_dns_address(0);
    auto dns_two = wifi::global_wifi_component->get_dns_address(1);

    std::string dns_results = dns_one.str() + " " + dns_two.str();

    if (dns_results != this->last_results_) {
      this->last_results_ = dns_results;
      this->publish_state(dns_results);
    }
  }
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }
  std::string unique_id() override { return get_mac_address() + "-wifiinfo-dns"; }
  void dump_config() override;

 protected:
  std::string last_results_;
};

class ScanResultsWiFiInfo : public PollingComponent, public text_sensor::TextSensor {
 public:
  void update() override {
    auto scan_results_list = wifi::global_wifi_component->get_scan_result();
    std::unordered_map<std::string, int> ssid_count;
    for (auto &scan : scan_results_list) {
      if (scan.get_is_hidden())
        continue;

      std::string ssid = scan.get_ssid();
      ssid_count[ssid]++;
    }

    std::string scan_results;
    for (auto &scan : scan_results_list) {
      if (scan.get_is_hidden())
        continue;

      std::string ssid = scan.get_ssid();
      scan_results += ssid;
      if (ssid_count[ssid] > 1) {
        char bssid_s[18];
        auto bssid = scan.get_bssid();
        sprintf(bssid_s, "%02X:%02X:%02X:%02X:%02X:%02X", bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
        scan_results += " (";
        scan_results += bssid_s;
        scan_results += ")";
      }
      scan_results += ": ";
      scan_results += esphome::to_string(scan.get_rssi());
      scan_results += "dB | ";
    }

    // Remove the trailing " | " from string
    if (!scan_results.empty()) {
      scan_results.pop_back();
      scan_results.pop_back();
      scan_results.pop_back();
    }

    if (this->last_scan_results_ != scan_results) {
      this->last_scan_results_ = scan_results;
      // There's a limit of 255 characters per state.
      // Longer states just don't get sent so we truncate it.
      this->publish_state(scan_results.substr(0, 255));
    }
  }
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }
  std::string unique_id() override { return get_mac_address() + "-wifiinfo-scanresults"; }
  void dump_config() override;

 protected:
  std::string last_scan_results_;
};

class SSIDWiFiInfo : public PollingComponent, public text_sensor::TextSensor {
 public:
  void update() override {
    std::string ssid = wifi::global_wifi_component->wifi_ssid();
    if (this->last_ssid_ != ssid) {
      this->last_ssid_ = ssid;
      this->publish_state(this->last_ssid_);
    }
  }
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }
  std::string unique_id() override { return get_mac_address() + "-wifiinfo-ssid"; }
  void dump_config() override;

 protected:
  std::string last_ssid_;
};

class BSSIDWiFiInfo : public PollingComponent, public text_sensor::TextSensor {
 public:
  void update() override {
    wifi::bssid_t bssid = wifi::global_wifi_component->wifi_bssid();
    if (memcmp(bssid.data(), last_bssid_.data(), 6) != 0) {
      std::copy(bssid.begin(), bssid.end(), last_bssid_.begin());
      char buf[30];
      sprintf(buf, "%02X:%02X:%02X:%02X:%02X:%02X", bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
      this->publish_state(buf);
    }
  }
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }
  std::string unique_id() override { return get_mac_address() + "-wifiinfo-bssid"; }
  void dump_config() override;

 protected:
  wifi::bssid_t last_bssid_;
};

class MacAddressWifiInfo : public Component, public text_sensor::TextSensor {
 public:
  void setup() override { this->publish_state(get_mac_address_pretty()); }
  std::string unique_id() override { return get_mac_address() + "-wifiinfo-macadr"; }
  void dump_config() override;
};

}  // namespace wifi_info
}  // namespace esphome
#endif
