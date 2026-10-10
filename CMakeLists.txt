# ESP-IDF only. stm-forge never reads this file: it compiles every .c under
# src/, chip folders included, puts src/ on the include path of this
# library's sources and adds include/ -- which is why each port file is empty
# on the other platform. Here the chip files are listed one by one, and
# PRIV_INCLUDE_DIRS "src" lets a chip folder reach loadcell_priv.h by plain
# name.
#
# The core is always built, even with CONFIG_LOADCELL off; each chip driver
# only when its Kconfig option is on. Kconfig values are available here during
# the build phase, and only SRCS depends on them -- never REQUIRES, which IDF
# resolves before sdkconfig. That is also why LOADCELL off still builds the
# core: an empty SRCS would need a second, differently-shaped register call,
# and an unreferenced core costs nothing once --gc-sections has run.
#
# REQUIRES driver, not esp_driver_i2c/gpio: the split components do not exist
# on IDF 5.2, and driver still pulls them in on later versions.
set(srcs "src/loadcell.c" "src/loadcell_log.c" "src/loadcell_port_esp.c")

if(CONFIG_LOADCELL AND NOT (CONFIG_LOADCELL_HX711 OR CONFIG_LOADCELL_NAU7802))
    message(WARNING "loadcell: CONFIG_LOADCELL is on but no chip driver is "
                    "selected; set CONFIG_LOADCELL_<CHIP>=y "
                    "(esp-forge: LOADCELL_<CHIP> = y)")
endif()

if(CONFIG_LOADCELL_HX711)
    list(APPEND srcs "src/hx711/hx711.c")
endif()
if(CONFIG_LOADCELL_NAU7802)
    list(APPEND srcs "src/nau7802/nau7802.c")
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "src"
                       REQUIRES driver
                       PRIV_REQUIRES esp_timer esp_rom)
