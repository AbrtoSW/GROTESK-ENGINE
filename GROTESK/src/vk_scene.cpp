#include "vk_scene.hpp"
#include "vk_engine.h"

Scene::Scene(VulkanEngine* eng) : engine(eng) {}

void Scene::init_scene() {
	
	init_renderSystem();
	terrainMesh();
	initDeagleEntity();
	init_camera();

};



void Scene::render_scene() {

	std::vector<RenderItem> items;

	renderSystem->update(ecsManager, items);

	mainCamera.update(engine->deltaTime, 1000.0f);

	mainCameraGPU.updateFromCamera(mainCamera);

	int frameIndex = engine->frameNumber % FRAME_OVERLAP;

	mainCameraGPU.upload(*engine, frameIndex);

	RenderContext context;

	context.sceneData = &mainCameraGPU.sceneData;
	context.globalSet = engine->frames[frameIndex].cameraSet;


	//fmt::print("Frame {} allocator alive: {}\n", frameIndex, (engine->frames[frameIndex].frameDescriptors != nullptr));
	//fmt::print("Descriptor set handle: {}\n", (void*)mainCameraGPU.descriptorSets[frameIndex]);

	renderer->render_frame(items, context);
};



void Scene::init_camera() {
	glm::vec3 pos = glm::vec3(0.f, 4.f, 20.f);
	float speed = 0.0f;
	VkExtent2D renderExtent;
	renderExtent.width = std::min(engine->swapchainExtent.width, engine->drawImage.imageExtent.width);
	renderExtent.height = std::min(engine->swapchainExtent.height, engine->drawImage.imageExtent.height);
	fmt::print("render extent height val{}, width val{}\n", renderExtent.height, renderExtent.width);
	mainCamera = Camera(pos, renderExtent, speed);
}

void Scene::init_renderSystem() {
	renderSystem = ecsManager.register_system<RenderSystem>();

	Signature signature;
	signature.set((size_t)ComponentTypes::MESH);
	signature.set((size_t)ComponentTypes::TRANSFORM);

	auto id = ecsManager.get_system<RenderSystem>();
	fmt::print("render system id is {}\n", (void*)id.get());


	fmt::print("Returned pointer = {}\n", (void*)renderSystem.get());

	ecsManager.set_system_signature<RenderSystem>(signature);

}

void Scene::initDeagleEntity() {
	using std::filesystem::path;

	// ✅ use Asset() so it works both in IDE and shipped exe
	const path glbPath = Asset("assets/DEAGLE.glb");

	fmt::print("[assets] DEAGLE path resolved to: {}\n", glbPath.string());

	if (!std::filesystem::exists(glbPath)) {
		fmt::print("GLB not found: {}\n", glbPath.string());
		return;
	}

	// 1) Load geometry; each surface now has newSurface.materialSet (set=1)
	auto meshesOpt = Importer::loadGltf(engine, renderer, glbPath);
	if (!meshesOpt || meshesOpt->empty()) {
		fmt::print("Failed to load {}\n", glbPath.string());
		return;
	}
	else {
		fmt::print("{}, loaded successfully\n", glbPath.string());
	}

	// 2) Pick the first mesh
	std::shared_ptr<MeshAsset> meshAsset = (*meshesOpt)[0];

	// 3) Create a few entities
	std::vector<Entity> ents;
	ents.reserve(3);
	for (int i = 0; i < 3; ++i) {
		ents.push_back(ecsManager.create_entity());
	}

	// 4) Components: Mesh + Transform + Pipeline
	MeshComponent meshComp;
	meshComp.mesh = meshAsset;   // or meshComp.mesh = meshAsset; (match your struct)

	PipelineComponent pipeComp;
	pipeComp.pid = renderer->pidNewOpaquePipeline;   // pipeline that has set0+set1

	// You can omit MaterialComponent entirely (surfaces bind their own set=1).
	// If your ECS expects it, keep it empty:
	MaterialComponent matComp;
	matComp.desc.clear();  // no set=1 here; we’ll use srf.materialSet per draw

	// 5) Place instances
	float spacing = 3.0f;
	int index = 0;

	for (auto e : ents) {
		TransformComponent xform{};
		int row = index / 5;
		int col = index % 5;

		glm::mat4 T = glm::translate(glm::mat4(1.0f), glm::vec3(col * spacing, 4.0f, row * spacing));
		glm::mat4 S = glm::scale(glm::mat4(1.0f), glm::vec3(10.0f));
		xform.worldMatrix = T * S;

		ecsManager.add_component(e, meshComp);
		ecsManager.add_component(e, xform);
		ecsManager.add_component(e, pipeComp);
		// optional:
		ecsManager.add_component(e, matComp);

		renderSystem->entities.push_back(e);
		++index;
	}

	fmt::print("ECS components added (mesh surfaces: {}).\n", meshAsset->surfaces.size());
}

