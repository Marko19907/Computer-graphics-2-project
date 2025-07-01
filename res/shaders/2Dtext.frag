#version 430 core

in vec2 texCoord;
out vec4 FragColor;

uniform sampler2D textSampler;

void main() {
    FragColor = texture(textSampler, texCoord);

    // Output red color for debugging
    // FragColor = vec4(1.0, 0.0, 0.0, 1.0);
}
