#include "radio_soapy_factory.hpp"
#include "radio_soapy_session.hpp"

#include "fmt/format.h"
#include "srsran/support/error_handling.h"
#include <cmath>

using namespace srsran;

// Statische Instanz des Validators definieren.
radio_factory_soapy_impl::config_validator_soapy radio_factory_soapy_impl::config_validator;

const radio_configuration::validator& radio_factory_soapy_impl::get_configuration_validator()
{
  return config_validator;
}

bool radio_factory_soapy_impl::config_validator_soapy::is_configuration_valid(
    const radio_configuration::radio& config) const
{
  // Sehr simple Checks, angelehnt an UHD, aber bewusst relaxter fuer den Anfang.

  if (!std::isnormal(config.sampling_rate_Hz) || (config.sampling_rate_Hz <= 0.0)) {
    fmt::print("Soapy radio: sampling_rate_Hz={} ist ungueltig.\n", config.sampling_rate_Hz);
    return false;
  }

  if (config.tx_streams.empty() || config.rx_streams.empty()) {
    fmt::print("Soapy radio: mindestens ein TX- und ein RX-Stream werden erwartet.\n");
    return false;
  }

  if (config.tx_streams.size() != config.rx_streams.size()) {
    fmt::print("Soapy radio: Anzahl TX-Streams ({}) stimmt nicht mit RX-Streams ({}) ueberein.\n",
               config.tx_streams.size(),
               config.rx_streams.size());
    return false;
  }

  // TODO: spaeter koennen wir hier noch mehr Checks fuer Frequenzen, Gains usw. einfuegen.
  return true;
}

std::unique_ptr<radio_session> radio_factory_soapy_impl::create(const radio_configuration::radio& config,
                                                                task_executor&                    async_task_executor,
                                                                radio_notification_handler&       notifier)
{
  if (!config_validator.is_configuration_valid(config)) {
    report_error("Soapy radio configuration ist ungueltig.");
    return nullptr;
  }

  // Explizit als Base-Pointer konstruieren.
  return std::unique_ptr<radio_session>(
      static_cast<radio_session*>(new radio_session_soapy_impl(config, async_task_executor, notifier)));
}

// *** WICHTIG: Das ist genau der Symbolname, den plugin_radio_factory via dlsym sucht. ***
std::unique_ptr<radio_factory> srsran::create_dynamic_radio_factory()
{
  return std::make_unique<radio_factory_soapy_impl>();
}
