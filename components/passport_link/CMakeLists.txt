idf_component_register(
    SRCS "src/passport_link_protocol.c"
         "src/passport_link_ble.c"
    INCLUDE_DIRS "include"
    REQUIRES passport_core
    PRIV_REQUIRES bt
)
