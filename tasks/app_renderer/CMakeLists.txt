
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
  shaders/gpu_culling.comp

  shaders/static_mesh.vert
  shaders/static_mesh.frag

  shaders/terrain.vert
  shaders/terrain.tesc
  shaders/terrain.tese
  shaders/terrain.frag

  shaders/particles_update.comp
  shaders/particles_draw.vert
  shaders/particles_draw.frag


  shaders/ssao_calculation.vert
  shaders/ssao_calculation.frag
  shaders/ssao_blur.comp

  shaders/deferred.vert
  shaders/deferred.frag

  shaders/tonmap0.comp
  shaders/tonmap1.comp
  shaders/tonmap2.comp

  shaders/HDR_to_LDR.vert
  shaders/HDR_to_LDR.frag
)
