#include "vk_ecs.hpp"

Entity EntityManager::create_entity() {

	assert(existingEntity < MAX_ENTITY && "Entity amount out of bounds");

	Entity id = availableEntities.front();

	availableEntities.pop();

	++EntityManager::existingEntity;

	return id;
}

void EntityManager::destroy_entity(Entity entity) {

	signatures[entity].reset();

	availableEntities.push(entity);

	--EntityManager::existingEntity;
}

void EntityManager::set_signature(Entity entity, Signature signature) {
	assert(entity < MAX_ENTITY && "Entity amount out of bounds");

	signatures[entity] = signature;
}

Signature EntityManager::get_signature(Entity entity) {
	//could replace to get names instead of bit combination
	assert(entity < MAX_ENTITY && "Signature amount out of bounds");

	return signatures[entity];
}

//COMPONENT LOGIC

void ComponentManager::entity_destroyed(Entity entity) {
	for (const auto& component : componentArrays) {
		if (!component) continue;
		component->entity_destroyed(entity);
	}
}

//SYSTEM LOGIC

void SystemManager::entity_signature_changed(Entity entity, Signature entitySignature) {
    
	for (SystemTypeID id = 0; id < MAX_SYSTEMS; ++id) {

		if (systems[id]) {
		//	fmt::print("System found for {}\n", id);
		}
		else {
			//fmt::print("[SystemManager DEBUG] Tried to access a null system slot{}\n", id);
			continue;
		}

        auto& systemEntities = systems[id]->entities;
        auto& systemSignature = signatures[id];

        bool matches = (entitySignature & systemSignature) == systemSignature;
        auto it = std::find(systemEntities.begin(), systemEntities.end(), entity);

        if (matches && it == systemEntities.end()) {
            systemEntities.push_back(entity);
        } else if (!matches && it != systemEntities.end()) {
            std::iter_swap(it, systemEntities.end() - 1);
            systemEntities.pop_back();
        }
    }
}

void SystemManager::entity_destroyed(Entity entity) {
	for (auto& system : systems) {
		auto it = std::find(system->entities.begin(), system->entities.end(), entity);

		if (it != system->entities.end()) {
			std::iter_swap(it, system->entities.end() - 1);
			system->entities.pop_back();
		}
	}
}

//ECS LOGIC

Entity ECSManager::create_entity() {
	return entityManager.create_entity();
}

void ECSManager::destroy_entity(Entity entity) {
	entityManager.destroy_entity(entity);
	componentManager.entity_destroyed(entity);
	systemManager.entity_destroyed(entity);
}

Signature ECSManager::get_signature(Entity entity) {
	return entityManager.get_signature(entity);
}

void ECSManager::set_entity_signature(Entity entity, Signature signature) {
	entityManager.set_signature(entity, signature);
	systemManager.entity_signature_changed(entity, signature);
}


void RenderSystem::update(ECSManager& ecsManager, std::vector<RenderItem>& renderItems) {
	renderItems.clear();

	// Reserve based on total surfaces
	size_t totalSurfaces = 0;
	for (auto e : entities) {
		const auto& mesh = ecsManager.get_component<MeshComponent>(e);
		if (mesh.mesh) totalSurfaces += mesh.mesh->surfaces.size();
	}
	renderItems.reserve(totalSurfaces);

	for (auto e : entities) {
		const auto& mesh = ecsManager.get_component<MeshComponent>(e);
		const auto& transform = ecsManager.get_component<TransformComponent>(e);
		const auto& material = ecsManager.get_component<MaterialComponent>(e);
		const auto& pipe = ecsManager.get_component<PipelineComponent>(e);

		if (!mesh.mesh) continue;
		const auto& mb = mesh.mesh->meshBuffers;

		for (const auto& srf : mesh.mesh->surfaces) {
			RenderItem ri{};

			ri.gpuData.vertexBufferAddress = mb.vertexBufferAddress;
			ri.gpuData.indexBuffer = mb.indexBuffer.buffer;
			ri.gpuData.indexBufferSizeBytes = mb.indexBuffer.sizeBytes;
			ri.gpuData.firstIndex = srf.startIndex;
			ri.gpuData.indexCount = srf.count;
			ri.gpuData.transform = transform.worldMatrix;
			ri.gpuData.instanceCount = mesh.mesh->instanceCount;

			ri.pID = pipe.pid;

			ri.desc.clear();
			if (srf.material && srf.material->data.materialSet != VK_NULL_HANDLE)
				ri.desc.add(1, srf.material->data.materialSet);
			else
				ri.desc = material.desc; // fallback if no texture bound

			renderItems.push_back(ri);
		}
	}
}
