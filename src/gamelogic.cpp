#include <algorithm>
#include <chrono>
#include <GLFW/glfw3.h>
#include <glad/glad.h>
#include <utilities/shader.hpp>
#include <glm/vec3.hpp>
#include <iostream>
#include <utilities/timeutils.h>
#include <utilities/mesh.h>
#include <utilities/shapes.h>
#include <utilities/glutils.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <fmt/format.h>
#include "gamelogic.h"
#include "sceneGraph.hpp"
#include "utilities/objLoader.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/transform.hpp>

#include "utilities/imageLoader.hpp"
#include "utilities/glfont.h"

enum KeyFrameAction {
    BOTTOM, TOP
};

#include <timestamps.h>
#include <unordered_map>
#include "utilities/camera.hpp"

double padPositionX = 0;
double padPositionZ = 0;

unsigned int currentKeyFrame = 0;
unsigned int previousKeyFrame = 0;

SceneNode* rootNode;
SceneNode* boxNode;
SceneNode* ballNode;
SceneNode* padNode;

SceneNode* trophySimpleNode;

double ballRadius = 3.0f;

// These are heap allocated, because they should not be initialised at the start of the program
Gloom::Shader* shader;
Gloom::Shader* shader2D;
Gloom::Shader* computeShader;

unsigned int rayTracedTexture;
unsigned int fullScreenQuadVAO; // for drawing a full-screen quad
bool rtEnabled = true;  // Ray tracing enabled by default, bool to track/toggle it

// Global camera pointer
glm::vec3 initialCameraPosition = glm::vec3(0, 2, -20);
Gloom::Camera* freeCam = new Gloom::Camera(initialCameraPosition, 8.0f, 0.005f);
bool freeCameraActive = false;


// Struct to hold the data for the light sources for the GPU
struct LightSourceData {
    glm::vec3 position;
    glm::vec3 color;
};

static std::vector<LightSourceData> lightsData;

struct Triangle {
    // vec3 + padding to align to 16 bytes due to std140 layout
    // v is world space vertex, n is normal
    glm::vec3 v0; float pad0;
    glm::vec3 v1; float pad1;
    glm::vec3 v2; float pad2;
    glm::vec3 n0; float pad3;
    glm::vec3 n1; float pad4;
    glm::vec3 n2; float pad5;
    unsigned int materialID; float pad6; float pad7; float pad8;
};


static std::vector<Triangle> allTriangles;
static std::unordered_map<int, Mesh> gMeshByVao;

struct Material {
    glm::vec3 baseColor; // 12 bytes; will be padded to 16 bytes
    float pad0;         // padding so that baseColor occupies 16 bytes

    float roughness;    // 4 bytes
    float reflectivity; // 4 bytes
    float pad1;         // 4 bytes
    float pad2;         // 4 bytes
};
// Total: 32 bytes per Material.

// Materials, indexed by ID, default material is 0
std::vector<Material> gMaterials = {
    {
        glm::vec3(1.0f, 1.0f, 1.0f), // white
        0,    // padding
        0.8f, // roughness
        0.8f  // reflectivity
    }
};

void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    // Toggle ray tracing on 'R' press
    if (key == GLFW_KEY_R && action == GLFW_PRESS) {
        rtEnabled = !rtEnabled;
        if (rtEnabled)
            std::cout << "Ray tracing ENABLED\n";
        else
            std::cout << "Ray tracing DISABLED\n";
    }

    // Toggle trophy on 'T' press
    if (key == GLFW_KEY_T && action == GLFW_PRESS) {
        // Check if trophySimpleNode is currently a child of rootNode
        auto it = std::find(rootNode->children.begin(), rootNode->children.end(), trophySimpleNode);
        if (it != rootNode->children.end()) {
            // Remove trophy from the scene
            rootNode->children.erase(it);
            std::cout << "Trophy removed" << std::endl;
        }
        else {
            // Add trophy back to the scene
            addChild(rootNode, trophySimpleNode);
            std::cout << "Trophy added" << std::endl;
        }
    }

    // Toggle free camera on 'C' press
    if (key == GLFW_KEY_C && action == GLFW_PRESS)
    {
        freeCameraActive = !freeCameraActive;
        if (freeCameraActive)
            std::cout << "Free camera ENABLED\n";
        else
            std::cout << "Free camera DISABLED\n";
    }

    // Pass inputs to the freeCam only if the free camera is active
    if (freeCameraActive)
    {
        freeCam->handleKeyboardInputs(key, action);
    }
}

GLuint triangleSSBO = 0;
GLuint materialSSBO = 0;

