#pragma once

#include "srsran/radio/radio_factory.h"

namespace srsran {

class radio_session_soapy_impl;

/// Soapy SDR backend fuer srsRAN (radio_soapy).
class radio_factory_soapy_impl : public radio_factory
{
public:
  radio_factory_soapy_impl()           = default;
  ~radio_factory_soapy_impl() override = default;

  // Validator fuer die Radio-Konfiguration.
  const radio_configuration::validator& get_configuration_validator() override;

  // Session-Erzeugung.
  std::unique_ptr<radio_session> create(const radio_configuration::radio& config,
                                        task_executor&                    async_task_executor,
                                        radio_notification_handler&       notifier) override;

private:
  /// Sehr einfacher Config-Validator fuer Soapy.
  class config_validator_soapy : public radio_configuration::validator
  {
  public:
    bool is_configuration_valid(const radio_configuration::radio& config) const override;
  };

  static config_validator_soapy config_validator;
};

/// Plugin-Entry-Point, auf den plugin_radio_factory via dlsym zugreift.
/// Wichtig: genau dieser Name / diese Signatur!
std::unique_ptr<radio_factory> create_dynamic_radio_factory();

} // namespace srsran
