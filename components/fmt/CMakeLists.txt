idf_component_register(INCLUDE_DIRS "include")

target_compile_definitions(${COMPONENT_LIB} INTERFACE FMT_HEADER_ONLY=1)