const glm::vec3 boxDimensions(180, 90, 90);
const glm::vec3 padDimensions(30, 3, 40);

glm::vec3 ballPosition(0, ballRadius + padDimensions.y, boxDimensions.z / 2);
glm::vec3 ballDirection(1, 1, 0.2f);

CommandLineOptions options;

bool hasStarted        = false;
bool hasLost           = false;
bool jumpedToNextFrame = false;
bool isPaused          = false;

bool mouseLeftPressed   = false;
bool mouseLeftReleased  = false;
bool mouseRightPressed  = false;
bool mouseRightReleased = false;

double totalElapsedTime = 0;
double gameElapsedTime  = 0;

double mouseSensitivity = 1.0;
double lastMouseX = windowWidth / 2;
double lastMouseY = windowHeight / 2;

void mouseCallback(GLFWwindow* window, double x, double y) {
    int windowWidth, windowHeight;
    glfwGetWindowSize(window, &windowWidth, &windowHeight);
    glViewport(0, 0, windowWidth, windowHeight);

    double deltaX = x - lastMouseX;
    double deltaY = y - lastMouseY;

    padPositionX -= mouseSensitivity * deltaX / windowWidth;
    padPositionZ -= mouseSensitivity * deltaY / windowHeight;

    if (padPositionX > 1) padPositionX = 1;
    if (padPositionX < 0) padPositionX = 0;
    if (padPositionZ > 1) padPositionZ = 1;
    if (padPositionZ < 0) padPositionZ = 0;

    glfwSetCursorPos(window, windowWidth / 2, windowHeight / 2);
}


