#pragma once

// This is a wrapper to handle the C/C++ keyword conflict with "namespace"
// The wlr-layer-shell protocol defines a parameter named "namespace" which is a C++ keyword

// Define a temporary macro to replace "namespace" with "namespace_" in the header
#define namespace namespace_

// Include the generated protocol headers
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

// Undefine the macro to avoid affecting other code
#undef namespace

// Define a wrapper function for cleaner usage
static inline struct zwlr_layer_surface_v1 *
zwlr_layer_shell_get_layer_surface_wrapper(struct zwlr_layer_shell_v1 *shell,
                                          struct wl_surface *surface,
                                          struct wl_output *output,
                                          uint32_t layer,
                                          const char *app_id)
{
    return zwlr_layer_shell_v1_get_layer_surface(shell, surface, output, layer, app_id);
}
