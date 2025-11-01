#pragma once
#include "vk_util.h"
#include "vk_renderer.h"
#include <bitset>
#include <queue>
#include <random>


// key notes to rmemeber with ECS, i could make some parts of these parallel, the main issue would be synchronization i believe.

//possibly move these definitions to types when finished, most likely not since they are specific types for this file

using Entity = std::uint32_t;

const Entity MAX_ENTITY = 5000;


//im doing it this way because i want it to be optimized for the compiler for less boiler plate code i could have done an unordered map with runtime logic

enum struct ComponentTypes {
	MESH,
	TRANSFORM,
	MATERIAL,
	PIPELINE,
	MAX_COMPONENTS
};

using Signature = std::bitset<(std::uint8_t)ComponentTypes::MAX_COMPONENTS>;

using SystemTypeID = std::uint32_t;

const SystemTypeID MAX_SYSTEMS = 10;

class EntityManager {
public:

	EntityManager() {
		for (Entity entity = 0; entity < MAX_ENTITY; ++entity) {
			availableEntities.push(entity);
		}
	}

	Entity create_entity();

	void destroy_entity(Entity entity);

	void set_signature(Entity entity, Signature signature);

	Signature get_signature(Entity entity);

private:
	std::queue<Entity> availableEntities{};
	std::array<Signature, MAX_ENTITY> signatures{};

	inline static uint32_t existingEntity = 0;
};


class IComponentArray {
public:
	virtual ~IComponentArray() = default;
	virtual void entity_destroyed(Entity entity) = 0;
};


// will add more efficieny depending on the structrure soon by making store AOS aswell as how it stores SOA 
template <typename T>
class ComponentArray : public IComponentArray {

public:
	// i could make this data structure component a vector type instead so i can make runtime changes but for now most objects amounts instantiated will be known, this structure inspired by an article i read but tried to implement my own way

	ComponentArray() {
		std::fill(entityToIndex.begin(), entityToIndex.end(), INVALID_INDEX);
	}

	void insert_data(Entity entity, T component) {

		//fmt::print("[ComponentArray DEBUG] Adding component of type {} to entity {}\n", typeid(T).name(), entity);
	//	fmt::print("[ComponentArray DEBUG] Current size = {}\n", size);
	//	fmt::print("[ComponentArray DEBUG] MAX_ENTITY = {}\n", MAX_ENTITY);

		if (entity >= MAX_ENTITY) {
			fmt::print("[ERROR] Entity {} exceeds MAX_ENTITY\n", entity);
			return;
		}

		assert(entity < MAX_ENTITY && "Entity ID exceeds MAX_ENTITY!");

		assert(entityToIndex[entity] == INVALID_INDEX && "Component added to same entity more than once.");

		auto index = size;

		entityToIndex[entity] = index;
		indexToEntity[index] = entity;
		componentArray[index] = component;
		++size;

	//	fmt::print("[ComponentArray DEBUG] Component inserted at internal index {}\n", index);
	}

	void delete_data(Entity entity) {

		size_t index = entityToIndex[entity];
		size_t lastIndex = size - 1;

		if (index != lastIndex) {
			componentArray[index] = componentArray[lastIndex];

			Entity lastEntity = indexToEntity[lastIndex];

			indexToEntity[index] = lastEntity;

			entityToIndex[lastEntity] = index;
		}
		entityToIndex[entity] = INVALID_INDEX;

		--size;
	}

	T& get_data(Entity entity) {

		assert(entityToIndex[entity] != INVALID_INDEX && "can't get a non existing component");

		return componentArray[entityToIndex[entity]];
	}

	void entity_destroyed(Entity entity) override {
		if (entityToIndex[entity] != INVALID_INDEX) {
			delete_data(entity);
		}
	}


private:
	std::array<size_t, MAX_ENTITY> entityToIndex;
	std::array <Entity, MAX_ENTITY> indexToEntity;
	std::array<T, MAX_ENTITY> componentArray;
	size_t size = 0;
	const size_t INVALID_INDEX = size_t(-1);
};


struct MeshComponent {
	std::shared_ptr<MeshAsset> mesh;
};

struct TransformComponent {
	glm::mat4 worldMatrix;
};

struct MaterialComponent {
	DescriptorBundle desc;
};

struct PipelineComponent {
	PID pid;
};


template<typename T>
struct ComponentTypeMap;

template <>
struct ComponentTypeMap<MeshComponent> {
	static constexpr ComponentTypes type = ComponentTypes::MESH;
};

template <>
struct ComponentTypeMap<TransformComponent> {
	static constexpr ComponentTypes type = ComponentTypes::TRANSFORM;
};

template <>
struct ComponentTypeMap<MaterialComponent> {
	static constexpr ComponentTypes type = ComponentTypes::MATERIAL;
};
template <>
struct ComponentTypeMap<PipelineComponent> {
	static constexpr ComponentTypes type = ComponentTypes::PIPELINE;
};



class ComponentManager {

public:

	template <typename T>
	void add_component(Entity entity, T component) {
		getComponentArray<T>()->insert_data(entity, component);
	}

