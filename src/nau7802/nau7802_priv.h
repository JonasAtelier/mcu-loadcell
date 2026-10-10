/* Internals shared inside the component. Not part of the public API. */
#pragma once

#include "loadcell_priv.h"
#include "nau7802.h"

struct nau7802_t {
    struct loadcell_dev cell;   /* the category view; offset, scale and
                                 * saturated live here; priv points back */
    struct loadcell_bus bus;
    int     gain;
    int     rate;
    int     channel;      /* 1 or 2 */
};
