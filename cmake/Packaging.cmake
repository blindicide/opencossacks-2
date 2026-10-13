# Пакеты .deb и .rpm (Linux) и .zip (Windows) через CPack.
set(CPACK_PACKAGE_NAME "opencossacks2")
set(CPACK_PACKAGE_VENDOR "opencossacks-2")
set(CPACK_PACKAGE_CONTACT "blindicide <216763350+blindicide@users.noreply.github.com>")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/blindicide/opencossacks-2")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Открытая реимплементация «Казаки II: Битва за Европу»")
set(CPACK_PACKAGE_DESCRIPTION
    "Движок на C++20 и Vulkan. Требует данные игры из легальной копии (например, GOG).")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_STRIP_FILES ON)

if(WIN32)
    set(CPACK_GENERATOR "ZIP")
else()
    set(CPACK_GENERATOR "DEB;RPM")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")

    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_SECTION "games")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON) # зависимости от системных библиотек — автоматически
    # Vulkan загружается во время выполнения (volk), поэтому shlibdeps его не видит.
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libvulkan1")

    set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
    set(CPACK_RPM_PACKAGE_GROUP "Amusements/Games")
    set(CPACK_RPM_PACKAGE_REQUIRES "vulkan-loader")
endif()

include(CPack)
