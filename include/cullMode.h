//
// Created by Stefan Balta on 2026-09-21.
//

#ifndef PIXELKILN_CULLMODE_H
#define PIXELKILN_CULLMODE_H

// Front faces are counter-clockwise.
enum CullMode {
    CULL_MODE_NONE,
    CULL_MODE_BACK,
    CULL_MODE_FRONT,
    CULL_MODE_COUNT
};

#endif //PIXELKILN_CULLMODE_H
