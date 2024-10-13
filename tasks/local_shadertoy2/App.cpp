#include "App.hpp"

#include <etna/Etna.hpp>
#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/RenderTargetStates.hpp>

#include "stb_image.h"


App::App()
  : resolution{1280, 720}
  , useVsync{true}
  , timeStart{std::chrono::system_clock::now()}
{
  // First, we need to initialize Vulkan, which is not trivial because
  // extensions are required for just about anything.
  {
    // GLFW tells us which extensions it needs to present frames to the OS window.
    // Actually rendering anything to a screen is optional in Vulkan, you can
    // alternatively save rendered frames into files, send them over network, etc.
    // Instance extensions do not depend on the actual GPU, only on the OS.
    auto glfwInstExts = windowing.getRequiredVulkanInstanceExtensions();

    std::vector<const char*> instanceExtensions{glfwInstExts.begin(), glfwInstExts.end()};

    // We also need the swapchain device extension to get access to the OS
    // window from inside of Vulkan on the GPU.
    // Device extensions require HW support from the GPU.
    // Generally, in Vulkan, we call the GPU a "device" and the CPU/OS combination a "host."
    std::vector<const char*> deviceExtensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    // Etna does all of the Vulkan initialization heavy lifting.
    // You can skip figuring out how it works for now.
    etna::initialize(etna::InitParams{
      .applicationName = "Local Shadertoy",
      .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
      .instanceExtensions = instanceExtensions,
      .deviceExtensions = deviceExtensions,
      // Replace with an index if etna detects your preferred GPU incorrectly
      .physicalDeviceIndexOverride = {},
      .numFramesInFlight = 1,
    });
  }

  // Now we can create an OS window
  osWindow = windowing.createWindow(OsWindow::CreateInfo{
    .resolution = resolution,
  });

  // But we also need to hook the OS window up to Vulkan manually!
  {
    // First, we ask GLFW to provide a "surface" for the window,
    // which is an opaque description of the area where we can actually render.
    auto surface = osWindow->createVkSurface(etna::get_context().getInstance());

    // Then we pass it to Etna to do the complicated work for us
    vkWindow = etna::get_context().createWindow(etna::Window::CreateInfo{
      .surface = std::move(surface),
    });

    // And finally ask Etna to create the actual swapchain so that we can
    // get (different) images each frame to render stuff into.
    // Here, we do not support window resizing, so we only need to call this once.
    auto [w, h] = vkWindow->recreateSwapchain(etna::Window::DesiredProperties{
      .resolution = {resolution.x, resolution.y},
      .vsync = useVsync,
    });

    // Technically, Vulkan might fail to initialize a swapchain with the requested
    // resolution and pick a different one. This, however, does not occur on platforms
    // we support. Still, it's better to follow the "intended" path.
    resolution = {w, h};
  }

  // Next, we need a magical Etna helper to send commands to the GPU.
  // How it is actually performed is not trivial, but we can skip this for now.
  commandManager = etna::get_context().createPerFrameCmdMgr();


  // TODO: Initialize any additional resources you require here!


  // --- Procedural Texture Init ---

  etna::create_program(
    "procedural_texture", 
    {LOCAL_SHADERTOY_2_SHADERS_ROOT "procedural_texture.comp.spv"});

  texturePipeline =
    etna::get_context().getPipelineManager().createComputePipeline("procedural_texture", {});

  textureImage = etna::get_context().createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{resolution.x, resolution.y, 1},
    .name = "procedural_texture",
    .format = vk::Format::eR8G8B8A8Unorm,
    .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage});

  textureSampler = etna::Sampler::Sampler(etna::Sampler::CreateInfo{
    .addressMode = vk::SamplerAddressMode::eRepeat, .name = "textureSampler"});


  // --- Texture Init ---

  int width, height, channels;
  unsigned char* image_data = stbi_load(
    GRAPHICS_COURSE_RESOURCES_ROOT "/textures/test_tex_1.png", &width, &height, &channels, 4);

  fileTextureImage = etna::get_context().createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{static_cast<unsigned>(width), static_cast<unsigned>(height), 1},
    .name = "file_texture",
    .format = vk::Format::eR8G8B8A8Unorm,
    .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage |
      vk::ImageUsageFlagBits::eTransferDst});

  fileTextureSampler = etna::Sampler::Sampler(etna::Sampler::CreateInfo{
    .addressMode = vk::SamplerAddressMode::eRepeat, .name = "fileTextureSampler"});

  transferHelper =
    std::make_unique<etna::BlockingTransferHelper>(etna::BlockingTransferHelper::CreateInfo{
      .stagingSize = static_cast<std::uint32_t>(width * height),
    });

  std::unique_ptr<etna::OneShotCmdMgr> OneShotCommands = etna::get_context().createOneShotCmdMgr();

  transferHelper->uploadImage(*OneShotCommands, fileTextureImage, 0, 0,
      std::span<const std::byte>(reinterpret_cast<const std::byte*>(image_data), width * height * 4));

  stbi_image_free(image_data);


  // --- Shader Init ---

  etna::create_program(
    "shader",
    {LOCAL_SHADERTOY_2_SHADERS_ROOT "main_shader.vert.spv",
     LOCAL_SHADERTOY_2_SHADERS_ROOT "main_shader.frag.spv"});

  shaderPipeline = 
      etna::get_context().getPipelineManager().createGraphicsPipeline(
          "shader", 
          etna::GraphicsPipeline::CreateInfo{
              .fragmentShaderOutput = 
              {
                  .colorAttachmentFormats = {vk::Format::eB8G8R8A8Srgb}
              },
          });

}

