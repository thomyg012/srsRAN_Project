// lib/radio/soapy/radio_soapy_gateway.cpp
#include "radio_soapy_gateway.hpp"

#include <SoapySDR/Types.hpp>
#include <vector>

namespace srsran {

// ---------- transmitter_impl ----------

class radio_soapy_gateway::transmitter_impl : public baseband_gateway_transmitter
{
public:
  transmitter_impl(SoapySDR::Device& dev_, SoapySDR::Stream* stream_) :
    dev(dev_), stream(stream_)
  {
  }

  void transmit(const baseband_gateway_buffer_reader&        data,
                const baseband_gateway_transmitter_metadata& metadata) override
  {
    // metadata aktuell nicht ausgewertet (kontinuierlicher Stream)
    (void)metadata;

    // Wir gehen vorerst von genau einem Kanal aus (Channel 0).
    auto     view        = data.get_channel_buffer(0);
    unsigned nof_samples = data.get_nof_samples();

    const void* buffs[1] = { static_cast<const void*>(view.data()) };

    int       flags   = 0;
    long long time_ns = 0; // ungenutzt, da keine Timed-Uebertragung

    int ret = dev.writeStream(stream, buffs, static_cast<int>(nof_samples), flags, time_ns);
    if (ret < 0) {
      // TODO: Logging einbauen, falls gewuenscht.
    }
  }

private:
  SoapySDR::Device& dev;
  SoapySDR::Stream* stream = nullptr;
};

// ---------- receiver_impl ----------

class radio_soapy_gateway::receiver_impl : public baseband_gateway_receiver
{
public:
  receiver_impl(SoapySDR::Device& dev_, SoapySDR::Stream* stream_, double fs_) :
    dev(dev_), stream(stream_), sampling_rate_hz(fs_)
  {
  }

  metadata receive(baseband_gateway_buffer_writer& data) override
  {
    metadata md{};

    auto     view        = data.get_channel_buffer(0);
    unsigned nof_samples = data.get_nof_samples();

    void* buffs[1] = { static_cast<void*>(view.data()) };

    int       flags   = 0;
    long long time_ns = 0;

    int ret = dev.readStream(stream, buffs, static_cast<int>(nof_samples), flags, time_ns);
    if (ret > 0) {
      // einfache, monotone Sample-Zaehlung als Timestamp
      last_ts += static_cast<baseband_gateway_timestamp>(ret);
    }

    md.ts = last_ts;
    return md;
  }

private:
  SoapySDR::Device&          dev;
  SoapySDR::Stream*          stream           = nullptr;
  double                     sampling_rate_hz = 0.0;
  baseband_gateway_timestamp last_ts          = 0;
};

// ---------- radio_soapy_gateway ----------

radio_soapy_gateway::radio_soapy_gateway(SoapySDR::Device&                  dev_,
                                         double                             sampling_rate_hz_,
                                         const radio_configuration::stream* tx_cfg,
                                         const radio_configuration::stream* rx_cfg,
                                         task_executor&                     async_task_executor,
                                         radio_notification_handler&        notifier) :
  dev(dev_),
  sampling_rate_hz(sampling_rate_hz_)
{
  (void)tx_cfg;
  (void)rx_cfg;
  (void)async_task_executor;
  (void)notifier;

  // aktuell: genau ein TX- und ein RX-Kanal (Index 0)
  std::vector<size_t> tx_channels = {0};
  std::vector<size_t> rx_channels = {0};

  // Format als "CF32" (komplexe float32 Samples)
  tx_stream = dev.setupStream(SOAPY_SDR_TX, "CF32", tx_channels);
  rx_stream = dev.setupStream(SOAPY_SDR_RX, "CF32", rx_channels);

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
  // MTU gibt Anzahl Samples pro readStream() an
  return static_cast<unsigned>(dev.getStreamMTU(rx_stream));
}

void radio_soapy_gateway::start(baseband_gateway_timestamp init_time)
{
  // init_time wird momentan nicht an Soapy weitergereicht (kein Timed-Start)
  (void)init_time;

  if (tx_stream != nullptr) {
    dev.activateStream(tx_stream, 0, 0, 0);
  }
  if (rx_stream != nullptr) {
    dev.activateStream(rx_stream, 0, 0, 0);
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
