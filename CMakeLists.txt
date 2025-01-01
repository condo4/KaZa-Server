cmake_minimum_required(VERSION 3.16)

project(KaZaServer LANGUAGES CXX VERSION 1.0.0)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Qml Network Sql)

include_directories(src)

add_library(KaZaLib SHARED
    src/kazaobject.h src/kazaobject.cpp
)

if(NOT KAZA_PROTOCOL_DIRECTORY)
    set(KAZA_PROTOCOL_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/protocol")
endif()

add_executable(KaZaServer
  src/main.cpp
  src/kazamanager.h src/kazamanager.cpp
  src/kazaconnection.h src/kazaconnection.cpp
  src/kazaremoteconnection.h src/kazaremoteconnection.cpp
  ${KAZA_PROTOCOL_DIRECTORY}/kazaprotocol.h ${KAZA_PROTOCOL_DIRECTORY}/kazaprotocol.cpp
)

target_include_directories(KaZaServer PUBLIC ${KAZA_PROTOCOL_DIRECTORY})
target_link_libraries(KaZaLib Qt6::Core Qt6::Network)
target_link_libraries(KaZaServer KaZaLib Qt6::Core Qt6::Qml Qt6::Sql)

include(GNUInstallDirs)


install(TARGETS KaZaServer
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)


# CMake Module
set(CMakeModuleName "KaZa")
set(config_install_dir "lib/cmake/${CMakeModuleName}")
set(include_install_dir "${CMAKE_INSTALL_INCLUDEDIR}/${CMakeModuleName}")
set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(version_config "${generated_dir}/${CMakeModuleName}ConfigVersion.cmake")
set(project_config "${generated_dir}/${CMakeModuleName}Config.cmake")
set(targets_export_name "${CMakeModuleName}Targets")
include(CMakePackageConfigHelpers)


set(KAZA_HEADERS src/kazaobject.h )
set_target_properties(KaZaLib PROPERTIES PUBLIC_HEADER "${KAZA_HEADERS}")

install(
    TARGETS "KaZaLib"
    EXPORT "${targets_export_name}"
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    PUBLIC_HEADER DESTINATION "${include_install_dir}"
)
write_basic_package_version_file(
    "${version_config}" COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "cmake/Config.cmake.in"
    "${project_config}"
    INSTALL_DESTINATION "${config_install_dir}"
)

install(
    FILES "${project_config}" "${version_config}"
    DESTINATION "${config_install_dir}"
)

install(
    EXPORT "${targets_export_name}"
    DESTINATION "${config_install_dir}"
)


if(NOT CMAKE_CROSSCOMPILING AND NOT DISABLE_PKG)
    if(EXISTS /etc/debian_version)
        message(STATUS "Build for debian package")
        set(BUILD_DEBIAN_PACKAGE ON)
    endif()
endif()

if(BUILD_DEBIAN_PACKAGE)
    set(DEB_DEPEND "")
    file(READ /etc/debian_version DEBIAN_VERSION)
    string(REGEX MATCH "^[0-9]+" DEBIAN_MAJOR "${DEBIAN_VERSION}")
    set(CPACK_GENERATOR "DEB")
    set(CPACK_PACKAGE_NAME "kaza-server")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_PACKAGE_VERSION_PATCH "${CMAKE_PROJECT_VERSION_PATCH}-debian${DEBIAN_MAJOR}")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt6sql6 qml6-module-qtqml")
    set(CPACK_PACKAGE_DESCRIPTION "KaZa Server")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Fabien Proriol <fabien.proriol@kazoe.org>")
    include(CPack)
endif()
