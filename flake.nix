{
  description = "Fringe Shift — a KWin (Plasma 6) effect plugin";

  inputs = {
    # Pinned to the revision this machine's NixOS generation was built from, so
    # kdePackages.kwin here is bit-identical to the KWin that will load the
    # plugin. Effect plugins are NOT binary compatible across KWin releases, so
    # bump this together with the system — `nixos-version --json` prints the
    # revision, and `nix flake update` alone will silently break the plugin.
    nixpkgs.url = "github:NixOS/nixpkgs/04607e1165ac22c5fde6dcc54c9e0b3c0487c555";
  };

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system:
        f (import nixpkgs { inherit system; }));
    in
    {
      packages = forAllSystems (pkgs: rec {
        default = fringeshift;

        fringeshift = pkgs.stdenv.mkDerivation {
          pname = "fringeshift";
          version = "0.2.2";

          # Explicit file set: keeps local build/ and result/ out of the store
          # path, so a dirty working tree does not change the derivation hash.
          src = pkgs.lib.fileset.toSource {
            root = ./.;
            fileset = pkgs.lib.fileset.unions [
              ./CMakeLists.txt
              ./src
              ./tests
            ];
          };

          nativeBuildInputs = with pkgs; [
            cmake
            ninja
            pkg-config
            kdePackages.extra-cmake-modules
            kdePackages.qttools        # lrelease / lupdate for i18n
            kdePackages.wrapQtAppsHook
          ];

          buildInputs = with pkgs; [
            # KWin itself: provides the KWinEffects/KWin::kwin CMake package and
            # the effect plugin headers (effect/effect.h, effect/offscreeneffect.h, ...).
            kdePackages.kwin

            kdePackages.qtbase
            kdePackages.qtdeclarative  # QML config UI + KWin's own QML deps

            kdePackages.kconfig
            kdePackages.kconfigwidgets
            kdePackages.kglobalaccel    # Ctrl+Alt+F9 toggle
            kdePackages.kcmutils       # KCM for the effect's settings page
            kdePackages.kcoreaddons
            kdePackages.ki18n
            kdePackages.kwindowsystem

            libepoxy                   # GL entry points, as used by KWin
            wayland
            libdrm

            # Off-screen measurement harness: EGL headers via libglvnd, and
            # Mesa so a sandboxed check has llvmpipe to render into. Without a
            # usable driver the harness exits 77 and ctest reports a skip.
            libglvnd
            mesa
          ];

          doCheck = true;
          checkPhase = ''
            runHook preCheck

            # The sandbox has no GPU and no glvnd config, so point EGL at Mesa's
            # vendor JSON and force the llvmpipe software rasteriser. Without
            # this the harness finds no display, exits 77, and ctest reports a
            # skip — correct, but it would mean the shader is never measured.
            export __EGL_VENDOR_LIBRARY_FILENAMES=${pkgs.mesa}/share/glvnd/egl_vendor.d/50_mesa.json
            export LIBGL_DRIVERS_PATH=${pkgs.mesa}/lib/dri
            export LIBGL_ALWAYS_SOFTWARE=1
            export GALLIUM_DRIVER=llvmpipe

            ctest --output-on-failure
            runHook postCheck
          '';

          cmakeFlags = [
            (pkgs.lib.cmakeFeature "CMAKE_BUILD_TYPE" "Release")
            # KWin loads effect plugins from <qt-plugin-dir>/kwin/effects/plugins.
            # KDEInstallDirs resolves that relative to CMAKE_INSTALL_PREFIX, so the
            # plugin lands in $out and gets picked up via QT_PLUGIN_PATH.
            (pkgs.lib.cmakeFeature "KDE_INSTALL_USE_QT_SYS_PATHS" "OFF")
          ];

          meta = with pkgs.lib; {
            description = "Per-channel subpixel fringe correction as a KWin effect";
            platforms = platforms.linux;
            license = licenses.gpl3Plus;
          };
        };
      });

      devShells = forAllSystems (pkgs: {
        default = pkgs.mkShell {
          inputsFrom = [ self.packages.${pkgs.stdenv.hostPlatform.system}.fringeshift ];

          packages = with pkgs; [
            # toolchain — these are what were missing from the bare shell
            cmake
            ninja
            gnumake
            gcc
            pkg-config

            # day-to-day tooling
            clang-tools        # clangd + clang-format
            gdb
            kdePackages.kwin   # kwin_wayland --replace, kwin-console for nested tests
            kdePackages.plasma-workspace
            qt6.qttools
          ];

          shellHook = ''
            export CMAKE_EXPORT_COMPILE_COMMANDS=ON
            # Let a locally built effect be found without installing system-wide:
            export QT_PLUGIN_PATH="$PWD/build/bin:''${QT_PLUGIN_PATH:-}"

            echo "fringeshift devshell — KWin effect (Plasma 6)"
            echo "  configure : cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug"
            echo "  build     : cmake --build build"
            echo "  install   : cmake --install build --prefix ~/.local"
            echo "  test      : kwin_wayland --xwayland --width 1600 --height 900 -- konsole"
          '';
        };
      });

      formatter = forAllSystems (pkgs: pkgs.nixfmt);
    };
}