void Scene::terrainMesh() {
	const int Resolution = 128;
	renderer->terrainInstanceCount = 1;
	renderer->init_terrain_gpu_resources(renderer->terrainInstanceCount, false, true);

	const uint32_t H = renderer->heightmapImage.imageExtent.width;
	const float    targetHeightTPM = 1.0f;
	float worldScale = float(H) / (targetHeightTPM * float(Resolution));

	GPUMeshBuffers grid = buildSharedGrid(Resolution);

	std::vector<TerrainInstance> instances(renderer->terrainInstanceCount);

	const uint32_t N = uint32_t(std::ceil(std::sqrt(double(renderer->terrainInstanceCount))));

	// precision-safe step & centering in double
	const double step = double(Resolution) * double(worldScale);
	const double half = 0.5 * double(N - 1);

	// *** NO CAMERA: fixed world origin ***
	const glm::dvec2 renderOrigin = glm::dvec2(0.0);

	for (uint32_t i = 0; i < renderer->terrainInstanceCount; i++) {
		uint32_t tx = i % N, tz = i / N;

		// exact grid in double, centered, then fixed-origin (no camera)
		glm::dvec2 base = (glm::dvec2(double(tx), double(tz)) - glm::dvec2(half)) * step;
		glm::dvec2 shifted = base - renderOrigin;

		instances[i].originXZ = glm::vec2(shifted);
		instances[i].uvOrigin = { 0.f, 0.f };
		instances[i].uvScale = { 1.f, 1.f };
		instances[i].worldScale = worldScale;
		instances[i].tileIndex = i;
	}

	renderer->upload_terrain_instances(instances.data(), renderer->terrainInstanceCount);
	renderer->terrainGrid = grid;
	renderer->terrainGridWorldScale = worldScale;
	cpuTerrainInstances = instances;
	rebuild_terrain_bounds();

	
{
    const TerrainInstance& ti = instances[0];
    const float tileMeters = float(Resolution) * renderer->terrainGridWorldScale;

    glm::vec2 worldCenterXZ = ti.originXZ + 0.5f * glm::vec2(tileMeters, tileMeters);

    float radiusMeters = 1000.0f; // pick a test radius
    glm::vec2 local = worldCenterXZ - ti.originXZ;
    glm::vec2 localUV = local / tileMeters;
    float radiusUV = radiusMeters / tileMeters;

    // OLD:
    // renderer->update_brush_ubo(localUV, radiusUV);

    // NEW: same center/radius you chose here, now also pass strength/mode
    renderer->update_brush_ubo(
        localUV,
        radiusUV,
        editorBrushState.strength,
        editorBrushState.mode
    );
}

	auto terrainAsset = std::make_shared<MeshAsset>();
	terrainAsset->meshBuffers = grid;
	GeometrySurface srf{ .startIndex = 0, .count = grid.indexCountTotal, .material = nullptr };
	terrainAsset->surfaces.push_back(srf);

	Entity e = ecsManager.create_entity();

	MeshComponent mc;  mc.mesh = terrainAsset;
	TransformComponent tc; tc.worldMatrix = glm::mat4(1.0f);
	PipelineComponent pc; pc.pid = renderer->pidHeightMap;

	MaterialComponent mat;
	mat.desc.add(1, renderer->terrainSet);

	ecsManager.add_component(e, mc);
	ecsManager.add_component(e, tc);
	ecsManager.add_component(e, mat);
	ecsManager.add_component(e, pc);
	renderSystem->entities.push_back(e);

	engine->mainDeletionQueue.push_mesh_buffer_deletion(terrainAsset);
}




