
add_executable(tonemapping_renderer
  main.cpp
  App.cpp
  Renderer.cpp
  WorldRenderer.cpp
)

target_link_libraries(tonemapping_renderer
  PRIVATE glfw etna glm::glm wsi gui scene render_utils)

target_add_shaders(tonemapping_renderer
  shaders/gpu_culling.comp
  shaders/HDR_to_LDR.frag
  shaders/HDR_to_LDR.vert
  shaders/static_mesh.frag
  shaders/static_mesh.vert
  shaders/terrain.vert
  shaders/terrain.tesc
  shaders/terrain.tese
  shaders/terrain.frag
  shaders/tonmap0.comp
  shaders/tonmap1.comp
  shaders/tonmap2.comp
)