void initGame(GLFWwindow* window, CommandLineOptions gameOptions) {
    options = gameOptions;

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
    glfwSetCursorPosCallback(window, mouseCallback);

    shader = new Gloom::Shader();
    shader->makeBasicShader("../res/shaders/simple.vert", "../res/shaders/simple.frag");
    shader->activate();

    // Create meshes
    Mesh pad = cube(padDimensions, glm::vec2(30, 40), true);
    Mesh box = cube(boxDimensions, glm::vec2(90), true, true);
    Mesh sphere = generateSphere(1.0, 40, 40);

    Mesh trophy = loadOBJ("../res/models/trophy.obj");
    Mesh trophySimple = loadOBJ("../res/models/trophy_simple.obj");

    // Generate TBN for the box before sending data to the GPU (in #generateBuffer())
    computeTangentsAndBitangents(box);

    // Fill buffers
    unsigned int ballVAO = generateBuffer(sphere);
    unsigned int boxVAO  = generateBuffer(box);
    unsigned int padVAO  = generateBuffer(pad);

    unsigned int trophyVAO = generateBuffer(trophy);
    unsigned int trophySimpleVAO = generateBuffer(trophySimple);

    // Store the meshes in a map for easy access for ray tracing
    gMeshByVao.insert({ballVAO, sphere});
    gMeshByVao.insert({boxVAO, box});
    gMeshByVao.insert({padVAO, pad});
    gMeshByVao.insert({trophyVAO, trophy});
    gMeshByVao.insert({trophySimpleVAO, trophySimple});

    // Construct scene
    rootNode = createSceneNode();
    boxNode  = createSceneNode();
    padNode  = createSceneNode();
    ballNode = createSceneNode();
    SceneNode* trophyNode = createSceneNode();
    trophySimpleNode = createSceneNode();


    // Create node for the 3 point lights
    SceneNode* sceneLightNode = createSceneNode();
    SceneNode* lightNode1 = createSceneNode();
    SceneNode* padLightNode = createSceneNode();

 
    // Set type to point light and add colors
    for (SceneNode* lightNode : {sceneLightNode, lightNode1, padLightNode}) {
        lightNode->nodeType = POINT_LIGHT;
    }

    // Some nice colors for the lights
    const glm::vec3 colors[3] = {
        {1.0f, 0.5f, 0.5f},  // tinted red
        {0.5f, 1.0f, 0.5f},  // tinted green
        // {0.5f, 0.5f, 1.0f},  // tinted blue
        {1.0f, 1.0f, 1.0f},  // white
    };

    // Set the colors of the lights
    sceneLightNode->lightColor = colors[0];
    lightNode1->lightColor = colors[1];
    padLightNode->lightColor = colors[2];

    // Move the lights to their positions
    lightNode1->position = glm::vec3(-55, -50, -90);
    sceneLightNode->position = glm::vec3(5, -40, -90);

    padLightNode->position = glm::vec3(0, 20, 0);

    // Add the lights to the scene
    addChild(rootNode, sceneLightNode);
    addChild(rootNode, lightNode1);
    addChild(padNode, padLightNode);


    // Prepare the box node
    PNGImage diffuseImage = loadPNGFile("../res/textures/Brick03_col.png");
    unsigned int diffuseTexID = createTexture(diffuseImage);

    PNGImage normalMapImage = loadPNGFile("../res/textures/Brick03_nrm.png");
    unsigned int normalMapTexID = createTexture(normalMapImage);

    PNGImage roughnessMapImage = loadPNGFile("../res/textures/Brick03_rgh.png");
    unsigned int roughnessMapTexID = createTexture(roughnessMapImage);

    boxNode->nodeType = NORMAL_MAPPED_GEOMETRY;
    boxNode->textureID   = diffuseTexID;
    boxNode->normalMapID = normalMapTexID;
    boxNode->roughnessMapID = roughnessMapTexID;


    // Add the nodes to the scene
    rootNode->children.push_back(boxNode);
    rootNode->children.push_back(padNode);
    rootNode->children.push_back(ballNode);

    boxNode->vertexArrayObjectID  = boxVAO;
    boxNode->VAOIndexCount        = box.indices.size();

    padNode->vertexArrayObjectID  = padVAO;
    padNode->VAOIndexCount        = pad.indices.size();

    ballNode->vertexArrayObjectID = ballVAO;
    ballNode->VAOIndexCount       = sphere.indices.size();

    trophyNode->vertexArrayObjectID = trophyVAO;
    trophyNode->VAOIndexCount       = trophy.indices.size();

    trophySimpleNode->vertexArrayObjectID = trophySimpleVAO;
    trophySimpleNode->VAOIndexCount       = trophySimple.indices.size();
    trophySimpleNode->position = glm::vec3(-40, -50, -90);
    trophySimpleNode->scale = glm::vec3(0.35f, 0.35f, 0.35f);

    // addChild(rootNode, trophyNode); // Disabled for now because it's really expensive in the ray tracer, lots of triangles
    addChild(rootNode, trophySimpleNode);


    // Load the compute ray tracing shader
    computeShader = new Gloom::Shader();
    computeShader->attach("../res/shaders/raytracer.comp");
    computeShader->link();
    printf("Loaded compute shader, valid: %d\n", computeShader->isValid());


    // Create output texture for ray tracing (using windowWidth and windowHeight)
    glGenTextures(1, &rayTracedTexture);
    glBindTexture(GL_TEXTURE_2D, rayTracedTexture);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, windowWidth, windowHeight);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Create a full-screen quad for displaying the computed image
    float quadVertices[] = {
        // positions                             // texCoords
        0.0f,               float(windowHeight), 0.0f, 0.0f, 1.0f,
        0.0f,               0.0f,                0.0f, 0.0f, 0.0f,
        float(windowWidth), 0.0f,                0.0f, 1.0f, 0.0f,
        
        0.0f,               float(windowHeight), 0.0f, 0.0f, 1.0f,
        float(windowWidth), 0.0f,                0.0f, 1.0f, 0.0f,
        float(windowWidth), float(windowHeight), 0.0f, 1.0f, 1.0f
    };

    unsigned int quadVBO;
    glGenVertexArrays(1, &fullScreenQuadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(fullScreenQuadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    // Position attribute at location 0: 3 floats per vertex.
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Texture coordinate attribute at location 2: 2 floats, starting after the first 3 floats.
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);


    shader2D = new Gloom::Shader();
    shader2D->makeBasicShader("../res/shaders/2Dtext.vert", "../res/shaders/2Dtext.frag");

    PNGImage charmapImage = loadPNGFile("../res/textures/charmap.png");
    printf("Loaded charmap with dimensions %d x %d\n", charmapImage.width, charmapImage.height);
    unsigned int charmapTexID = createTexture(charmapImage);

    std::string text = "Hello, World!";
    float ratio = 39.0f / 29.0f;  // height / width
    float totalWidth = 200.0f;    // good enough for this text?
    Mesh textMesh = generateTextGeometryBuffer(text, ratio, totalWidth);

    unsigned int textVAO = generateBuffer(textMesh);

    printf("Generated text mesh with texture id %d and VAO id %d\n", charmapTexID, textVAO);

    SceneNode* textNode = new SceneNode();
    textNode->nodeType = GEOMETRY_2D;
    textNode->vertexArrayObjectID = textVAO;
    textNode->VAOIndexCount       = textMesh.indices.size();
    textNode->textureID           = charmapTexID;
    textNode->position            = glm::vec3(windowWidth / 2, windowHeight / 2, 0); // Move it somewhere so it's not in the corner

    addChild(rootNode, textNode);


    // Setup the camera callback
    glfwSetKeyCallback(window, keyCallback);

    getTimeDeltaSeconds();

    std::cout << fmt::format("Initialized scene with {} SceneNodes.", totalChildren(rootNode)) << std::endl;

    std::cout << "Ready. Click to start!" << std::endl;
}

