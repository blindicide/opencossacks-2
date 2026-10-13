# Внешние зависимости загружаются и собираются вместе с проектом (FetchContent),
# поэтому системные пакеты для них не нужны и сборка одинакова на всех машинах.
include(FetchContent)

FetchContent_Declare(Vulkan-Headers
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG        vulkan-sdk-1.4.350.1
    GIT_SHALLOW    TRUE)

FetchContent_Declare(volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG        1.4.350
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  none) # только загрузка: цель volk собираем ниже сами

FetchContent_Declare(VulkanMemoryAllocator
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG        v3.4.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  none) # только заголовок; реализация — в src/render/vma.cpp

FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.4.18
    GIT_SHALLOW    TRUE)

# SDL собираем статически и без лишних подсистем.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_VULKAN ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(Vulkan-Headers SDL3 volk VulkanMemoryAllocator)

# volk сам загружает Vulkan во время выполнения; линковать libvulkan не нужно.
add_library(volk STATIC ${volk_SOURCE_DIR}/volk.c)
target_include_directories(volk PUBLIC ${volk_SOURCE_DIR})
target_link_libraries(volk PUBLIC Vulkan::Headers ${CMAKE_DL_LIBS})
target_compile_definitions(volk PUBLIC VK_NO_PROTOTYPES)
if(WIN32)
    target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_WIN32_KHR)
endif()

# VMA: функции Vulkan берёт через volk во время выполнения. Заголовок подключается как
# системный, чтобы его предупреждения не смешивались с нашими.
add_library(vma STATIC ${PROJECT_SOURCE_DIR}/src/render/vma.cpp)
target_include_directories(vma SYSTEM PUBLIC ${vulkanmemoryallocator_SOURCE_DIR}/include)
target_link_libraries(vma PUBLIC volk)
target_compile_definitions(vma PUBLIC VMA_STATIC_VULKAN_FUNCTIONS=0 VMA_DYNAMIC_VULKAN_FUNCTIONS=1)
