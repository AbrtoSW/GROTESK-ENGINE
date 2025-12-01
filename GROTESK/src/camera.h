#pragma once
#include <vk_types.h>
#include "SDL3/SDL_events.h"
#include "vk_util.h"

class VulkanEngine;

class Camera {
public:

	glm::vec3 position{ 0.f, 0.f, 20.f };
	glm::vec3 frameDirection{ 0.f, 0.f, 0.f };
	float pitch{ 0.f };
	float yaw{ 0.f };
	float speed{1500.0f};

	VkExtent2D drawExtent = { 0,0 };

	Camera() = default;
	Camera(glm::vec3 startPos, VkExtent2D viewportExtent, float moveSpeed = 5.0f)
		: position(startPos), drawExtent(viewportExtent), speed(moveSpeed) {
		if (drawExtent.width == 0 || drawExtent.height == 0) {
			throw std::runtime_error("must set drawExtent");
		}
	}

	glm::mat4 getViewMatrix() const;
	glm::mat4 getProjectionMatrix() const;
	glm::mat4 getRotationMatrix() const;

	void processSDLEvent(SDL_Event& e, bool registerMouse);

	void update(float deltaTime, float speed);
};


class CameraGPU {

public:

	GPUSceneData sceneData;

	std::array<AllocatedBuffer, FRAME_OVERLAP> gpuSceneDataBuffers;
	std::array<VkDescriptorSet, FRAME_OVERLAP> descriptorSets;

	CameraGPU() = default;

	void updateFromCamera(Camera& camera);

	void upload(VulkanEngine& engine, int frameIndex);


};
