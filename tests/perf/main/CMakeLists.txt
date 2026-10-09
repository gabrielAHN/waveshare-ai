set(HOME_SOURCE_ROOT "$ENV{HOME_SOURCE_ROOT}")
if(NOT HOME_SOURCE_ROOT)
  message(FATAL_ERROR "Set HOME_SOURCE_ROOT to immutable b589 or the candidate worktree")
endif()
set(DEVICE_FIRMWARE "${HOME_SOURCE_ROOT}/devices/waveshare-esp32-s3-touch-amoled-1-8-v2/firmware")
idf_component_register(
  SRCS "../home_renderer_bench.c" "${DEVICE_FIRMWARE}/components/qrcodegen/qrcodegen.c"
  INCLUDE_DIRS
    "${DEVICE_FIRMWARE}/main"
    "${HOME_SOURCE_ROOT}/plugins/hermes/firmware"
    "${HOME_SOURCE_ROOT}/plugins/home_assistant/firmware"
    "${DEVICE_FIRMWARE}/components/qrcodegen"
  REQUIRES esp_timer esp_hw_support
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Werror -Wframe-larger-than=4096)
