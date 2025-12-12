#include "radio_soapy_gateway.hpp"

#include <SoapySDR/Device.hpp>
#include <SoapySDR/Types.hpp>

#include <algorithm>
#include <atomic>
#include <complex>
#include <stdexcept>
#include <vector>

namespace srsran {

// ============================================================================
// transmitter_impl
// ============================================================================

class radio_soapy_gateway::transmitter_impl : public baseband_gateway_transmitter
{
public:
  transmitter_impl(SoapySDR::Device& dev_,
                   SoapySDR::Stream* stream_,
                   std::atomic<bool>& running_) :
    dev(dev_),
    stream(stream_),
    running(running_)
  {
  }

  void transmit(const baseband_gateway_buffer_reader&        data,
                const baseband_gateway_transmitter_metadata& metadata) override
  {
    (void)metadata;

    if (!running.load(std::memory_order_acquire) || stream == nullptr) {
      return;
    }

    const unsigned nof_samples = data.get_nof_samples();
    if (nof_samples == 0) {
      return;
    }

    auto view = data.get_channel_buffer(0);
    if (!view.data()) {
      return;
    }

    tmp_sc16.resize(nof_samples);
    std::copy_n(view.data(), nof_samples, tmp_sc16.data());

    const void* buffs[1] = { static_cast<const void*>(tmp_sc16.data()) };

    int       flags   = 0;
    long long time_ns = 0;

    constexpr long timeout_us = 20000; // 20 ms

    const int ret = dev.writeStream(stream,
                                    buffs,
                                    static_cast<int>(nof_samples),
                                    flags,
                                    time_ns,
                                    timeout_us);

    if (ret == SOAPY_SDR_TIMEOUT) {
      return;
    }

    if (ret < 0) {
      if (!running.load(std::memory_order_acquire)) {
        return;
      }
      // Beim Betrieb lieber tolerieren als aborten
      return;
    }
  }

private:
  SoapySDR::Device& dev;
  SoapySDR::Stream* stream = nullptr;

  std::atomic<bool>& running;

  std::vector<std::complex<int16_t>> tmp_sc16;
};

// ============================================================================
// receiver_impl
// ============================================================================

class radio_soapy_gateway::receiver_impl : public baseband_gateway_receiver
{
public:
  receiver_impl(SoapySDR::Device& dev_,
                SoapySDR::Stream* stream_,
                double            fs_,
                std::atomic<bool>& running_) :
    dev(dev_),
    stream(stream_),
    sampling_rate_hz(fs_),
    running(running_)
  {
  }

  metadata receive(baseband_gateway_buffer_writer& data) override
  {
    metadata md{};
    md.ts = last_ts;

    if (!running.load(std::memory_order_acquire) || stream == nullptr) {
      return md;
    }

    const unsigned req_samples = data.get_nof_samples();
    if (req_samples == 0) {
      return md;
    }

    tmp_sc16.resize(req_samples);

    void*     buffs[1] = { static_cast<void*>(tmp_sc16.data()) };
    int       flags    = 0;
    long long time_ns  = 0;

    constexpr long timeout_us = 20000; // 20 ms

    const int ret = dev.readStream(stream,
                                   buffs,
                                   static_cast<int>(req_samples),
                                   flags,
                                   time_ns,
                                   timeout_us);

    if (ret == SOAPY_SDR_TIMEOUT) {
      return md;
    }

    if (ret < 0) {
      if (!running.load(std::memory_order_acquire)) {
        return md;
      }
      throw std::runtime_error("Soapy readStream failed with code " + std::to_string(ret));
    }

    const unsigned nread = static_cast<unsigned>(ret);
    const unsigned ncopy = std::min(nread, req_samples);

    auto view = data.get_channel_buffer(0);
    if (!view.data()) {
      return md;
    }

    std::copy_n(tmp_sc16.data(), ncopy, view.data());

    last_ts += ncopy;
    md.ts = last_ts;
    return md;
  }

private:
  SoapySDR::Device& dev;
  SoapySDR::Stream* stream = nullptr;

  double sampling_rate_hz = 0.0;

  std::atomic<bool>& running;

  baseband_gateway_timestamp last_ts = 0;

  std::vector<std::complex<int16_t>> tmp_sc16;
};

// ============================================================================
// radio_soapy_gateway
// ============================================================================

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

  std::vector<size_t> channels = {0};

  // Soapy expects format as STRING, e.g. "CS16" or "CF32"
  tx_stream = dev.setupStream(SOAPY_SDR_TX, "CS16", channels);
  rx_stream = dev.setupStream(SOAPY_SDR_RX, "CS16", channels);

  tx = std::make_unique<transmitter_impl>(dev, tx_stream, running);
  rx = std::make_unique<receiver_impl>(dev, rx_stream, sampling_rate_hz, running);
}

radio_soapy_gateway::~radio_soapy_gateway()
{
  stop();

  if (tx_stream) {
    dev.closeStream(tx_stream);
    tx_stream = nullptr;
  }
  if (rx_stream) {
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

  running.store(true, std::memory_order_release);

  if (tx_stream) {
    dev.activateStream(tx_stream, 0, 0, 0);
  }
  if (rx_stream) {
    dev.activateStream(rx_stream, 0, 0, 0);
  }
}

void radio_soapy_gateway::stop()
{
  running.store(false, std::memory_order_release);

  if (tx_stream) {
    dev.deactivateStream(tx_stream, 0, 0);
  }
  if (rx_stream) {
    dev.deactivateStream(rx_stream, 0, 0);
  }
}

} // namespace srsran
