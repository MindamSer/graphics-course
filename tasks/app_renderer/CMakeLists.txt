
add_executable(app_renderer
  main.cpp
  App.cpp
  Renderer.cpp
  WorldRenderer.cpp
  ResourceManager.cpp
)

target_link_libraries(app_renderer
  PRIVATE glfw etna glm::glm wsi gui scene render_utils)

target_add_shaders(app_renderer
  shaders/deferred.frag
  shaders/deferred.vert
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