App::~App()
{
  ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());
}

void App::run()
{
  while (!osWindow->isBeingClosed())
  {
    windowing.poll();

    drawFrame();
  }

  // We need to wait for the GPU to execute the last frame before destroying
  // all resources and closing the application.
  ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());
}

void App::drawFrame()
{
  // First, get a command buffer to write GPU commands into.
  auto currentCmdBuf = commandManager->acquireNext();

  // Next, tell Etna that we are going to start processing the next frame.
  etna::begin_frame();

  // And now get the image we should be rendering the picture into.
  auto nextSwapchainImage = vkWindow->acquireNext();

  // When window is minimized, we can't render anything in Windows
  // because it kills the swapchain, so we skip frames in this case.
  if (nextSwapchainImage)
  {
    auto [backbuffer, backbufferView, backbufferAvailableSem] = *nextSwapchainImage;

    ETNA_CHECK_VK_RESULT(currentCmdBuf.begin(vk::CommandBufferBeginInfo{}));
    {
      // First of all, we need to "initialize" th "backbuffer", aka the current swapchain
      // image, into a state that is appropriate for us working with it. The initial state
      // is considered to be "undefined" (aka "I contain trash memory"), by the way.
      // "Transfer" in vulkanese means "copy or blit".
      // Note that Etna sometimes calls this for you to make life simpler, read Etna's code!
      etna::set_state(
        currentCmdBuf,
        backbuffer,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageAspectFlagBits::eColor);
      // The set_state doesn't actually record any commands, they are deferred to
      // the moment you call flush_barriers.
      // As with set_state, Etna sometimes flushes on it's own.
      // Usually, flushes should be placed before "action", i.e. compute dispatches
      // and blit/copy operations.
      etna::flush_barriers(currentCmdBuf);


      // TODO: Record your commands here!

      // --- Constants ---
      float time =
        std::chrono::duration<float>(std::chrono::system_clock::now() - timeStart).count();


      // --- Texture ---
      {
        auto computeInfo = etna::get_shader_program("procedural_texture");

        auto set = etna::create_descriptor_set(
          computeInfo.getDescriptorLayoutId(0),
          currentCmdBuf,
          {
                etna::Binding{0, textureImage.genBinding({}, vk::ImageLayout::eGeneral)}
          });

        vk::DescriptorSet vkSet = set.getVkSet();

        currentCmdBuf.bindPipeline(vk::PipelineBindPoint::eCompute, texturePipeline.getVkPipeline());
        currentCmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eCompute, texturePipeline.getVkPipelineLayout(),
          0, 1, &vkSet, 0, nullptr);

        struct Params
        {
          glm::uvec2 res;
          float time;
        };
        Params param{
            .res = resolution, 
            .time = time
        };
        currentCmdBuf.pushConstants( texturePipeline.getVkPipelineLayout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(param), &param);

        etna::flush_barriers(currentCmdBuf);

        currentCmdBuf.dispatch(resolution.x / 16, resolution.y / 16, 1);
      }


      etna::set_state(
        currentCmdBuf,
        textureImage.get(),
        vk::PipelineStageFlagBits2::eFragmentShader,
        vk::AccessFlagBits2::eShaderSampledRead,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::ImageAspectFlagBits::eColor);

      etna::flush_barriers(currentCmdBuf);


      // --- Drawing ---
      {
        etna::RenderTargetState state{currentCmdBuf, {{}, {resolution.x, resolution.y}}, 
            {{backbuffer, backbufferView}}, {}};

        auto graphicsInfo = etna::get_shader_program("shader");

        auto set = etna::create_descriptor_set(
          graphicsInfo.getDescriptorLayoutId(0),
          currentCmdBuf,
          {
                etna::Binding{0, textureImage.genBinding(textureSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
                etna::Binding{1, fileTextureImage.genBinding(fileTextureSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)}
          });

        vk::DescriptorSet vkSet = set.getVkSet();

        currentCmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, shaderPipeline.getVkPipeline());
        currentCmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, shaderPipeline.getVkPipelineLayout(),
          0, 1, &vkSet, 0, nullptr);

        struct Params
        {
          glm::uvec2 res;
          glm::uvec2 mouse;
          float time;
        };
        Params param
        {
          .res = resolution, 
          .mouse = osWindow->mouse.freePos, 
          .time = time
        };
        currentCmdBuf.pushConstants(shaderPipeline.getVkPipelineLayout(), vk::ShaderStageFlagBits::eFragment, 0, sizeof(param), &param);

        currentCmdBuf.draw(3, 1, 0, 0);
      }

      // At the end of "rendering", we are required to change how the pixels of the
      // swpchain image are laid out in memory to something that is appropriate
      // for presenting to the window (while preserving the content of the pixels!).
      etna::set_state(
        currentCmdBuf,
        backbuffer,
        // This looks weird, but is correct. Ask about it later.
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        {},
        vk::ImageLayout::ePresentSrcKHR,
        vk::ImageAspectFlagBits::eColor);
      // And of course flush the layout transition.
      etna::flush_barriers(currentCmdBuf);
    }
    ETNA_CHECK_VK_RESULT(currentCmdBuf.end());

    // We are done recording GPU commands now and we can send them to be executed by the GPU.
    // Note that the GPU won't start executing our commands before the semaphore is
    // signalled, which will happen when the OS says that the next swapchain image is ready.
    auto renderingDone =
      commandManager->submit(std::move(currentCmdBuf), std::move(backbufferAvailableSem));

    // Finally, present the backbuffer the screen, but only after the GPU tells the OS
    // that it is done executing the command buffer via the renderingDone semaphore.
    const bool presented = vkWindow->present(std::move(renderingDone), backbufferView);

    if (!presented)
      nextSwapchainImage = std::nullopt;
  }

  etna::end_frame();

  // After a window us un-minimized, we need to restore the swapchain to continue rendering.
  if (!nextSwapchainImage && osWindow->getResolution() != glm::uvec2{0, 0})
  {
    auto [w, h] = vkWindow->recreateSwapchain(etna::Window::DesiredProperties{
      .resolution = {resolution.x, resolution.y},
      .vsync = useVsync,
    });
    ETNA_VERIFY((resolution == glm::uvec2{w, h}));
  }
}
