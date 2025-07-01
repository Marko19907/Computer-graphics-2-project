#version 430 core

#define MAX_LIGHTS 64

//
// Scene constants
//
const vec3 AMBIENT_CONTRIBUTION  = vec3(0.1, 0.1, 0.1);

const vec3 SPECULAR_COLOR = vec3(1.0, 1.0, 1.0); // Specular highlights color
const vec3 EMISSION_COLOR = vec3(0.0, 0.0, 0.0); // Non-zero will make the object glow

const float SHININESS_FACTOR_DEFAULT = 32.0; // Default shininess factor if no normal map

// Ball properties
const float BALL_RADIUS = 3.0; // Hardcoded for now I guess
const float SOFT_SHADOW_EXTRA_RADIUS = 0.2; // Extra radius for soft shadows

// Attenuation constants, just random values for now, looks good enough :)
const float ATTENUATION_CONST_BASE      = 0.0011; // constant term
const float ATTENUATION_CONST_LINEAR    = 0.0014; // linear term
const float ATTENUATION_CONST_QUADRATIC = 0.0038; // quadratic term


//
// Shader inputs
//
struct LightSource {
    vec3 position;
    vec3 color;
};

uniform LightSource lights[MAX_LIGHTS];
uniform int numLights;


uniform vec3 cameraPosition;
uniform vec3 ballCenter;

in layout(location = 0) vec3 normal_in;
in layout(location = 1) vec2 textureCoordinates;
in layout(location = 2) vec3 fragPos_in;
in layout(location = 3) mat3 TBN;

// Normal mapping stuff
uniform bool useNormalMap;
uniform sampler2D diffuseMap;
uniform sampler2D normalMap;
uniform sampler2D roughnessMap;


//
// Fragment output
//
out vec4 color;

float rand(vec2 co) { return fract(sin(dot(co.xy, vec2(12.9898,78.233))) * 43758.5453); }
float dither(vec2 uv) { return (rand(uv)*2.0-1.0) / 256.0; }

// Attenuation function
float attenuation(float distance) {
    return 1.0 / (ATTENUATION_CONST_BASE + ATTENUATION_CONST_LINEAR * distance + ATTENUATION_CONST_QUADRATIC * distance * distance);
}

// Reject function given in the assignment
vec3 reject(vec3 from, vec3 onto) {
    return from - onto * dot(from, onto) / dot(onto, onto);
}

// Returns a shadow factor in [0,1] where 1 means full shadow, 0 no shadow.
float shadowFactor(vec3 fragPos, vec3 lightPos, vec3 ballCenter, float ballRadius) {
    // Compute vectors from the fragment to the light and to the sphere’s center.
    vec3 toLight = lightPos - fragPos;
    vec3 toBall  = ballCenter - fragPos;

    // If the sphere is behind the fragment or if the light is closer than the sphere, no shadow.
    if (dot(toLight, toBall) < 0.0 || length(toLight) < length(toBall)) {
        return 0.0;
    }

    vec3 offset = reject(toBall, toLight);
    float distToRay = length(offset);

    // Define the hard (ballRadius) and soft shadow radii.
    float hardShadowRadius = ballRadius;
    float softShadowRadius = ballRadius + SOFT_SHADOW_EXTRA_RADIUS;

    if (distToRay < hardShadowRadius) {
        return 1.0; // We're inside the hard shadow area -> full shadow
    }
    else if (distToRay >= softShadowRadius) {
        return 0.0; // We're outside the soft shadow area -> no shadow
    }
    else {
        // Otherwise, linearly interpolate between full and no shadow.
        float t = (softShadowRadius - distToRay) / (softShadowRadius - hardShadowRadius);
        return clamp(t, 0.0, 1.0);
    }
}

void main()
{
    // For safety, re-normalize the normal once more
    vec3 surfaceNormal = normalize(normal_in);

    // If using normal map, override the normal_in
    if (useNormalMap) {
        // Sample from the normal map
        vec3 normalMapValue = texture(normalMap, textureCoordinates).rgb;

        // Convert from [0,1] to [-1,1]
        normalMapValue = normalMapValue * 2.0 - 1.0;

        // Transform it by TBN
        surfaceNormal = normalize(TBN * normalMapValue);

        // Debug: show the TBN-transformed normal
        // vec3 debugNormal = 0.5 * (TBN * (texture(normalMap, textureCoordinates).xyz * 2.0 - 1.0) + 1.0);
        // color = vec4(debugNormal, 1.0);
        // return;
    }

    // Start with the ambient color
    vec3 ambientContribution = AMBIENT_CONTRIBUTION;

    // We'll sum up all the diffuse + specular from each light
    vec3 diffuseSum  = vec3(0.0);
    vec3 specularSum = vec3(0.0);

    // Prepare the shininess factor
    float shininessFactor = SHININESS_FACTOR_DEFAULT;
    if (useNormalMap) {
        float roughness = texture(roughnessMap, textureCoordinates).r;
        shininessFactor = 5.0 / max(roughness * roughness, 1e-8); // Avoid division by zero by adding a small epsilon
    }

    // The view (eye) direction: from fragment to camera
    vec3 viewDirection = normalize(cameraPosition - fragPos_in);

    // Loop over all lights
    for (int i = 0; i < numLights; i++) {
        vec3 lightPosition = lights[i].position;
        vec3 lightColor = lights[i].color;

        // Compute the shadow factor (1 = fully shadowed, 0 = no shadow)
        float shadow = shadowFactor(fragPos_in, lightPosition, ballCenter, BALL_RADIUS);

        // Vector from fragment to light
        vec3  vectorToLight   = lightPosition - fragPos_in;
        float distanceToLight = length(vectorToLight);
        vec3  lightDirection  = normalize(vectorToLight);

        // Attenuation factor
        float attenuationFactor = attenuation(distanceToLight);

        // Diffuse
        float diffuseFactor = max(dot(surfaceNormal, lightDirection), 0.0);

        // Specular
        // reflect(I, N) expects I pointing *into* the surface, so pass -lightDirection
        vec3  reflectionDirection = reflect(-lightDirection, surfaceNormal);
        float specularFactor      = pow(max(dot(reflectionDirection, viewDirection), 0.0), shininessFactor);

        // Accumulate
        diffuseSum  += (1.0 - shadow) * diffuseFactor * lightColor * attenuationFactor;
        specularSum += (1.0 - shadow) * specularFactor * lightColor * attenuationFactor;
    }

    // Add them all up + emission
    vec3 finalColor = ambientContribution + diffuseSum + specularSum + EMISSION_COLOR;

    // If using normal map, multiply by the diffuse map color
    if (useNormalMap) {
        finalColor *= texture(diffuseMap, textureCoordinates).rgb;
    }

    // Output final color
    color = vec4(finalColor, 1.0);

    // Add some dithering noise
    float noise = dither(textureCoordinates);
    color.rgb += noise;
}