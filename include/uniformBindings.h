//
// Created by Stefan Balta on 2026-08-23.
//

#ifndef PIXELKILN_UNIFORMBINDINGS_H
#define PIXELKILN_UNIFORMBINDINGS_H
#include <vector>

enum UniformBindingType {
    UNIFORM_BINDING_TYPE_BUFFER,
    UNIFORM_BINDING_TYPE_SAMPLER,
    UNIFORM_BINDING_TYPE_STORAGE_IMAGE,
};

struct UniformBindings {
    std::vector<UniformBindingType> uniformBindings;
};

#endif //PIXELKILN_UNIFORMBINDINGS_H
