#include "radio_soapy_session.hpp"
#include "radio_soapy_gateway.hpp"

#include <SoapySDR/Device.hpp>
#include "srsran/support/error_handling.h"
#include "srsran/support/srsran_assert.h"

#include <algorithm> // std::max

using namespace srsran;

namespace {

/// Minimaler Management-Plane, der alle Kommandos akzeptiert, aber nichts tut.
/// So kann der gNB schon mal laufen, ohne dass wir Gains/Frequenzen dynamisch setzen.
class noop_radio_management_plane : public radio_management_plane
{
public:
  bool set_tx_gain(unsigned port_id, double gain_dB) override
  {
    (void)port_id;
    (void)gain_dB;
    return true;
  }

  bool set_rx_gain(unsigned port_id, double gain_dB) override
  {
    (void)port_id;
    (void)gain_dB;
    return true;
  }

  bool set_tx_freq(unsigned stream_id, double center_freq_Hz) override
  {
    (void)stream_id;
    (void)center_freq_Hz;
    return true;
  }

  bool set_rx_freq(unsigned stream_id, double center_freq_Hz) override
  {
    (void)stream_id;
    (void)center_freq_Hz;
    return true;
  }
};

} // namespace

radio_session_soapy_impl::radio_session_soapy_impl(const radio_configuration::radio& config,
                                                   task_executor&                    async_task_executor,
                                                   radio_notification_handler&       notifier)
{
  // TODO: spaeter device-Argumente sauber aus config ableiten (z.B. config.device_driver / config.args).
  SoapySDR::Kwargs args;

  dev.reset(SoapySDR::Device::make(args));
  report_fatal_error_if_not(dev, "Soapy radio: Failed to open device");

  // TODO: Hier koennen wir spaeter noch Sample-Rate, Frequenzen und Gains direkt am Soapy-Device setzen,
  //       basierend auf config.sampling_rate_Hz und den Stream-Konfigurationen.

  // Gateways anlegen (zunaechst 1:1 pro Stream).
  unsigned nof_streams = std::max(config.tx_streams.size(), config.rx_streams.size());
  gateways.reserve(nof_streams);

  for (unsigned i = 0; i != nof_streams; ++i) {
    const radio_configuration::stream* tx_stream_cfg =
        (i < config.tx_streams.size()) ? &config.tx_streams[i] : nullptr;

    const radio_configuration::stream* rx_stream_cfg =
        (i < config.rx_streams.size()) ? &config.rx_streams[i] : nullptr;

    gateways.emplace_back(std::make_unique<radio_soapy_gateway>(*dev,
                                                                config.sampling_rate_Hz,
                                                                tx_stream_cfg,
                                                                rx_stream_cfg,
                                                                async_task_executor,
                                                                notifier));
  }

  // Einfacher Management-Plane ohne Funktionalitaet.
  mgmt = std::make_unique<noop_radio_management_plane>();
}

radio_session_soapy_impl::~radio_session_soapy_impl() = default;

radio_management_plane& radio_session_soapy_impl::get_management_plane()
{
  return *mgmt;
}

baseband_gateway& radio_session_soapy_impl::get_baseband_gateway(unsigned stream_id)
{
  srsran_assert(stream_id < gateways.size(), "Ungueltiger Stream-Index {}", stream_id);
  return *gateways[stream_id];
}

baseband_gateway_timestamp radio_session_soapy_impl::read_current_time()
{
  // Minimal-Implementierung: 0 zurueckgeben.
  // Spaeter koennen wir das an Soapy-Hardwarezeit koppeln (falls verfuegbar).
  return 0;
}

void radio_session_soapy_impl::start(baseband_gateway_timestamp init_time)
{
  // Starten der Streams passiert im Gateway.
  for (auto& gw : gateways) {
    gw->start(init_time);
  }
}

void radio_session_soapy_impl::stop()
{
  for (auto& gw : gateways) {
    gw->stop();
  }
}
