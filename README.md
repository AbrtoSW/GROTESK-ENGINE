GROTESK is an experimental 3D engine and world editor built with Vulkan 1.3.
It’s an early alpha — raw, unorganized, and constantly evolving — created to explore low-level rendering, GPU compute workflows, and compile-time ECS design.
The focus is performance, scalability, and correctness through direct implementation rather than abstraction.

Overview

This project blends renderer, ECS, and editor logic in one place while testing real Vulkan concepts.
Every system was written from scratch to understand how GPU-driven terrain, descriptor management, and compute-based editing actually work.
It’s not meant to be clean yet — it’s a working prototype to validate architecture and performance behavior.

Systems

ECS
A fully compile-time component type mapping system.
Components are registered through template specializations that resolve to fixed enum IDs, eliminating runtime type lookup and reflection overhead.
It’s designed for scalability — supports many entity types and integrates directly with rendering and material systems.
Fast, deterministic, and easy to extend by adding new struct mappings.

Renderer
Handles Vulkan 1.3 initialization, swapchain, command buffers, and synchronization.
Uses VMA for memory management and a custom PipelineManager that handles pipeline hot-reloading and descriptor compatibility.
Implements dynamic rendering, synchronization2, descriptor indexing, and indirect draw count extensions.
Built to support large-scale terrain rendering and GPU compute passes.

Terrain
GPU-driven tiled terrain using 2D array heightmaps (one slice per tile).
Compute shaders build indirect draw calls and update per-tile data.
System is designed to scale and experiments with clipmap-style LOD and dynamic streaming.

Brush System
Hybrid CPU/GPU terrain editing pipeline.
Automatically chooses the optimal path — CPU for small, local edits and GPU compute for large-area changes.
I would implement fallbacks  for systems without compute capability.
Handles per-tile locking, UV alignment, and screen-to-world coordinate mapping.
The purpose 
GROTESK is a hands-on engine project built to understand Vulkan and modern engine structure through real implementation.
Every part from ECS templates to GPU compute was written to study how full-scale engines connect systems together under real performance constraints. 

REQUIREMENT
must run on release mode
