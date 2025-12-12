// lib/radio/soapy/radio_soapy_gateway.hpp
#pragma once

#include "srsran/gateways/baseband/baseband_gateway.h"
#include "srsran/gateways/baseband/baseband_gateway_transmitter.h"
#include "srsran/gateways/baseband/baseband_gateway_receiver.h"
#include "srsran/gateways/baseband/buffer/baseband_gateway_buffer_reader.h"
#include "srsran/gateways/baseband/buffer/baseband_gateway_buffer_writer.h"
#include "srsran/gateways/baseband/baseband_gateway_transmitter_metadata.h"
#include "srsran/gateways/baseband/baseband_gateway_timestamp.h"
#include "srsran/radio/radio_configuration.h"
#include "srsran/radio/radio_factory.h"

#include <SoapySDR/Device.hpp>
#include <cstdint>
#include <memory>
#include <atomic>

namespace srsran {

class radio_soapy_gateway : public baseband_gateway
{
public:
  radio_soapy_gateway(SoapySDR::Device&                  dev,
                      double                             sampling_rate_hz,
                      const radio_configuration::stream* tx_cfg,
                      const radio_configuration::stream* rx_cfg,
                      task_executor&                     async_task_executor,
                      radio_notification_handler&        notifier);

  ~radio_soapy_gateway() override;

  baseband_gateway_transmitter& get_transmitter() override;
  baseband_gateway_receiver&    get_receiver() override;

  /// Anzahl Samples, die optimalerweise pro receive() gelesen werden sollen.
  unsigned get_receiver_optimal_buffer_size() const override;

  /// Streams aktivieren (optional mit Start-Timestamp).
  void start(baseband_gateway_timestamp init_time);
  void stop();

private:
  // interne TX/RX-Helper-Klassen, die die Soapy-Streams benutzen
  class transmitter_impl;
  class receiver_impl;

  SoapySDR::Device& dev;
  SoapySDR::Stream* tx_stream = nullptr;
  SoapySDR::Stream* rx_stream = nullptr;

  std::unique_ptr<transmitter_impl> tx;
  std::unique_ptr<receiver_impl>    rx;

  double sampling_rate_hz = 0.0;

  std::atomic<bool> running{false};
};

} // namespace srsran