	template<typename T>
	void remove_component(Entity entity) {
		getComponentArray<T>()->delete_data(entity);
	}

	template<typename T>
	T& get_component(Entity entity) {
		return getComponentArray<T>()->get_data(entity);
	}

	void entity_destroyed(Entity entity);

private:

	std::array<std::shared_ptr<IComponentArray>, (size_t)ComponentTypes::MAX_COMPONENTS> componentArrays;

	template <typename T>
	std::shared_ptr<ComponentArray<T>> getComponentArray() {
		constexpr ComponentTypes type = ComponentTypeMap<T>::type;
		if (!componentArrays[(size_t)type]) {
			//fmt::print("[ComponentManager DEBUG] Creating ComponentArray for type {}\n", typeid(T).name());
			componentArrays[(size_t)type] = std::make_shared<ComponentArray<T>>();
		}
		return std::static_pointer_cast<ComponentArray<T>>(componentArrays[(size_t)type]);
	}
};



class System {
public:

	std::vector<Entity> entities;

	virtual void update(VkCommandBuffer cmd) = 0;
	virtual ~System() = default;
};

class SystemManager {

public:

	template <typename T>
	std::shared_ptr<T> register_system() {

		auto id = get_system_id<T>();

		auto system = std::make_shared<T>();

		systems[id] = std::static_pointer_cast<System>(system);

		return system;
	}

	template <typename T>
	void set_signature(Signature signature) {

		auto id = get_system_id<T>();

		signatures[id] = signature;
	}

	void entity_signature_changed(Entity entity, Signature entitySignature);

	void entity_destroyed(Entity entity);

	template <typename T>
	std::shared_ptr<T> get_system() {
		auto id = get_system_id<T>();
		fmt::print("Getting system ID = {}\n", id);
		fmt::print("Systems array size = {}\n", systems.size());

		if (id >= systems.size()) {
			fmt::print("Error: system ID {} out of range (systems size = {})\n", id, systems.size());
			return nullptr;
		}

		auto system = systems[id];
		if (!system) {
			fmt::print("System pointer at ID {} is null!\n", id);
			return nullptr;
		}

		return std::static_pointer_cast<T>(system);
	}

	template<typename T>
	inline static SystemTypeID get_system_id() {
		static SystemTypeID typeID = get_unique_system_id();
		return typeID;
	}

private:
	//may change shared ptr to unique since system manager should be the only one that owns this, but shared ptr just to test first implementation
	std::array<std::shared_ptr<System>, MAX_SYSTEMS> systems;
	std::array<Signature, MAX_SYSTEMS> signatures;

	inline static SystemTypeID get_unique_system_id() {
		static SystemTypeID uniqueID = 0;
		return uniqueID++;
	}

};

class ECSManager {

public:

	Entity create_entity();

	void destroy_entity(Entity entity);
	
	void set_entity_signature(Entity entity, Signature signature);

	Signature get_signature(Entity entity);

	template <typename T>
	void add_component(Entity entity, T& component) {
		// Print type info
		//fmt::print("[ECS DEBUG] Adding component of type: {}\n", typeid(T).name());

		// Print entity info
		//fmt::print("[ECS DEBUG] Entity ID: {}\n", entity);

		// Add the component to the ComponentManager
		componentManager.add_component<T>(entity, component);
		//fmt::print("[ECS DEBUG] Component added to ComponentManager\n");

		// Update entity signature
		auto signature = entityManager.get_signature(entity);
		constexpr ComponentTypes type = ComponentTypeMap<T>::type;
		signature.set((size_t)type, true);
		entityManager.set_signature(entity, signature);
	//	fmt::print("[ECS DEBUG] Signature updated: {}\n", signature.to_string());

		// Notify SystemManager
		systemManager.entity_signature_changed(entity, signature);
	//	fmt::print("[ECS DEBUG] SystemManager notified about entity signature change\n");
	}

	template <typename T>
	void remove_component(Entity entity) {
		componentManager.remove_component<T>(entity);

		auto signature = entityManager.get_signature(entity);

		constexpr ComponentTypes type = ComponentTypeMap<T>::type;

		signature.set((size_t)type, false);
		entityManager.set_signature(entity, signature);


		systemManager.entity_signature_changed(entity, signature);
	}

	template<typename T>
	T& get_component(Entity entity) {
		return componentManager.get_component<T>(entity);
	}

	template<typename T>
	std::shared_ptr<T> register_system() {
		return systemManager.register_system<T>();
	}

	template<typename T>
	void set_system_signature(Signature signature) {
		systemManager.set_signature<T>(signature);
	}

	template<typename T>
	std::shared_ptr<T> get_system() {
		return systemManager.get_system<T>();
	}


private:
	EntityManager entityManager;
	ComponentManager componentManager;
	SystemManager systemManager;
};


class RenderSystem : public  System {

public:


	void update(VkCommandBuffer cmd) override {
		// optional: leave empty or call the other update with a dummy list
	}

	void update(ECSManager& ecsManager, std::vector<RenderItem>& renderItem);
};