// Recursively gather all triangles in the scene graph into a single vector of triangles for ray tracing
void gatherTriangles(SceneNode* node, std::vector<Triangle>& output)
{
    // If this node has real geometry:
    if (node->nodeType == GEOMETRY || 
        node->nodeType == NORMAL_MAPPED_GEOMETRY) 
    {
        // Get the mesh from the VAO ID
        auto it = gMeshByVao.find(node->vertexArrayObjectID);
        if (it != gMeshByVao.end()) {
            Mesh& mesh = it->second;

            for (size_t i = 0; i < mesh.indices.size(); i += 3) {
                unsigned int i0 = mesh.indices[i + 0];
                unsigned int i1 = mesh.indices[i + 1];
                unsigned int i2 = mesh.indices[i + 2];

                glm::vec3 p0_local = mesh.vertices[i0];
                glm::vec3 p1_local = mesh.vertices[i1];
                glm::vec3 p2_local = mesh.vertices[i2];

                glm::vec3 p0_world = glm::vec3(node->modelMatrix * glm::vec4(p0_local, 1.0));
                glm::vec3 p1_world = glm::vec3(node->modelMatrix * glm::vec4(p1_local, 1.0));
                glm::vec3 p2_world = glm::vec3(node->modelMatrix * glm::vec4(p2_local, 1.0));

                glm::vec3 n0_world(0.0f);
                glm::vec3 n1_world(0.0f);
                glm::vec3 n2_world(0.0f);

                if (!mesh.normals.empty()) {
                    glm::vec3 n0_local = mesh.normals[i0];
                    glm::vec3 n1_local = mesh.normals[i1];
                    glm::vec3 n2_local = mesh.normals[i2];

                    // For correct normal transform, use node->normalMatrix
                    n0_world = glm::normalize(node->normalMatrix * n0_local);
                    n1_world = glm::normalize(node->normalMatrix * n1_local);
                    n2_world = glm::normalize(node->normalMatrix * n2_local);
                }

                Triangle tri;
                tri.v0 = p0_world;
                tri.v1 = p1_world;
                tri.v2 = p2_world;
                tri.n0 = n0_world;
                tri.n1 = n1_world;
                tri.n2 = n2_world;
                tri.materialID = node->materialID;

                output.push_back(tri);
            }
        }
    }

    // Recursively gather from children
    for (SceneNode* child : node->children) {
        gatherTriangles(child, output);
    }
}

