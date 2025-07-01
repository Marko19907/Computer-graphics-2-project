#pragma once

#include <glm/glm.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <stack>
#include <vector>
#include <cstdio>
#include <stdbool.h>
#include <cstdlib> 
#include <ctime> 
#include <chrono>
#include <fstream>

enum SceneNodeType {
	GEOMETRY, POINT_LIGHT, SPOT_LIGHT, GEOMETRY_2D, NORMAL_MAPPED_GEOMETRY 
};

static int globalId = 0;

struct SceneNode {
	SceneNode() {
		position = glm::vec3(0, 0, 0);
		rotation = glm::vec3(0, 0, 0);
		scale = glm::vec3(1, 1, 1);

        referencePoint = glm::vec3(0, 0, 0);
        vertexArrayObjectID = -1;
        VAOIndexCount = 0;

        nodeType = GEOMETRY;

		id = globalId++;
	}

	// A list of all children that belong to this node.
	std::vector<SceneNode*> children;
	
	// The node's position and rotation relative to its parent
	glm::vec3 position;
	glm::vec3 rotation;
	glm::vec3 scale;

	// Light color, present only on light nodes
	glm::vec3 lightColor;

	// A transformation matrix representing the transformation of the node's location relative to its parent. This matrix is updated every frame. MVP matrix
	glm::mat4 currentTransformationMatrix;

	// Object-to-world transform
	glm::mat4 modelMatrix;

	// Normal matrix
	glm::mat3 normalMatrix;

	// The location of the node's reference point
	glm::vec3 referencePoint;

	// The ID of the VAO containing the "appearance" of this SceneNode.
	int vertexArrayObjectID;
	unsigned int VAOIndexCount;

	// Node type is used to determine how to handle the contents of a node
	SceneNodeType nodeType;

	// ID of the node
	int id;

	// for 2D or normal mapped geometry
	unsigned int textureID;

	unsigned int normalMapID;

	unsigned int roughnessMapID;

	// ID of the material used for this node, 0 if the default material is used
	unsigned int materialID = 0;
};

SceneNode* createSceneNode();
void addChild(SceneNode* parent, SceneNode* child);
void printNode(SceneNode* node);
int totalChildren(SceneNode* parent);
