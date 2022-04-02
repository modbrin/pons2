#pragma once

#include <cstdint>
#include <vector>

#include "common.h"

const std::vector<Vertex> mockVertices = {{{-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}},
                                          {{0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}},
                                          {{0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}},
                                          {{-0.5f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}}};

const std::vector<uint16_t> mockIndices = {0, 1, 2, 2, 3, 0};