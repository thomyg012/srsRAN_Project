# lib/radio/soapy/CMakeLists.txt

# SoapySDR finden – SoapySDR installiert SoapySDRConfig.cmake ins Sysroot
find_package(SoapySDR REQUIRED)

add_library(srsran_radio_soapy SHARED
    radio_soapy_factory.cpp
    radio_soapy_session.cpp
    radio_soapy_gateway.cpp
)

set_target_properties(srsran_radio_soapy PROPERTIES
    OUTPUT_NAME "srsran_radio_soapy"
)

target_link_libraries(srsran_radio_soapy
    srsran_radio             # Interfaces (radio_factory, radio_session, baseband_gateway, ...)
    srsran_support
    srsvec
    srslog
    SoapySDR
)

install(TARGETS srsran_radio_soapy
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
