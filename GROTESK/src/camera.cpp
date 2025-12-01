#include "camera.h"
#include <glm/gtx/transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include "vk_engine.h"
#include "SDL3/SDL_keyboard.h"

glm::mat4 Camera::getViewMatrix() const {
	glm::mat4 cameraTranslation = glm::translate(glm::mat4(1.f), position);
	glm::mat4 cameraRotation = getRotationMatrix();
	return glm::inverse(cameraTranslation * cameraRotation);
}

glm::mat4 Camera::getProjectionMatrix() const {
	glm::mat4 P = glm::perspectiveRH_ZO(glm::radians(70.f),
		float(drawExtent.width) / float(drawExtent.height), 0.1f, 10000.f);
	P[1][1] *= -1;
	return P;
}

glm::mat4 Camera::getRotationMatrix() const {
	constexpr float maxPitch = glm::radians(89.0f);
	const float p = glm::clamp(pitch, -maxPitch, maxPitch); // local, no write
	const glm::quat pitchRotation = glm::angleAxis(p, glm::vec3{ 1.f,  0.f, 0.f });
	const glm::quat yawRotation = glm::angleAxis(yaw, glm::vec3{ 0.f, -1.f, 0.f });
	return glm::toMat4(yawRotation) * glm::toMat4(pitchRotation);
}
void Camera::update(float deltaTime, float speed) {

	glm::mat4 cameraRotation = getRotationMatrix();

	glm::quat yawQ = glm::angleAxis(yaw, glm::vec3(0.f, -1.f, 0.f));
	glm::mat4 yawOnly = glm::toMat4(yawQ);

	glm::vec3 localMove = frameDirection;
	float worldY = localMove.y;
	localMove.y = 0.0f;

	glm::vec3 moved = glm::vec3(yawOnly * glm::vec4(localMove * speed * deltaTime, 0.0f));
	moved.y += worldY * speed * deltaTime;

	position += moved;
}


void Camera::processSDLEvent(SDL_Event& e, bool registerMouse) {
	glm::vec3 direction(0.0f);
	const bool* keyState = SDL_GetKeyboardState(nullptr);

	if (keyState[SDL_SCANCODE_W])      direction.z -= 1.0f;
	if (keyState[SDL_SCANCODE_S])      direction.z += 1.0f;
	if (keyState[SDL_SCANCODE_A])      direction.x -= 1.0f;
	if (keyState[SDL_SCANCODE_D])      direction.x += 1.0f;
	if (keyState[SDL_SCANCODE_SPACE])  direction.y += 1.0f;  
	if (keyState[SDL_SCANCODE_LCTRL])  direction.y -= 1.0f;

	if (glm::length(direction) > 0.0f)
		direction = glm::normalize(direction);

	frameDirection = direction;



	if (registerMouse) {
		if (e.type == SDL_EVENT_MOUSE_MOTION) {
			yaw += (float)e.motion.xrel / 200.f;
			pitch -= (float)e.motion.yrel / 200.f;
		}
	}
	else {
		return;
	}
}

void CameraGPU::updateFromCamera(Camera& camera) {
	sceneData.view = camera.getViewMatrix();
	sceneData.proj = camera.getProjectionMatrix();
	sceneData.viewproj = sceneData.proj * sceneData.view;
	sceneData.cameraPos = glm::vec4(camera.position, 1.0f);
}

void CameraGPU::upload(VulkanEngine& engine, int frameIndex) {
	// Use the persistently mapped pointer for this frame
	void* mappedData = engine.frames[frameIndex].cameraUBOMapped;

	// Copy the updated camera data
	memcpy(mappedData, &sceneData, sizeof(GPUSceneData));
}