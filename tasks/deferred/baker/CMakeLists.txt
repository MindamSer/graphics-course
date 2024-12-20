
add_executable(deferred_baker
  main.cpp
)

target_link_libraries(deferred_baker
  PRIVATE tinygltf glm::glm etna)
