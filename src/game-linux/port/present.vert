#version 450
// PORT: shader wierzcholkow backendu SDL3 GPU (port_gpu.cpp).
// Fullscreen quad: 2 trojkaty, pozycje clip-space i uv 0..1 na caly swapchain.
// Kompilacja do SPIR-V w Makefile (glslangValidator/glslc), wbudowana jako
// tablica bajtow (spv_data.cpp, xxd). Konwencja SDL_GPU dla SPIR-V: samplery
// fragmentu w set 2, uniformy fragmentu w set 3 (patrz SDL_CreateGPUShader).
layout(location = 0) in vec2 aPos; // clip-space (-1..1)
layout(location = 1) in vec2 aUv;  // 0..1 calego okna

layout(location = 0) out vec2 vUv;

void main() {
  vUv = aUv;
  gl_Position = vec4(aPos, 0.0, 1.0);
}
