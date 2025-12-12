// lib/radio/soapy/radio_soapy_gateway.cpp
#include "radio_soapy_gateway.hpp"

#include <SoapySDR/Types.hpp>

#include <algorithm>
#include <complex>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace srsran {

// ---------- transmitter_impl ----------

class radio_soapy_gateway::transmitter_impl : public baseband_gateway_transmitter
{
public:
  transmitter_impl(SoapySDR::Device& dev_, SoapySDR::Stream* stream_) : dev(dev_), stream(stream_) {}

  void transmit(const baseband_gateway_buffer_reader&        data,
                const baseband_gateway_transmitter_metadata& metadata) override
  {
    (void)metadata;

    if (!stream) {
      throw std::runtime_error("Soapy TX stream is null");
    }

    const unsigned nof_samples = data.get_nof_samples();
    if (nof_samples == 0) {
      return;
    }

    // srsRAN liefert SC16: span<const std::complex<short>>
    auto view = data.get_channel_buffer(0);
    if (!view.data()) {
      throw std::runtime_error("srsRAN TX view.data() is null");
    }

    tmp_sc16.resize(nof_samples);
    std::copy_n(view.data(), nof_samples, tmp_sc16.data());

    const void* buffs[1] = { static_cast<const void*>(tmp_sc16.data()) };

    int       flags   = 0;
    long long time_ns = 0;

    const int ret = dev.writeStream(stream, buffs, static_cast<int>(nof_samples), flags, time_ns);
    if (ret < 0) {
      throw std::runtime_error("Soapy: writeStream() failed with code " + std::to_string(ret));
    }
  }

private:
  SoapySDR::Device& dev;
  SoapySDR::Stream* stream = nullptr;
  std::vector<std::complex<int16_t>> tmp_sc16;
};

// ---------- receiver_impl ----------

class radio_soapy_gateway::receiver_impl : public baseband_gateway_receiver
{
public:
  receiver_impl(SoapySDR::Device& dev_,
                SoapySDR::Stream* stream_,
                double            sampling_rate_hz_) :
    dev(dev_),
    stream(stream_),
    sampling_rate_hz(sampling_rate_hz_)
  {
  }

  metadata receive(baseband_gateway_buffer_writer& data) override
  {
    metadata md{};
    md.ts = last_ts;

    if (stream == nullptr) {
      return md;
    }

    // Zielbuffer von srsRAN (complex<int16>)
    auto     view        = data.get_channel_buffer(0);
    unsigned req_samples = data.get_nof_samples();

    // Soapy arbeitet mit CF32 → temporärer Buffer
    tmp_cf32.resize(req_samples);

    void* buffs[1] = { tmp_cf32.data() };

    int       flags   = 0;
    long long time_ns = 0;

    // WICHTIG: Timeout, damit Ctrl+C funktioniert
    constexpr long timeout_us = 100000; // 100 ms

    const int ret = dev.readStream(stream,
                                   buffs,
                                   static_cast<int>(req_samples),
                                   flags,
                                   time_ns,
                                   timeout_us);

    // Timeout ist NORMAL → einfach nichts liefern
    if (ret == SOAPY_SDR_TIMEOUT) {
      md.ts = last_ts;
      return md;
    }

    // Fehler → Exception (srsRAN stoppt sauber)
    if (ret < 0) {
      throw std::runtime_error(
        "Soapy readStream failed with code " + std::to_string(ret));
    }

    // Anzahl effektiv gelesener Samples
    const unsigned nread = static_cast<unsigned>(ret);
    const unsigned ncopy = std::min(nread, req_samples);

    // CF32 → CI16 (saubere Konversion, KEIN memcpy)
    for (unsigned i = 0; i < ncopy; ++i) {
      const float re = tmp_cf32[i].real();
      const float im = tmp_cf32[i].imag();

      // einfache Skalierung (1.0 → int16 max)
      constexpr float scale = 32767.0f;

      view[i].real(static_cast<int16_t>(
        std::clamp(re * scale, -32768.0f, 32767.0f)));
      view[i].imag(static_cast<int16_t>(
        std::clamp(im * scale, -32768.0f, 32767.0f)));
    }

    last_ts += ncopy;
    md.ts    = last_ts;
    return md;
  }

private:
  SoapySDR::Device& dev;
  SoapySDR::Stream* stream = nullptr;

  double sampling_rate_hz = 0.0;

  baseband_gateway_timestamp last_ts = 0;

  // temporärer CF32 RX-Buffer
  std::vector<std::complex<float>> tmp_cf32;
};

// ---------- radio_soapy_gateway ----------

radio_soapy_gateway::radio_soapy_gateway(SoapySDR::Device&                  dev_,
                                         double                             sampling_rate_hz_,
                                         const radio_configuration::stream*  tx_cfg,
                                         const radio_configuration::stream*  rx_cfg,
                                         task_executor&                      async_task_executor,
                                         radio_notification_handler&         notifier) :
  dev(dev_),
  sampling_rate_hz(sampling_rate_hz_)
{
  (void)tx_cfg;
  (void)rx_cfg;
  (void)async_task_executor;
  (void)notifier;

  std::vector<size_t> tx_channels = {0};
  std::vector<size_t> rx_channels = {0};

  tx_stream = dev.setupStream(SOAPY_SDR_TX, "CS16", tx_channels);
  if (!tx_stream) {
    throw std::runtime_error("Soapy: setupStream(TX, CS16) failed");
  }

  rx_stream = dev.setupStream(SOAPY_SDR_RX, "CS16", rx_channels);
  if (!rx_stream) {
    throw std::runtime_error("Soapy: setupStream(RX, CS16) failed");
  }

  tx = std::make_unique<transmitter_impl>(dev, tx_stream);
  rx = std::make_unique<receiver_impl>(dev, rx_stream, sampling_rate_hz);
}

radio_soapy_gateway::~radio_soapy_gateway()
{
  stop();

  if (tx_stream != nullptr) {
    dev.closeStream(tx_stream);
    tx_stream = nullptr;
  }
  if (rx_stream != nullptr) {
    dev.closeStream(rx_stream);
    rx_stream = nullptr;
  }
}

baseband_gateway_transmitter& radio_soapy_gateway::get_transmitter()
{
  return *tx;
}

baseband_gateway_receiver& radio_soapy_gateway::get_receiver()
{
  return *rx;
}

unsigned radio_soapy_gateway::get_receiver_optimal_buffer_size() const
{
  if (!rx_stream) {
    return 0;
  }
  return static_cast<unsigned>(dev.getStreamMTU(rx_stream));
}

void radio_soapy_gateway::start(baseband_gateway_timestamp init_time)
{
  (void)init_time;

  if (tx_stream != nullptr) {
    const int ret = dev.activateStream(tx_stream, 0, 0, 0);
    if (ret != 0) {
      throw std::runtime_error("Soapy: activateStream(TX) failed with code " + std::to_string(ret));
    }
  }

  if (rx_stream != nullptr) {
    const int ret = dev.activateStream(rx_stream, 0, 0, 0);
    if (ret != 0) {
      throw std::runtime_error("Soapy: activateStream(RX) failed with code " + std::to_string(ret));
    }
  }
}

void radio_soapy_gateway::stop()
{
  if (tx_stream != nullptr) {
    dev.deactivateStream(tx_stream, 0, 0);
  }
  if (rx_stream != nullptr) {
    dev.deactivateStream(rx_stream, 0, 0);
  }
}

} // namespace srsran