void updateFrame(GLFWwindow* window) {
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    double timeDelta = getTimeDeltaSeconds();

    const float ballBottomY = boxNode->position.y - (boxDimensions.y/2) + ballRadius + padDimensions.y;
    const float ballTopY    = boxNode->position.y + (boxDimensions.y/2) - ballRadius;
    const float BallVerticalTravelDistance = ballTopY - ballBottomY;

    const float cameraWallOffset = 30; // Arbitrary addition to prevent ball from going too much into camera

    const float ballMinX = boxNode->position.x - (boxDimensions.x/2) + ballRadius;
    const float ballMaxX = boxNode->position.x + (boxDimensions.x/2) - ballRadius;
    const float ballMinZ = boxNode->position.z - (boxDimensions.z/2) + ballRadius;
    const float ballMaxZ = boxNode->position.z + (boxDimensions.z/2) - ballRadius - cameraWallOffset;

    if (freeCameraActive) {
        // Let the camera update its position/orientation based on keys pressed
        freeCam->updateCamera((float)timeDelta);
    }

    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_1)) {
        mouseLeftPressed = true;
        mouseLeftReleased = false;
    } else {
        mouseLeftReleased = mouseLeftPressed;
        mouseLeftPressed = false;
    }
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_2)) {
        mouseRightPressed = true;
        mouseRightReleased = false;
    } else {
        mouseRightReleased = mouseRightPressed;
        mouseRightPressed = false;
    }

    if (!hasStarted) {
        if (mouseLeftPressed) {
            totalElapsedTime = 0;
            gameElapsedTime = 0;
            hasStarted = true;
        }

        ballPosition.x = ballMinX + (1 - padPositionX) * (ballMaxX - ballMinX);
        ballPosition.y = ballBottomY;
        ballPosition.z = ballMinZ + (1 - padPositionZ) * ((ballMaxZ+cameraWallOffset) - ballMinZ);
    } else {
        totalElapsedTime += timeDelta;
        if (hasLost) {
            if (mouseLeftReleased) {
                hasLost = false;
                hasStarted = false;
                currentKeyFrame = 0;
                previousKeyFrame = 0;
            }
        } else if (isPaused) {
            if (mouseRightReleased) {
                isPaused = false;
            }
        } else {
            gameElapsedTime += timeDelta;
            if (mouseRightReleased) {
                isPaused = true;
            }
            // Get the timing for the beat of the song
            for (unsigned int i = currentKeyFrame; i < keyFrameTimeStamps.size(); i++) {
                if (gameElapsedTime < keyFrameTimeStamps.at(i)) {
                    continue;
                }
                currentKeyFrame = i;
            }

            jumpedToNextFrame = currentKeyFrame != previousKeyFrame;
            previousKeyFrame = currentKeyFrame;

            double frameStart = keyFrameTimeStamps.at(currentKeyFrame);
            double frameEnd = keyFrameTimeStamps.at(currentKeyFrame + 1); // Assumes last keyframe at infinity

            double elapsedTimeInFrame = gameElapsedTime - frameStart;
            double frameDuration = frameEnd - frameStart;
            double fractionFrameComplete = elapsedTimeInFrame / frameDuration;

            double ballYCoord;

            KeyFrameAction currentOrigin = keyFrameDirections.at(currentKeyFrame);
            KeyFrameAction currentDestination = keyFrameDirections.at(currentKeyFrame + 1);

            // Synchronize ball with music
            if (currentOrigin == BOTTOM && currentDestination == BOTTOM) {
                ballYCoord = ballBottomY;
            } else if (currentOrigin == TOP && currentDestination == TOP) {
                ballYCoord = ballBottomY + BallVerticalTravelDistance;
            } else if (currentDestination == BOTTOM) {
                ballYCoord = ballBottomY + BallVerticalTravelDistance * (1 - fractionFrameComplete);
            } else if (currentDestination == TOP) {
                ballYCoord = ballBottomY + BallVerticalTravelDistance * fractionFrameComplete;
            }

            // Make ball move
            const float ballSpeed = 60.0f;
            ballPosition.x += timeDelta * ballSpeed * ballDirection.x;
            ballPosition.y = ballYCoord;
            ballPosition.z += timeDelta * ballSpeed * ballDirection.z;

            // Make ball bounce
            if (ballPosition.x < ballMinX) {
                ballPosition.x = ballMinX;
                ballDirection.x *= -1;
            } else if (ballPosition.x > ballMaxX) {
                ballPosition.x = ballMaxX;
                ballDirection.x *= -1;
            }
            if (ballPosition.z < ballMinZ) {
                ballPosition.z = ballMinZ;
                ballDirection.z *= -1;
            } else if (ballPosition.z > ballMaxZ) {
                ballPosition.z = ballMaxZ;
                ballDirection.z *= -1;
            }

            if (options.enableAutoplay) {
                padPositionX = 1-(ballPosition.x - ballMinX) / (ballMaxX - ballMinX);
                padPositionZ = 1-(ballPosition.z - ballMinZ) / ((ballMaxZ+cameraWallOffset) - ballMinZ);
            }

            // Check if the ball is hitting the pad when the ball is at the bottom.
            // If not, you just lost the game! (hehe)
            if (jumpedToNextFrame && currentOrigin == BOTTOM && currentDestination == TOP) {
                double padLeftX  = boxNode->position.x - (boxDimensions.x/2) + (1 - padPositionX) * (boxDimensions.x - padDimensions.x);
                double padRightX = padLeftX + padDimensions.x;
                double padFrontZ = boxNode->position.z - (boxDimensions.z/2) + (1 - padPositionZ) * (boxDimensions.z - padDimensions.z);
                double padBackZ  = padFrontZ + padDimensions.z;

                if (   ballPosition.x < padLeftX
                    || ballPosition.x > padRightX
                    || ballPosition.z < padFrontZ
                    || ballPosition.z > padBackZ
                ) {
                    hasLost = true;
                }
            }
        }
    }

    glm::mat4 projection = glm::perspective(glm::radians(80.0f), float(windowWidth) / float(windowHeight), 0.1f, 350.f);

    glm::mat4 view;
    if (freeCameraActive) {
        view = freeCam->getViewMatrix();
    }
    else {
        // Original fixed camera transform
        glm::vec3 cameraPosition = glm::vec3(initialCameraPosition);
        float lookRotation = -0.6f / (1.f + exp(-5.f * (padPositionX - 0.5f))) + 0.3f;
        glm::mat4 cameraTransform =
              glm::rotate(0.3f + 0.2f * float(-padPositionZ*padPositionZ), glm::vec3(1, 0, 0))
            * glm::rotate(lookRotation, glm::vec3(0, 1, 0))
            * glm::translate(-cameraPosition);

        view = cameraTransform;
    }

    glm::mat4 VP = projection * view;

    // Calculate camera position
    glm::mat4 invView = glm::inverse(view);
    glm::vec3 cameraPos = glm::vec3(invView[3]); // Extract translation component

    // Upload camera position to the shader
    glUniform3fv(glGetUniformLocation(shader->get(), "cameraPosition"), 1, glm::value_ptr(cameraPos));

    // Move and rotate various SceneNodes
    boxNode->position = { 0, -10, -80 };

    ballNode->position = ballPosition;
    ballNode->scale = glm::vec3(ballRadius);
    ballNode->rotation = { 0, totalElapsedTime*2, 0 };

    // Upload the ball's position to the shader
    GLint loc = glGetUniformLocation(shader->get(), "ballCenter");
    glUniform3fv(loc, 1, glm::value_ptr(ballPosition));

    padNode->position  = {
        boxNode->position.x - (boxDimensions.x/2) + (padDimensions.x/2) + (1 - padPositionX) * (boxDimensions.x - padDimensions.x),
        boxNode->position.y - (boxDimensions.y/2) + (padDimensions.y/2),
        boxNode->position.z - (boxDimensions.z/2) + (padDimensions.z/2) + (1 - padPositionZ) * (boxDimensions.z - padDimensions.z)
    };

    // Clear old contents so we can gather fresh positions
    lightsData.clear();

    // Recompute transformations
    updateNodeTransformations(rootNode, glm::mat4(1.0f), VP);

    allTriangles.clear();
    gatherTriangles(rootNode, allTriangles);

}

