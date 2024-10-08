
add_executable(local_shadertoy_2
  main.cpp
  App.cpp
)

target_link_libraries(local_shadertoy_2
  PRIVATE glfw etna glm::glm wsi gui)

target_add_shaders(local_shadertoy_2
  shaders/main_shader.frag
  shaders/main_shader.vert
  shaders/procedural_texture.comp
)
