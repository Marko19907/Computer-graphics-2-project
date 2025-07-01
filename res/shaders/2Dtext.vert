#version 430 core

layout(location = 0) in vec3 inPosition;
layout(location = 2) in vec2 inTexCoord;

out vec2 texCoord;

uniform mat4 MVP;  // orthographic transform for 2D

void main() {
    gl_Position = MVP * vec4(inPosition, 1.0);
    texCoord = inTexCoord;
}
