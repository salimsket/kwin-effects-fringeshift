cmake_minimum_required(VERSION 3.20)

# C is enabled only because KF6WindowSystem's config runs a C try_compile
# (CheckLibraryExists for X11) while resolving KWin's dependencies.
project(fringeshift VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# main.cpp has no literal Q_OBJECT — the effect factory macro expands to one.
# Without this, AutoMoc still generates main.moc but warns on every build that
# the include looks unbacked.
list(APPEND CMAKE_AUTOMOC_MACRO_NAMES KWIN_EFFECT_FACTORY_SUPPORTED)

find_package(ECM 6.0 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

# Widgets and DBus are for the config module only: the effect itself is drawn by
# the compositor and talks to nobody.
find_package(Qt6 REQUIRED COMPONENTS Core DBus Gui Widgets)
find_package(KF6 REQUIRED COMPONENTS Config ConfigWidgets CoreAddons GlobalAccel I18n KCMUtils WidgetsAddons)

# Effect plugins are not binary compatible across KWin releases: the plugin must
# be built against the same KWin it will be loaded into.
find_package(KWin REQUIRED)

message(STATUS "Building against KWin ${KWin_VERSION}")

# KWin <= 6.6 rewrites a shader path to its "_core" variant on a core-profile
# context; 6.7 dropped that and loads the path verbatim. Ship the one authored
# shader under both names so either resolver finds it.
configure_file(src/shaders/fringeshift.frag ${CMAKE_CURRENT_BINARY_DIR}/shaders/fringeshift.frag COPYONLY)
configure_file(src/shaders/fringeshift.frag ${CMAKE_CURRENT_BINARY_DIR}/shaders/fringeshift_core.frag COPYONLY)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/fringeshift.qrc
"<!DOCTYPE RCC><RCC version=\"1.0\">
<qresource prefix=\"/effects/fringeshift/\">
  <file>shaders/fringeshift.frag</file>
  <file>shaders/fringeshift_core.frag</file>
</qresource>
</RCC>
")

# The shader's parameter set and its preconditions. Split out from the plugin so
# that the measurement harness, which must not link KWin, uploads uniforms
# through exactly the same code the compositor does.
add_library(fringeshiftparams STATIC src/fringeshiftparams.cpp)

kconfig_add_kcfg_files(fringeshiftparams
    src/fringeshiftconfig.kcfgc
)

set_target_properties(fringeshiftparams PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(fringeshiftparams PUBLIC
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ConfigGui, not ConfigCore: kconfig_compiler derives the generated class from
# KConfigSkeleton, which lives in KF6::ConfigGui.
target_link_libraries(fringeshiftparams PUBLIC
    KF6::ConfigGui
    Qt6::Core
    Qt6::Gui
)

kcoreaddons_add_plugin(fringeshift
    INSTALL_NAMESPACE "kwin/effects/plugins"
    SOURCES
        src/main.cpp
        src/fringeshifteffect.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/fringeshift.qrc
)

# 6.7 replaced GLShader::isValid() with a nullptr return from the loader.
if(KWin_VERSION VERSION_LESS 6.7)
    target_compile_definitions(fringeshift PRIVATE FRINGESHIFT_GLSHADER_HAS_ISVALID)
endif()

target_link_libraries(fringeshift PRIVATE
    fringeshiftparams
    KWin::kwin
    KF6::GlobalAccel
    KF6::I18n
)

# The "Configure..." page in System Settings -> Desktop Effects. A *separate*
# plugin from the effect, and KWin only finds it by prepending
# kwin/effects/configs/ to the effect metadata's X-KDE-ConfigModule — so the
# target name, that metadata value and this install namespace all have to agree.
#
# It links fringeshiftparams for the same generated FringeShiftSettings the
# effect uses. Regenerating the .kcfgc into this target instead would put two
# copies of a singleton in the systemsettings process, where the second
# instance() call is dropped with only a qDebug line.
kcoreaddons_add_plugin(kwin_fringeshift_config
    INSTALL_NAMESPACE "kwin/effects/configs"
    SOURCES
        src/kcm/FringeShiftKCM.cpp
        src/kcm/plugin.cpp
)

# ki18n_wrap_ui, not qt6_wrap_ui: it runs uic so the form's strings come out as
# i18n() calls rather than tr(), which is what puts them in the KDE catalogue.
ki18n_wrap_ui(kwin_fringeshift_config src/kcm/FringeShiftKCM.ui)

target_link_libraries(kwin_fringeshift_config PRIVATE
    fringeshiftparams
    KF6::ConfigWidgets   # KConfigDialogManager, behind addConfig()
    KF6::CoreAddons
    KF6::I18n
    KF6::KCMUtils        # KCModule
    KF6::WidgetsAddons   # KMessageWidget, the sub-pixel-rendering notice
    Qt6::DBus
    Qt6::Widgets
)

option(FRINGESHIFT_BUILD_TESTS "Build the off-screen shader measurement harness" ON)

if(FRINGESHIFT_BUILD_TESTS)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(Epoxy REQUIRED IMPORTED_TARGET epoxy)
    pkg_check_modules(EGL REQUIRED IMPORTED_TARGET egl)

    # Deliberately does NOT link KWin: the point of the harness is to exercise
    # the shipped shader with no compositor present.
    add_executable(fringeshift-measure
        tests/shaderrunner.cpp
        tests/measure.cpp
    )

    target_link_libraries(fringeshift-measure PRIVATE
        fringeshiftparams
        PkgConfig::Epoxy
        PkgConfig::EGL
    )

    # KDECompilerSettings builds KDE code with -fno-exceptions. The harness is
    # not loaded into the compositor, and reporting GL setup failure as an
    # exception keeps the measurement code free of error plumbing.
    kde_target_enable_exceptions(fringeshift-measure PRIVATE)

    enable_testing()
    add_test(NAME shader-measurements
        COMMAND fringeshift-measure
            ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/fringeshift.frag
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/stubs
    )
    # No headless GL driver on this host is a skip, not a shader failure.
    set_tests_properties(shader-measurements PROPERTIES SKIP_RETURN_CODE 77)
endif()
