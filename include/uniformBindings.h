//
// Created by Stefan Balta on 2026-08-23.
//

#ifndef PIXELKILN_UNIFORMBINDINGS_H
#define PIXELKILN_UNIFORMBINDINGS_H
#include <vector>

// Index in UniformBindings == binding number in descriptor set 0.
enum UniformBindingType {
    UNIFORM_BINDING_TYPE_BUFFER,         // small uniform buffer, bytes passed inline in the call
    UNIFORM_BINDING_TYPE_SAMPLER,        // combined image sampler over a kiln image
    UNIFORM_BINDING_TYPE_STORAGE_IMAGE,  // kiln image
    UNIFORM_BINDING_TYPE_STORAGE_BUFFER, // kiln buffer
    UNIFORM_BINDING_TYPE_EMPTY,          // hole, no binding at this index
    UNIFORM_BINDING_TYPE_COUNT
};

typedef std::vector<UniformBindingType> UniformBindings;

#endif //PIXELKILN_UNIFORMBINDINGS_H