void updateNodeTransformations(SceneNode* node, glm::mat4 transformationThusFar, glm::mat4 VP) {
    glm::mat4 localTransform =
              glm::translate(node->position)
            * glm::translate(node->referencePoint)
            * glm::rotate(node->rotation.y, glm::vec3(0,1,0))
            * glm::rotate(node->rotation.x, glm::vec3(1,0,0))
            * glm::rotate(node->rotation.z, glm::vec3(0,0,1))
            * glm::scale(node->scale)
            * glm::translate(-node->referencePoint);

    // Compute the full (cumulative) model matrix
    node->modelMatrix = transformationThusFar * localTransform;

    // compute normalMatrix from modelMatrix
    glm::mat3 normalMatrix = glm::mat3(node->modelMatrix);
    normalMatrix = glm::inverse(normalMatrix);
    normalMatrix = glm::transpose(normalMatrix);
    node->normalMatrix = normalMatrix;

    // Then compute the final MVP by multiplying your VP by the node's model matrix
    node->currentTransformationMatrix = VP * node->modelMatrix;

    // If point light, store info
    if (node->nodeType == POINT_LIGHT) {
        glm::vec3 worldPos = glm::vec3(node->modelMatrix * glm::vec4(0,0,0,1));

        // Create a LightSourceData for this node
        LightSourceData data;
        data.position = worldPos;
        data.color    = node->lightColor;  // taken from the node itself

        // Add it to the global or static lightsData vector
        lightsData.push_back(data);
    }

    switch(node->nodeType) {
        case GEOMETRY: break;
        case POINT_LIGHT: break;
        case SPOT_LIGHT: break;
    }

    for (SceneNode* child : node->children) {
        updateNodeTransformations(child, node->modelMatrix, VP);
    }
}

