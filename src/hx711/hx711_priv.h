/* Internals shared inside the component. Not part of the public API. */
#pragma once

#include "loadcell_priv.h"
#include "hx711.h"

struct hx711_t {
    struct loadcell_dev cell;   /* the category view; offset, scale and
                                 * saturated live here; priv points back */
    struct loadcell_pin dout;   /* the chip drives this */
    struct loadcell_pin sck;    /* the driver drives this */
    int   gain;             /* 128, 64 or 32 */
};
