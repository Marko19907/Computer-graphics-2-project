#version 430 core

in layout(location = 0) vec3 position;
in layout(location = 1) vec3 normal_in;
in layout(location = 2) vec2 textureCoordinates_in;

// MVP, Model, and NormalMatrix from the CPU
uniform layout(location = 3) mat4 MVP;
uniform layout(location = 4) mat4 Model;
uniform layout(location = 5) mat3 normalMatrix;

// Tangent and Bitangent from the CPU
layout(location = 6) in vec3 tangent_in;
layout(location = 7) in vec3 bitangent_in;


out layout(location = 0) vec3 normal_out;
out layout(location = 1) vec2 textureCoordinates_out;
out layout(location = 2) vec3 fragPos_out;
out layout(location = 3) mat3 TBN;


void main()
{
    // Transform the position to clip space
    gl_Position = MVP * vec4(position, 1.0);

    // Transform the normal properly (inverse-transpose of the top-left 3x3 of Model)
    normal_out = normalize(normalMatrix * normal_in);

    // Create the TBN matrix and pass it to the fragment shader
    vec3 N = normalize(normalMatrix * normal_in);
    vec3 T = normalize(normalMatrix * tangent_in);
    vec3 B = normalize(normalMatrix * bitangent_in);

    TBN = mat3(T, B, N);

    // Pass through texture coordinates
    textureCoordinates_out = textureCoordinates_in;

    // Compute and pass through the world position
    vec4 worldPos = Model * vec4(position, 1.0);
    fragPos_out   = worldPos.xyz;
}
