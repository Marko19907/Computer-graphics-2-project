#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#include "objLoader.h"
#include <iostream>
#include <glm/glm.hpp>

Mesh loadOBJ(std::string const &filename) {
    Mesh mesh;
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    // Load the OBJ file
    bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filename.c_str());
    if (!warn.empty()) {
        std::cout << "OBJ Loader warning: " << warn << std::endl;
    }
    if (!err.empty()) {
        std::cerr << "OBJ Loader error: " << err << std::endl;
    }
    if (!ret) {
        std::cerr << "Failed to load/parse .obj file: " << filename << std::endl;
        return mesh;
    }

    // Loop through each shape (group in the OBJ)
    for (size_t s = 0; s < shapes.size(); s++) {
        // Loop through each index of the shape
        for (size_t i = 0; i < shapes[s].mesh.indices.size(); i++) {
            tinyobj::index_t idx = shapes[s].mesh.indices[i];

            // Vertex positions
            glm::vec3 vertex;
            vertex.x = attrib.vertices[3 * idx.vertex_index + 0];
            vertex.y = attrib.vertices[3 * idx.vertex_index + 1];
            vertex.z = attrib.vertices[3 * idx.vertex_index + 2];
            mesh.vertices.push_back(vertex);

            // Normals
            if (!attrib.normals.empty() && idx.normal_index >= 0) {
                glm::vec3 normal;
                normal.x = attrib.normals[3 * idx.normal_index + 0];
                normal.y = attrib.normals[3 * idx.normal_index + 1];
                normal.z = attrib.normals[3 * idx.normal_index + 2];
                mesh.normals.push_back(normal);
            } else {
                // If no normals are provided, push a default value
                mesh.normals.push_back(glm::vec3(0.0f, 0.0f, 0.0f));
            }

            // Texture coordinates
            if (!attrib.texcoords.empty() && idx.texcoord_index >= 0) {
                glm::vec2 texCoord;
                texCoord.x = attrib.texcoords[2 * idx.texcoord_index + 0];
                texCoord.y = attrib.texcoords[2 * idx.texcoord_index + 1];
                mesh.textureCoordinates.push_back(texCoord);
            } else {
                // Default tex coord if not provided
                mesh.textureCoordinates.push_back(glm::vec2(0.0f, 0.0f));
            }

            // Because we're building a new vertex for every index,
            // the indices become sequential.
            mesh.indices.push_back(static_cast<unsigned int>(mesh.indices.size()));
        }
    }
    return mesh;
}
