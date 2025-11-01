#pragma  once
#include "vk_renderer.h"
#include "vk_ecs.hpp"
#include "vk_loader.h"
#include "camera.h"
#include "vk_types.h"

extern Camera mainCamera;

class Scene {
public:
	 
	Scene(VulkanEngine* eng);

	ECSManager ecsManager;
	Renderer* renderer = nullptr;
	std::shared_ptr<RenderSystem> renderSystem = nullptr;
	std::vector<std::shared_ptr<MeshAsset>> meshes;

	Camera mainCamera;
	CameraGPU mainCameraGPU;

	bool brushPreviewEnabled = false;
	int  brushMouseX = 0, brushMouseY = 0;  // latest mouse position

	void init_scene();
	void render_scene();   

	void init_camera();
	void init_renderSystem();
	void initDeagleEntity();

	glm::vec2 brushLockedWorldXZ{ 0.0f };
	glm::vec2 lastBrushWorldXZ{ 0.0f };
	bool      brushLockActive = false;
	std::vector<TerrainInstance> cpuTerrainInstances;
	glm::vec2 terrainMinXZ{ 0.0f };
	glm::vec2 terrainMaxXZ{ 0.0f };
	EditorBrushState editorBrushState;
	void terrainMesh();
	GPUMeshBuffers buildSharedGrid(int r);

	void update_brush_from_mouse(int mouseX, int mouseY);
	void rebuild_terrain_bounds();
	void enqueue_brush_world(const glm::vec2& worldXZ);
	static bool screen_to_world_xz_plane(const Camera& cam, VkExtent2D fb, int mx, int my, glm::vec2& outXZ);
	void paint_from_cursor();
private:
	VulkanEngine* engine;
};

