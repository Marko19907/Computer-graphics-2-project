#pragma once

#include <string>
#include "mesh.h"

// Loads an OBJ file from the given path and returns a Mesh.
Mesh loadOBJ(std::string const &filename);
