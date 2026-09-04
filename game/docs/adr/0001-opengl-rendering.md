# ADR 0001 — Use OpenGL 4.6 + GLAD instead of Vulkan 1.3

Date: 2026-06-30
Status: Accepted

## Context

`plan/START.md` mandates Vulkan 1.3 (Vulkan-Hpp + VMA + Volk + shaderc) as the
primary graphics stack, but explicitly permits an alternative:

> _Alternative_: OpenGL 4.6 + GLAD — "Acceptable for faster prototyping
> (3-5x less code/feature) but loses compute shaders & MT recording.
> Requires an ADR."

This project targets a **playable foundation within a single implementation
pass** (Phases 1-6), not full feature parity. Vulkan's explicitness would
roughly 4-5x the renderer code (instance/device/swapchain setup, command
buffers, memory barriers, render passes, pipeline objects) and would require
installing the Vulkan SDK — none of which is currently present on the machine.

## Decision

Use **OpenGL 4.6 core profile** via **GLAD (glad2)** for the rendering backend.

- Windowing/surface: **GLFW 3.4** (unchanged from the mandated stack).
- GL function loading: **glad2** generated at CMake configure time
  (`glad_add_library(glad STATIC API gl:core=4.6)`).
- Loader call: `gladLoadGL(glfwGetProcAddress)` after context creation.
- Include order: `<glad/gl.h>` before `<GLFW/glfw3.h>` (glad2 requirement).

## Consequences

### Positive
- ~3-5x less renderer code → faster path to a working, playable game.
- No Vulkan SDK dependency; smaller toolchain footprint.
- Familiar fixed-function-style API lowers debugging cost; RenderDoc still
  works for GL frame capture.
- Compute shaders (PHASE3 greedy meshing / light propagation on GPU) are
  available via OpenGL 4.3+ compute shaders, so the loss is limited.

### Negative
- Loses multi-threaded command recording (Phase 1 worker pool cannot record
  GL commands off-thread; only CPU-side mesh building is off-thread, which is
  the actual hot path anyway).
- No explicit GPU memory control (VMA). Acceptable: VBO allocation is
  coarse-grained per chunk.
- Single-queue submission; less optimal draw-call scheduling.

## Mitigation / Future migration
The renderer is isolated behind a `Renderer` facade (`src/renderer/renderer.hpp`)
so a future Vulkan backend can be swapped in without touching gameplay/world
systems. When full feature parity (Phases 8/14 advanced rendering) is pursued,
revisit this ADR and consider migrating to Vulkan per the original stack.