GPUMeshBuffers Scene::buildSharedGrid(int r) {
	std::vector<VertexP2> gridVertices;
	std::vector<uint32_t> gridIndices;
	gridVertices.reserve(size_t(r + 1) * size_t(r + 1));
	gridIndices.reserve(size_t(r) * size_t(r) * 6);

	for (int z = 0; z <= r; ++z)
		for (int x = 0; x <= r; ++x)
			gridVertices.push_back({ glm::vec2(float(x), float(z)) });

	auto vertexIndex = [&](int x, int z) { return uint32_t(z * (r + 1) + x); };
	for (int z = 0; z < r; ++z)
		for (int x = 0; x < r; ++x) {
			uint32_t topLeft = vertexIndex(x, z);
			uint32_t topRight = vertexIndex(x + 1, z);
			uint32_t bottomLeft = vertexIndex(x, z + 1);
			uint32_t bottomRight = vertexIndex(x + 1, z + 1);
			gridIndices.insert(gridIndices.end(), { topLeft, bottomLeft, topRight, topRight, bottomLeft, bottomRight });
		}

	std::vector<Vertex> gpuVertices;
	gpuVertices.resize(gridVertices.size());
	for (size_t k = 0; k < gridVertices.size(); ++k) {
		// leave grid in [0, r] space — top-left aligned
		gpuVertices[k].position = {
			gridVertices[k].position.x,
			0.0f,
			gridVertices[k].position.y
		};

		gpuVertices[k].uv_x = gridVertices[k].position.x / float(r);
		gpuVertices[k].uv_y = gridVertices[k].position.y / float(r);
	}
	return engine->uploadMesh(std::span{ gridIndices }, std::span{ gpuVertices });
}



static bool screen_to_world_xz_plane(const Camera& cam, VkExtent2D fb, int mx, int my, glm::vec2& outXZ)
{
	if (fb.width == 0 || fb.height == 0) return false;

	// NDC
	float x = (2.0f * float(mx)) / float(fb.width) - 1.0f;
	float y = 1.0f - (2.0f * float(my)) / float(fb.height);

	glm::vec4 ndcNear(x, y, -1.0f, 1.0f);
	glm::vec4 ndcFar(x, y, 1.0f, 1.0f);

	// assume cam.proj and cam.view exist; adjust names if yours differ
	glm::mat4 invVP = glm::inverse(cam.getProjectionMatrix() * cam.getViewMatrix());
	glm::mat4 invV = glm::inverse(cam.getViewMatrix());

	glm::vec4 wNear = invVP * ndcNear; wNear /= wNear.w;
	glm::vec4 wFar = invVP * ndcFar;  wFar /= wFar.w;

	glm::vec3 ro = glm::vec3(invV[3]);                 // camera world pos
	glm::vec3 rd = glm::normalize(glm::vec3(wFar - wNear));

	// intersect y=0 plane
	if (glm::abs(rd.y) < 1e-6f) return false;
	float t = -ro.y / rd.y;
	if (t <= 0.0f) return false;

	glm::vec3 hit = ro + t * rd;
	outXZ = glm::vec2(hit.x, hit.z);
	return true;
}


void Scene::update_brush_from_mouse(int mouseX, int mouseY)
{
	if (!renderer || cpuTerrainInstances.empty()) return;

	VkExtent2D extent{ renderer->drawExtent.width, renderer->drawExtent.height };
	glm::vec2 worldXZ;
	if (!screen_to_world_xz_plane(mainCamera, extent, mouseX, mouseY, worldXZ)) return;

	worldXZ = glm::clamp(worldXZ, terrainMinXZ, terrainMaxXZ);

	// OLD:
	// float radiusMeters = 5.0f;
	// renderer->update_brush_ubo(worldXZ, radiusMeters);

	// NEW: drive from editorBrushState so CPU & GPU match
	renderer->update_brush_ubo(
		worldXZ,
		editorBrushState.radiusWorld,
		editorBrushState.strength,
		editorBrushState.mode
	);
}


