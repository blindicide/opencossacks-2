cmake_minimum_required(VERSION 3.24)

# Версия берётся из единственного источника — src/core/version.hpp.
file(STRINGS src/core/version.hpp _ver REGEX "kVersion = \"[^\"]+\"")
string(REGEX REPLACE ".*kVersion = \"([0-9]+\\.[0-9]+\\.[0-9]+)[^\"]*\".*" "\\1" OC2_VERSION "${_ver}")
# Метка предварительного выпуска (alpha.1 и т. п.), если есть.
set(OC2_PRERELEASE "")
if(_ver MATCHES "kVersion = \"[0-9.]+-([^\"]+)\"")
    set(OC2_PRERELEASE "${CMAKE_MATCH_1}")
endif()

project(opencossacks2 VERSION ${OC2_VERSION} LANGUAGES C CXX)
if(WIN32)
  add_compile_definitions(NOMINMAX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
endif()

include(cmake/Dependencies.cmake)

# Чтение форматов игры; без зависимостей от окна и графики, чтобы им пользовались утилиты и тесты.
add_library(oc2_assets STATIC
    src/assets/ai_script.cpp
    src/assets/audio_decode.cpp
    src/assets/compression.cpp
    src/assets/economy.cpp
    src/assets/g16.cpp
    src/assets/g2d.cpp
    src/assets/game_paths.cpp
    src/assets/image_decode.cpp
    src/assets/map3d.cpp
    src/assets/bigmap_data.cpp
    src/assets/mission_list.cpp
    src/assets/mission_options.cpp
    src/assets/png.cpp
    src/assets/settlement.cpp
    src/assets/sound_list.cpp
    src/assets/sprite_list.cpp
    src/assets/text_encoding.cpp
    src/assets/text_table.cpp
    src/assets/campaign_list.cpp
    src/core/settings.cpp
    src/assets/nation_tech.cpp
    src/assets/unit_desc.cpp
    src/assets/weapon_list.cpp
    src/assets/xml_tree.cpp
)
target_include_directories(oc2_assets PUBLIC src)
target_link_libraries(oc2_assets PRIVATE bz2 stb_headers)

# Мир игры без графики: построение рельефа и (дальше) логика.
add_library(oc2_world STATIC
    src/world/savegame.cpp
    src/world/terrain_mesh.cpp
    src/world/lock_map.cpp
    src/world/market.cpp
    src/world/strategic_ai.cpp
    src/world/mission_script.cpp
    src/world/settlements.cpp
    src/world/unit_catalog.cpp
    src/world/world.cpp
    src/world/squad.cpp
    src/world/bigmap.cpp
    src/net/socket.cpp
    src/net/discovery.cpp
    src/net/session.cpp
)
target_include_directories(oc2_world PUBLIC src)
target_link_libraries(oc2_world PUBLIC oc2_assets)
if(WIN32)
    target_link_libraries(oc2_world PUBLIC ws2_32)
endif()

add_executable(opencossacks2
    src/main.cpp
    src/platform/window.cpp
    src/platform/audio.cpp
    src/platform/sound_bank.cpp
    src/render/vulkan_context.cpp
    src/render/sprite_renderer.cpp
    src/render/sprite_sheet.cpp
    src/render/gp_images.cpp
    src/render/terrain_renderer.cpp
    src/render/map_objects.cpp
    src/render/effects.cpp
    src/ui/fonts.cpp
    src/ui/rich_text.cpp
    src/ui/dialogs.cpp
    src/ui/menus.cpp
    src/ui/menus_net.cpp
    src/ui/bigmap_screen.cpp
)
target_link_libraries(opencossacks2 PRIVATE oc2_assets oc2_world SDL3::SDL3-static volk vma)
if(WIN32)
    set_target_properties(opencossacks2 PROPERTIES WIN32_EXECUTABLE ON)
endif()
target_include_directories(opencossacks2 PRIVATE src)

# Предупреждения для наших целей (не для сторонних библиотек).
function(oc2_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8 /permissive-)
    else()
        # Vulkan-структуры инициализируются как {VK_STRUCTURE_TYPE_...}, остальное — нулями намеренно.
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
    endif()
endfunction()
oc2_warnings(oc2_assets)
oc2_warnings(oc2_world)
oc2_warnings(opencossacks2)

# Утилита выгрузки ресурсов в PNG и проверки чтения форматов.
add_executable(oc2-extract
    tools/oc2-extract/main.cpp
)
target_link_libraries(oc2-extract PRIVATE oc2_assets)
oc2_warnings(oc2-extract)

# Установка и пакеты. Пакеты содержат только наш движок: данные игры
# пользователь берёт из своей легальной копии.
include(GNUInstallDirs)
install(TARGETS opencossacks2 oc2-extract RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})
if(UNIX AND NOT APPLE)
    install(FILES packaging/linux/opencossacks2.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
endif()
include(cmake/Packaging.cmake)

# Тесты на синтетических данных (файлы игры не нужны): ctest --test-dir build/linux -C Debug
option(OC2_BUILD_TESTS "Собирать тесты" ON)
if(OC2_BUILD_TESTS)
    enable_testing()
    add_executable(oc2_assets_test tests/assets_test.cpp)
    target_link_libraries(oc2_assets_test PRIVATE oc2_assets oc2_world bz2)
    oc2_warnings(oc2_assets_test)
    add_test(NAME assets COMMAND oc2_assets_test)
endif()