void renderNode3D(SceneNode* node) {
    glUniformMatrix4fv(3, 1, GL_FALSE, glm::value_ptr(node->currentTransformationMatrix));

    // Upload the Model to uniform location = 4
    glUniformMatrix4fv(4, 1, GL_FALSE, glm::value_ptr(node->modelMatrix));

    // Upload the Normal matrix to uniform location = 5, using glUniformMatrix3fv since it's a mat3
    glUniformMatrix3fv(5, 1, GL_FALSE, glm::value_ptr(node->normalMatrix));

    switch(node->nodeType) {
        case NORMAL_MAPPED_GEOMETRY:
            // Enable normal mapping
            glUniform1i(glGetUniformLocation(shader->get(), "useNormalMap"), 1);
    
            // Bind the diffuse map to texture unit 0
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, node->textureID);
            glUniform1i(glGetUniformLocation(shader->get(), "diffuseMap"), 0);
    
            // Bind the normal map to texture unit 1
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, node->normalMapID);
            glUniform1i(glGetUniformLocation(shader->get(), "normalMap"), 1);

            // Bind the roughness map to texture unit 2
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, node->roughnessMapID);
            glUniform1i(glGetUniformLocation(shader->get(), "roughnessMap"), 2);
            
            // Then draw
            if (node->vertexArrayObjectID != -1) {
                glBindVertexArray(node->vertexArrayObjectID);
                glDrawElements(GL_TRIANGLES, node->VAOIndexCount, GL_UNSIGNED_INT, nullptr);
            }
        
            break;
        case GEOMETRY:
            glUniform1i(glGetUniformLocation(shader->get(), "useNormalMap"), 0);

            if(node->vertexArrayObjectID != -1) {
                glBindVertexArray(node->vertexArrayObjectID);
                glDrawElements(GL_TRIANGLES, node->VAOIndexCount, GL_UNSIGNED_INT, nullptr);
            }
            break;
        case POINT_LIGHT: break;
        case SPOT_LIGHT: break;
    }

    for (SceneNode* child : node->children) {
        renderNode3D(child);
    }
}

void renderNode2D(SceneNode* node, glm::mat4 ortho) {
    if (node->nodeType == GEOMETRY_2D) {
        // Combine orthographic projection with node’s model transform
        glm::mat4 MVP = ortho * node->modelMatrix;

        // Then upload the MVP to the shader
        GLint mvpLoc = glGetUniformLocation(shader2D->get(), "MVP");
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, glm::value_ptr(MVP));

        // Bind the texture to texture unit 0
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, node->textureID);

        // Sampler in the fragment shader is named "textSampler", set it to 0 (the texture unit)
        GLint samplerLoc = glGetUniformLocation(shader2D->get(), "textSampler");
        glUniform1i(samplerLoc, 0);

        // Draw geometry
        glBindVertexArray(node->vertexArrayObjectID);
        glDrawElements(GL_TRIANGLES, node->VAOIndexCount, GL_UNSIGNED_INT, nullptr);
    }

    // Recurse
    for (SceneNode* child : node->children) {
        renderNode2D(child, ortho);
    }
}