void Scene::rebuild_terrain_bounds()
{
	if (!renderer || cpuTerrainInstances.empty()) { terrainMinXZ = terrainMaxXZ = glm::vec2(0); return; }

	const int   Resolution = 128;
	const float tileMeters = float(Resolution) * renderer->terrainGridWorldScale;

	glm::vec2 mn(std::numeric_limits<float>::infinity());
	glm::vec2 mx(-std::numeric_limits<float>::infinity());

	for (const auto& ti : cpuTerrainInstances) {
		mn = glm::min(mn, ti.originXZ);
		mx = glm::max(mx, ti.originXZ + glm::vec2(tileMeters));
	}
	terrainMinXZ = mn;
	terrainMaxXZ = mx;
}

void Scene::enqueue_brush_world(const glm::vec2& worldXZ)
{
	if (!renderer || cpuTerrainInstances.empty()) return;

	const int   Resolution = 128;
	const float tileMeters = float(Resolution) * renderer->terrainGridWorldScale;

	const uint32_t H = renderer->heightmapImage.imageExtent.width;
	const float metersPerTexel = tileMeters / float(H - 1);

	const float radiusWorld = editorBrushState.radiusWorld;
	const float strength = editorBrushState.strength;
	const int   mode = editorBrushState.mode;

	glm::vec2 mn = worldXZ - glm::vec2(radiusWorld);
	glm::vec2 mx = worldXZ + glm::vec2(radiusWorld);

	for (const TerrainInstance& ti : cpuTerrainInstances) {
		glm::vec2 tMin = ti.originXZ;
		glm::vec2 tMax = ti.originXZ + glm::vec2(tileMeters);
		if (mx.x < tMin.x || mn.x > tMax.x || mx.y < tMin.y || mn.y > tMax.y) continue;

		BrushStroke b{};
		b.layer = ti.tileIndex;                            // array slice
		b.tileOrigin = ti.originXZ;
		b.worldScale = metersPerTexel;         // meters/texel
		b.worldXZ = worldXZ;
		b.localUV = glm::vec2(0);                            // not used in CPU path
		b.radiusWorld = radiusWorld;
		b.strength = strength;
		b.mode = mode;

		renderer->editorBrushQueue.push_back(b);
	}
}


bool Scene::screen_to_world_xz_plane(const Camera& cam, VkExtent2D fb, int mx, int my, glm::vec2& outXZ)
{
	if (fb.width == 0 || fb.height == 0) return false;

	float x = (2.0f * float(mx)) / float(fb.width) - 1.0f;
	float y = 1.0f - (2.0f * float(my)) / float(fb.height);

	glm::vec4 ndcNear(x, y, -1.0f, 1.0f);
	glm::vec4 ndcFar(x, y, 1.0f, 1.0f);

	glm::mat4 invVP = glm::inverse(cam.getProjectionMatrix() * cam.getViewMatrix());
	glm::mat4 invV = glm::inverse(cam.getViewMatrix());

	glm::vec4 wNear = invVP * ndcNear; wNear /= wNear.w;
	glm::vec4 wFar = invVP * ndcFar;  wFar /= wFar.w;

	glm::vec3 ro = glm::vec3(invV[3]);
	glm::vec3 rd = glm::normalize(glm::vec3(wFar - wNear));

	if (glm::abs(rd.y) < 1e-6f) return false;
	float t = -ro.y / rd.y;
	if (t <= 0.0f) return false;

	glm::vec3 hit = ro + t * rd;
	outXZ = glm::vec2(hit.x, hit.z);
	return true;
}

void Scene::paint_from_cursor()
{
	if (!renderer) return;
	glm::vec2 worldXZ;
	VkExtent2D fb{ renderer->drawExtent.width, renderer->drawExtent.height };
	if (Scene::screen_to_world_xz_plane(mainCamera, fb, brushMouseX, brushMouseY, worldXZ)) {
		worldXZ = glm::min(glm::max(worldXZ, terrainMinXZ), terrainMaxXZ);
		enqueue_brush_world(worldXZ);
	}
}