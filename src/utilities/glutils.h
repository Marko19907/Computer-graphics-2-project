#pragma once

#include "mesh.h"
#include "imageLoader.hpp"

unsigned int generateBuffer(Mesh &mesh);

unsigned int createTexture(PNGImage &image);

void computeTangentsAndBitangents(Mesh& mesh);
