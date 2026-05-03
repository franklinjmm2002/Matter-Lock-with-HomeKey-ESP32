idf_component_register(
  SRC_DIRS "src"
  INCLUDE_DIRS "include"
  REQUIRES driver loggable pn532_cxx fmt
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC "-DLOG_LOCAL_LEVEL=ESP_LOG_VERBOSE")
