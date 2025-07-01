#include <glad/glad.h>
#include <program.hpp>
#include "glutils.h"
#include <vector>
#include "imageLoader.hpp"

template <class T>
unsigned int generateAttribute(int id, int elementsPerEntry, std::vector<T> data, bool normalize) {
    unsigned int bufferID;
    glGenBuffers(1, &bufferID);
    glBindBuffer(GL_ARRAY_BUFFER, bufferID);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(T), data.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(id, elementsPerEntry, GL_FLOAT, normalize ? GL_TRUE : GL_FALSE, sizeof(T), 0);
    glEnableVertexAttribArray(id);
    return bufferID;
}

unsigned int generateBuffer(Mesh &mesh) {
    unsigned int vaoID;
    glGenVertexArrays(1, &vaoID);
    glBindVertexArray(vaoID);

    // Positions -> location = 0
    generateAttribute(0, 3, mesh.vertices, false);

    // Normals -> location = 1
    if (mesh.normals.size() > 0) {
        generateAttribute(1, 3, mesh.normals, true);
    }
    // UVs -> location = 2
    if (mesh.textureCoordinates.size() > 0) {
        generateAttribute(2, 2, mesh.textureCoordinates, false);
    }
    // Tangents -> location = 6
    if (mesh.tangents.size() > 0) {
        generateAttribute(6, 3, mesh.tangents, false);
    }
    // Bitangents -> location = 7
    if (mesh.bitangents.size() > 0) {
        generateAttribute(7, 3, mesh.bitangents, false);
    }

    unsigned int indexBufferID;
    glGenBuffers(1, &indexBufferID);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBufferID);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size() * sizeof(unsigned int), mesh.indices.data(), GL_STATIC_DRAW);

    return vaoID;
}

unsigned int createTexture(PNGImage &image) {
    unsigned int textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    // Copy pixel data into GPU memory
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.pixels.data());

    // Generate mipmaps
    glGenerateMipmap(GL_TEXTURE_2D);

    // Set filtering
    // When texture is seen at smaller sizes (MINify), use trilinear filtering:
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    // When the texture is seen at larger sizes (MAGnify), use linear filtering:
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Unbind
    glBindTexture(GL_TEXTURE_2D, 0);

    return textureID;
}

/**
 * Calculate tangents and bitangents for the given mesh.
 * Inspired by: http://www.opengl-tutorial.org/intermediate-tutorials/tutorial-13-normal-mapping/
 *
 * @param mesh The mesh to calculate tangents and bitangents for
 */
void computeTangentsAndBitangents(Mesh& mesh) {
    // Initialize
    mesh.tangents.resize(mesh.vertices.size(), glm::vec3(0.0f));
    mesh.bitangents.resize(mesh.vertices.size(), glm::vec3(0.0f));

    for (size_t i = 0; i < mesh.indices.size(); i += 3) {
        unsigned int i0 = mesh.indices[i + 0];
        unsigned int i1 = mesh.indices[i + 1];
        unsigned int i2 = mesh.indices[i + 2];

        // Positions
        glm::vec3 p0 = mesh.vertices[i0];
        glm::vec3 p1 = mesh.vertices[i1];
        glm::vec3 p2 = mesh.vertices[i2];

        // UVs
        glm::vec2 uv0 = mesh.textureCoordinates[i0];
        glm::vec2 uv1 = mesh.textureCoordinates[i1];
        glm::vec2 uv2 = mesh.textureCoordinates[i2];

        glm::vec3 e1 = p1 - p0;
        glm::vec3 e2 = p2 - p0;

        glm::vec2 dUV1 = uv1 - uv0;
        glm::vec2 dUV2 = uv2 - uv0;

        float f = 1.0f / (dUV1.x * dUV2.y - dUV2.x * dUV1.y + 1e-8); // add small offset to avoid /0

        glm::vec3 tangent = f * ( e1 * dUV2.y - e2 * dUV1.y );
        glm::vec3 bitangent = f * (-e1 * dUV2.x + e2 * dUV1.x);

        // Accumulate per vertex
        mesh.tangents[i0] += tangent;
        mesh.tangents[i1] += tangent;
        mesh.tangents[i2] += tangent;

        mesh.bitangents[i0] += bitangent;
        mesh.bitangents[i1] += bitangent;
        mesh.bitangents[i2] += bitangent;
    }

    // Normalize
    for (size_t i = 0; i < mesh.vertices.size(); i++) {
        mesh.tangents[i]   = glm::normalize(mesh.tangents[i]);
        mesh.bitangents[i] = glm::normalize(mesh.bitangents[i]);
    }
}
