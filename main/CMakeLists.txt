idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES w5500_driver
    PRIV_REQUIRES spi_flash
)
