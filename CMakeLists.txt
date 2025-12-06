cmake_minimum_required(VERSION 3.16)

project(kazad LANGUAGES CXX VERSION 1.2.0)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Qml Network Sql Positioning)
find_package(OpenSSL REQUIRED)

include_directories(src)

add_library(KaZaLib SHARED
    src/kazaobject.h src/kazaobject.cpp
)

if(NOT KAZA_PROTOCOL_DIRECTORY)
    set(KAZA_PROTOCOL_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/protocol")
endif()

add_executable(kazad
  src/main.cpp
  src/kazamanager.h src/kazamanager.cpp
  src/kazaconnection.h src/kazaconnection.cpp
  src/kazaremoteconnection.h src/kazaremoteconnection.cpp
  src/kzobject.h src/kzobject.cpp
  src/kazaelement.h src/kazaelement.cpp
  src/scheduler.h src/scheduler.cpp
  src/kzalarm.h src/kzalarm.cpp
  src/internalobject.h src/internalobject.cpp
  src/kazacertificategenerator.h src/kazacertificategenerator.cpp
  ${KAZA_PROTOCOL_DIRECTORY}/kazaprotocol.h ${KAZA_PROTOCOL_DIRECTORY}/kazaprotocol.cpp
  conf/kazad.service
  conf/postinst
  conf/postrm
)

add_executable(kls
    src/cmd/kls.cpp
    ${KAZA_PROTOCOL_DIRECTORY}/kazaprotocol.h ${KAZA_PROTOCOL_DIRECTORY}/kazaprotocol.cpp
)

target_include_directories(kazad PUBLIC ${KAZA_PROTOCOL_DIRECTORY})
target_include_directories(kls PUBLIC ${KAZA_PROTOCOL_DIRECTORY})
target_link_libraries(KaZaLib Qt6::Core Qt6::Network)
set_target_properties(KaZaLib PROPERTIES VERSION 2.0.0 SOVERSION 2)
target_link_libraries(kazad PRIVATE KaZaLib Qt6::Core Qt6::Qml Qt6::Sql Qt6::Positioning systemd OpenSSL::SSL OpenSSL::Crypto)
target_link_libraries(kls Qt6::Core Qt6::Network OpenSSL::SSL OpenSSL::Crypto)


include(GNUInstallDirs)


install(TARGETS kazad
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME
        COMPONENT bin
        DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(TARGETS kls
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME
        COMPONENT bin
        DESTINATION ${CMAKE_INSTALL_BINDIR}
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
    LIBRARY
        COMPONENT bin
        NAMELINK_COMPONENT dev
        DESTINATION ${CMAKE_INSTALL_LIBDIR}
    PUBLIC_HEADER
        COMPONENT dev
        DESTINATION "${include_install_dir}"
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
    COMPONENT dev
)


install(
    FILES conf/kazad.service
    DESTINATION /lib/systemd/system
    COMPONENT bin
)

install(
    FILES conf/kazad.conf
    DESTINATION /usr/share/kazad
    COMPONENT bin
)

install(
    EXPORT "${targets_export_name}"
    DESTINATION "${config_install_dir}"
    COMPONENT dev
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
    set(CPACK_PACKAGE_VERSION "1.2.0")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_NAME "kazad")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Fabien Proriol <fabien.proriol@kazoe.org>")
    set(CPACK_PACKAGE_VERSION_PATCH "${CMAKE_PROJECT_VERSION_PATCH}-deb${DEBIAN_MAJOR}")

    set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "KaZa server")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt6sql6, qml6-module-qtqml, openssl")
    set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
    set(CPACK_DEBIAN_BIN_PACKAGE_CONTROL_EXTRA "${CMAKE_CURRENT_SOURCE_DIR}/conf/postinst;${CMAKE_CURRENT_SOURCE_DIR}/conf/postrm")

    set(CPACK_DEBIAN_DEV_PACKAGE_NAME "${CPACK_DEBIAN_PACKAGE_NAME}-dev")
    set(CPACK_COMPONENT_DEV_DESCRIPTION "KaZa server developpement files")
    set(CPACK_DEBIAN_DEV_PACKAGE_DEPENDS "${CPACK_DEBIAN_PACKAGE_NAME}-bin")

    set(CPACK_COMPONENTS_ALL bin dev)
    set(CPACK_DEB_COMPONENT_INSTALL ON)

    include(CPack)
endif()
