
add_executable(app_baker
  main.cpp
)

target_link_libraries(app_baker
  PRIVATE tinygltf glm::glm etna)