void renderFrame(GLFWwindow* window) {
    int windowWidth, windowHeight;
    glfwGetWindowSize(window, &windowWidth, &windowHeight);
    glViewport(0, 0, windowWidth, windowHeight);


    // --- 3D rendering ---
    if (!rtEnabled) {
        shader->activate();

        // Set numLights in the shader
        int numLights = lightsData.size();
        glUniform1i(shader->getUniformFromName("numLights"), numLights);

        // Upload light data to the shader
        for (int i = 0; i < numLights; i++) {
            std::string posName = fmt::format("lights[{}].position", i);
            std::string colName = fmt::format("lights[{}].color", i);

            GLint locPos = shader->getUniformFromName(posName);
            GLint locCol = shader->getUniformFromName(colName);

            glUniform3fv(locPos, 1, glm::value_ptr(lightsData[i].position));
            glUniform3fv(locCol, 1, glm::value_ptr(lightsData[i].color));
        }

        renderNode3D(rootNode);
    }

    // Deactivate the 3D one, might be active even if RT is enabled due to how I've set up the uniform updates in updateFrame()
    // This is a bit of a hack, but it works for now. Revisit later.
    shader->deactivate();


    // --- Ray tracing ---
    if (rtEnabled) {
        computeShader->activate();
    
        glm::mat4 projection = glm::perspective(glm::radians(80.0f),
                                                float(windowWidth) / float(windowHeight),
                                                0.1f, 350.f);
    
        glm::vec3 rtCameraPosition;
        glm::mat4 rtView;
        if (freeCameraActive) {
            // Use the free camera’s view matrix.
            rtView = freeCam->getViewMatrix();

            // Extract the camera position from the inverse of the view matrix.
            glm::mat4 invFreeView = glm::inverse(rtView);
            rtCameraPosition = glm::vec3(invFreeView[3]);
        }
        else {
            // Fixed camera, use the original camera position.
            rtCameraPosition = glm::vec3(initialCameraPosition);
            rtView =
                glm::rotate(glm::mat4(1.0f),
                            0.3f + 0.2f * (-static_cast<float>(padPositionZ * padPositionZ)),
                            glm::vec3(1, 0, 0)) *
                glm::rotate(glm::mat4(1.0f),
                            -0.6f / (1.0f + expf(-5.0f * (static_cast<float>(padPositionX) - 0.5f))) + 0.3f,
                            glm::vec3(0, 1, 0)) *
                glm::translate(glm::mat4(1.0f), -rtCameraPosition);
        }
    
        glm::mat4 invView = glm::inverse(rtView);
        glm::mat4 invProjection = glm::inverse(projection);
    
        // Bind output texture as image unit 0 for write access
        glBindImageTexture(0, rayTracedTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    
        // Set uniforms for the compute shader
        glUniformMatrix4fv(glGetUniformLocation(computeShader->get(), "invProjection"), 1, GL_FALSE, glm::value_ptr(invProjection));
        glUniformMatrix4fv(glGetUniformLocation(computeShader->get(), "invView"), 1, GL_FALSE, glm::value_ptr(invView));
        glUniform3fv(glGetUniformLocation(computeShader->get(), "cameraPosition"), 1, glm::value_ptr(rtCameraPosition));    

        // Set ambient light
        glm::vec3 ambient(0.1f, 0.1f, 0.1f);
        glUniform3fv(glGetUniformLocation(computeShader->get(), "ambientColor"), 1, glm::value_ptr(ambient));

        // Upload multiple lights to the compute shader
        int numLights = lightsData.size();
        glUniform1i(glGetUniformLocation(computeShader->get(), "numLights"), numLights);
        for (int i = 0; i < numLights; i++) {
            std::string posName = fmt::format("lights[{}].position", i);
            std::string colName = fmt::format("lights[{}].color", i);
            glUniform3fv(glGetUniformLocation(computeShader->get(), posName.c_str()), 1, glm::value_ptr(lightsData[i].position));
            glUniform3fv(glGetUniformLocation(computeShader->get(), colName.c_str()), 1, glm::value_ptr(lightsData[i].color));
        }

        if (!triangleSSBO) {
            glGenBuffers(1, &triangleSSBO);
        }
        
        // Upload the data
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, triangleSSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER,
                     allTriangles.size() * sizeof(Triangle),
                     allTriangles.data(),
                     GL_DYNAMIC_DRAW);
        
        // Bind to some binding index for the compute shader
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, triangleSSBO);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        
        // Also pass the count as a uniform
        int triCount = (int) allTriangles.size();
        glUniform1i(glGetUniformLocation(computeShader->get(), "numTriangles"), triCount);

        if (!materialSSBO) {
            glGenBuffers(1, &materialSSBO);
        }

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, materialSSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER,
                     gMaterials.size() * sizeof(Material),
                     gMaterials.data(),
                     GL_STATIC_DRAW);

        // Binding index = 2 must match `layout(std430, binding=2)` in compute
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, materialSSBO);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    
        // Dispatch the compute shader
        GLuint workGroupsX = (windowWidth + 15) / 16;
        GLuint workGroupsY = (windowHeight + 15) / 16;
        glDispatchCompute(workGroupsX, workGroupsY, 1);
    
        // Wait for compute shader to finish writing
        glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
    
        computeShader->deactivate();
    
        // --- Render the output texture to the screen ---
        shader2D->activate();
    
        // Set up an orthographic projection that covers the full screen
        glm::mat4 orthoCS = glm::ortho(0.0f, float(windowWidth), 0.0f, float(windowHeight), -1.0f, 1.0f);
        GLint mvpLocCS = glGetUniformLocation(shader2D->get(), "MVP");
        glUniformMatrix4fv(mvpLocCS, 1, GL_FALSE, glm::value_ptr(orthoCS));
    
        // Bind the ray traced texture to texture unit 0
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, rayTracedTexture);
        GLint samplerLoc = glGetUniformLocation(shader2D->get(), "textSampler");
        glUniform1i(samplerLoc, 0);
    
        // Render the full-screen quad
        glBindVertexArray(fullScreenQuadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
    
        shader2D->deactivate();
    }



    // --- 2D text rendering ---
    shader2D->activate();

    // Orthographic projection for 2D
    glm::mat4 ortho = glm::ortho(
        0.0f, float(windowWidth),  // left, right
        0.0f, float(windowHeight), // bottom, top
        -1.0f, 1.0f                // near, far
    );

    // Vertex shader expects a uniform "MVP", set it
    GLint mvpLoc = glGetUniformLocation(shader2D->get(), "MVP");
    glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, glm::value_ptr(ortho));

    
    glDisable(GL_DEPTH_TEST);
    renderNode2D(rootNode, ortho);
    glEnable(GL_DEPTH_TEST);

    shader2D->deactivate();

    // Re-enable the 3D shader for the next frame so that updateFrame() can set uniforms
    shader->activate();
}
