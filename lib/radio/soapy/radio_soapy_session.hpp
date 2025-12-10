#pragma once

#include "srsran/radio/radio_session.h"
#include "srsran/radio/radio_configuration.h"
#include "srsran/radio/radio_notification_handler.h"
#include "srsran/support/executors/task_executor.h"

#include <memory>
#include <vector>

namespace SoapySDR {
class Device;
class Stream;
} // namespace SoapySDR

namespace srsran {

class radio_soapy_gateway; // Forward-Declaration fuer den Moment

class radio_session_soapy_impl : public radio_session
{
public:
  radio_session_soapy_impl(const radio_configuration::radio& config,
                           task_executor&                    async_task_executor,
                           radio_notification_handler&       notifier);

  ~radio_session_soapy_impl() override;

  // radio_session interface
  radio_management_plane&    get_management_plane() override;
  baseband_gateway&          get_baseband_gateway(unsigned stream_id) override;
  baseband_gateway_timestamp read_current_time() override;
  void                       start(baseband_gateway_timestamp init_time) override;
  void                       stop() override;

private:
  std::unique_ptr<SoapySDR::Device>              dev;
  std::vector<std::unique_ptr<radio_soapy_gateway>> gateways;
  std::unique_ptr<radio_management_plane>        mgmt;
};

} // namespace srsran
