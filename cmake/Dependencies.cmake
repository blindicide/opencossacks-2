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

# bzip2: исходники официального выпуска; собираем только библиотеку (ниже).
FetchContent_Declare(bzip2
    URL      https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz
    URL_HASH SHA256=ab5a03176ee106d3f0fa90e381da478ddae405918153cca248e682cd0c4a2269
    SOURCE_SUBDIR none)

# stb_image (общественное достояние): JPG, TGA, BMP и PNG интерфейса.
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        2c980bb59875b0d32144a71867fbdebb2f77cd20
    SOURCE_SUBDIR  none)

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

FetchContent_MakeAvailable(Vulkan-Headers SDL3 volk VulkanMemoryAllocator bzip2 stb)

# volk сам загружает Vulkan во время выполнения; линковать libvulkan не нужно.
add_library(volk STATIC ${volk_SOURCE_DIR}/volk.c)
target_include_directories(volk PUBLIC ${volk_SOURCE_DIR})
target_link_libraries(volk PUBLIC Vulkan::Headers ${CMAKE_DL_LIBS})
target_compile_definitions(volk PUBLIC VK_NO_PROTOTYPES)
if(WIN32)
    # windows.h (его подключают volk и VMA) без NOMINMAX определяет макросы min и max,
    # которые ломают std::min и std::max.
    target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_WIN32_KHR NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# VMA: функции Vulkan берёт через volk во время выполнения. Заголовок подключается как
# системный, чтобы его предупреждения не смешивались с нашими.
add_library(vma STATIC ${PROJECT_SOURCE_DIR}/src/render/vma.cpp)
target_include_directories(vma SYSTEM PUBLIC ${vulkanmemoryallocator_SOURCE_DIR}/include)
target_link_libraries(vma PUBLIC volk)
target_compile_definitions(vma PUBLIC VMA_STATIC_VULKAN_FUNCTIONS=0 VMA_DYNAMIC_VULKAN_FUNCTIONS=1)

# libbz2 для распаковки .g17 (метод 1 сжатых блоков движка).
add_library(bz2 STATIC
    ${bzip2_SOURCE_DIR}/blocksort.c
    ${bzip2_SOURCE_DIR}/huffman.c
    ${bzip2_SOURCE_DIR}/crctable.c
    ${bzip2_SOURCE_DIR}/randtable.c
    ${bzip2_SOURCE_DIR}/compress.c
    ${bzip2_SOURCE_DIR}/decompress.c
    ${bzip2_SOURCE_DIR}/bzlib.c)
target_include_directories(bz2 SYSTEM PUBLIC ${bzip2_SOURCE_DIR})
set_target_properties(bz2 PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_definitions(bz2 PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(bz2 PRIVATE /w)
else()
    target_compile_options(bz2 PRIVATE -w)
endif()

# Реализация stb_image — в src/assets/image_decode.cpp; заголовок системный, чтобы
# не смешивать его предупреждения с нашими.
add_library(stb_headers INTERFACE)
target_include_directories(stb_headers SYSTEM INTERFACE ${stb_SOURCE_DIR})
