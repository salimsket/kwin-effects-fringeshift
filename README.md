> [!CAUTION]
> This is a 100% Claude generated repository.

# FringeShift

<img width="1450" height="514" alt="kde-effects-fringeshift" src="https://github.com/user-attachments/assets/4814f9da-b522-441d-b58d-5ebb625dfab7" />

A KWin (Plasma 6) effect that kills QD-OLED colour fringing — the green halo
above edges and the warm one below — by shifting each colour channel's content
a fraction of a pixel the other way. Whole screen, not just text.

Ported from FringeShift by Resolve:
[How I fixed text on my QD-OLED monitor (not MacType)](https://www.youtube.com/watch?v=StNJVwCgMKY)

## KWin Effect config

<img width="1154" height="787" alt="image" src="https://github.com/user-attachments/assets/2c997fd8-3d6a-462a-9218-59c65db6d861" />

## Requirements

You need Plasma 6 and, critically, **KWin's development headers for the exact
KWin you are running** — effect plugins are not binary compatible across KWin
releases, so a plugin built against 6.5 will not load into 6.6. Check yours with
`kwin_wayland --version`.

| Need | Minimum | Built and tested against |
| --- | --- | --- |
| C++ compiler | C++20 (GCC 11 / Clang 14) | GCC 15.2 |
| CMake | 3.20 | 4.x |
| Ninja | any | — |
| extra-cmake-modules (ECM) | 6.0 | 6.26.0 |
| Qt 6 | 6.6 (Plasma 6 floor) | 6.11.1 — Core, DBus, Gui, Widgets |
| KDE Frameworks 6 | 6.0 | 6.26.0 — Config, ConfigWidgets, CoreAddons, GlobalAccel, I18n, KCMUtils, WidgetsAddons |
| KWin | Plasma 6 | **6.6.6**, **6.8.2** — dev package, provides `KWinConfig.cmake` + `effect/*.h` |
| libepoxy + EGL | any | tests only; skip with `-DFRINGESHIFT_BUILD_TESTS=OFF` |

KWin ≤ 6.6, 6.7 and 6.8+ are handled — the build detects `KWin_VERSION` and
switches four version shims. Two of them are new in 6.8: `drawWindow()`
returns `bool`, and the offscreen texture's y axis points the other way.

**Arch / CachyOS / EndeavourOS** (kwin ships its own headers, no `-devel` split):

```sh
sudo pacman -S --needed base-devel cmake ninja extra-cmake-modules \
    qt6-base kwin kconfig kconfigwidgets kcoreaddons kglobalaccel \
    ki18n kcmutils kwidgetsaddons libepoxy libglvnd
```

**Fedora / Nobara:**

```sh
sudo dnf install gcc-c++ cmake ninja-build extra-cmake-modules \
    qt6-qtbase-devel kwin-devel kf6-kconfig-devel kf6-kconfigwidgets-devel \
    kf6-kcoreaddons-devel kf6-kglobalaccel-devel kf6-ki18n-devel \
    kf6-kcmutils-devel kf6-kwidgetsaddons-devel libepoxy-devel mesa-libEGL-devel
```

**Debian 13+ / Ubuntu 24.10+:**

```sh
sudo apt install build-essential cmake ninja-build extra-cmake-modules \
    qt6-base-dev kwin-dev libkf6config-dev libkf6configwidgets-dev \
    libkf6coreaddons-dev libkf6globalaccel-dev libkf6i18n-dev \
    libkf6kcmutils-dev libkf6widgetsaddons-dev libepoxy-dev libegl-dev
```

**openSUSE Tumbleweed:** the same set, `kwin6-devel` plus the `kf6-*-devel`
packages.

If `find_package(KWin)` fails, that is the one to chase — it is the least
consistently packaged piece and on some distros it is not shipped at all, in
which case you need KWin's own source tree or the Nix path below.

## Quick start

**0. Turn off font sub-pixel rendering first.** It displaces the same three
channels inside the glyph rasteriser, so the two corrections stack and fringing
gets *worse*.

```sh
kwriteconfig6 --file kdeglobals --group General --key XftSubPixel none
```

Log out and back in for that to reach running apps.

**1. Build.**

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The configure step prints `Building against KWin <version>` — check that it is
the KWin you actually run.

**2. Install.**

```sh
cmake --install build --prefix ~/.local
```

**3. Restart the compositor.**

```sh
kwin_wayland --replace &      # or just log out and back in
```

**4. Enable it** in *System Settings → Desktop Effects → Fringe Shift*.
**Meta+Ctrl+F9** toggles it at runtime for A/B comparison.

If the effect does not appear in that list, KWin did not find the plugin. It
searches Qt's plugin paths for `kwin/effects/plugins/`, and whether `~/.local`
is on them depends on the distro (Fedora and openSUSE install to `lib64`, which
often is not). Either point Qt at it —

```sh
export QT_PLUGIN_PATH=$HOME/.local/lib/qt-6/plugins:$QT_PLUGIN_PATH   # lib64 on Fedora/openSUSE
```

— set for the session rather than one shell, or install system-wide instead
with `sudo cmake --install build --prefix /usr`.

> [!WARNING]
> Rebuild after every Plasma update. An effect plugin is **not** binary
> compatible across KWin releases; a stale one silently fails to load and the
> effect vanishes from the list.

To see whether KWin loaded the plugin:

```sh
qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.isEffectSupported fringeshift   # true
journalctl --user -b | grep -i "fringeshift.*different version"                    # no output
```

A plugin built for a different KWin logs `Effect "fringeshift" was built for a
different version of KWin and needs to be rebuilt`. KWin then marks the effect
unsupported, and System Settings hides it. KWin uses the first copy it finds on
`QT_PLUGIN_PATH`. Thus a stale copy earlier on the path (for example in
`~/.nix-profile`) hides a correct copy later on the path.

### With Nix (optional)

`nix develop` gives the whole toolchain and every header above, so none of the
distro packages are needed:

```sh
nix develop
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
```

`nix build .#` builds and runs the test suite, leaving the plugin at
`result/lib/qt-6/plugins/kwin/effects/plugins/fringeshift.so`. On NixOS add the
flake package to `environment.systemPackages` rather than installing to
`~/.local`, which is not on KWin's plugin path there.

`flake.nix` pins nixpkgs to the revision this machine's system was built from,
so `kdePackages.kwin` in the shell is bit-identical to the KWin that will load
the plugin. Bump that pin together with the system — `nix flake update` on its
own breaks the ABI match.

## Configuration

**Configure…** beside the effect in *Desktop Effects* opens a page with a slider
and spin box per channel axis, the sharpen amount, and the resample toggles.
It writes the keys below and tells the compositor to re-read them, so Apply is
enough.

A row of preset buttons loads five archived parameter sets — **MPE** and **SPE**
(two readings of the panel's emitter geometry), **Osorio** (a reference diagram,
tuned by hand on live hardware), **OSORIO-321URX** (Osorio checked under a
microscope, with `LinearLight`; what ships), and **Triad** (a conservative
floor). A button only
fills the controls; Apply still commits and Reset still undoes.

Same thing from the shell — `kwinrc`, group `[Effect-fringeshift]`, physical
pixels, `+X` right and `+Y` down:

```sh
kwriteconfig6 --file kwinrc --group Effect-fringeshift --key GreenY 0.12
kwriteconfig6 --file kwinrc --group Effect-fringeshift --key RedY -- -0.06
busctl --user call org.kde.KWin /Effects org.kde.kwin.Effects reconfigureEffect s fringeshift
```

`reconfigureEffect` is the call that reloads this effect. The general
`/KWin reconfigure` does not. A negative value needs `--` before it, or
`kwriteconfig6` reads the minus sign as an option.

| Key | Default | Meaning |
| --- | --- | --- |
| `RedX` / `RedY` | `0.200` / `-0.290` | red content shift, px |
| `GreenX` / `GreenY` | `0.130` / `0.200` | green content shift, px |
| `BlueX` / `BlueY` | `-0.290` / `-0.290` | blue content shift, px |
| `Sharpen` | `0.0` | unsharp amount on top of the shift, 0 – 1.5 |
| `Bicubic` | `true` | Catmull-Rom resample; `false` = bilinear (softer, no ringing) |
| `LinearLight` | `true` | area resample in linear light: the light moves by exactly the shift, at any contrast. Overrides `Bicubic` — see [below](#nominal-versus-delivered-shift) |
| `Adaptive` | `true` | gate `Sharpen` to text-like pixels (never gates the shift) |
| `ShowMask` | `false` | debug: render the text mask instead of the image |

Shifts are clamped to ±1 px. `ShowMask` is a debug flag with no widget on the
settings page; set it by hand. With every shift at 0 and `Sharpen` at 0 the
effect unredirects every window, so an all-zero configuration costs nothing.

The defaults are the emitter offsets measured on the panel, negated: green
0.49 px above the red/blue pair (the green/magenta fringe itself), red and
blue 0.49 px apart horizontally (red on left edges, blue on right), and green
0.07 px right of red. Where they come from is
[below](#where-the-default-numbers-come-from).

## Known limits

- Every non-fullscreen window pays one offscreen pass per frame while the
  effect is enabled.
- A corrected window goes through `OffscreenEffect`'s 8-bit sRGB buffer,
  whatever its own colour description is. With HDR on, HDR and wide-gamut
  content in a corrected window is clipped to SDR sRGB.
- **Fullscreen windows are not corrected**, on purpose. A redirected window
  counts as having an effect, so KWin cannot give it direct scanout, and its
  HDR is lost to the buffer above. Fullscreen games and video are where both
  matter, and they are not the text this effect is for. A window leaves the
  correction when it goes fullscreen and comes back when it leaves fullscreen.
  A borderless window that only *covers* the screen is not fullscreen, and
  stays corrected.
- Alpha is not shifted, so window shadows and rounded corners stay intact; the
  premultiplied RGB near an alpha edge is filtered along with the colour.
- Shifts are in physical pixels and correct on HiDPI, but they assume the panel
  is in its native orientation — a rotated output has a rotated triad, which
  this does not model.
- Nothing outside the compositor is corrected, e.g. a client that KWin hands
  straight to the display controller — the same honest limit the Windows
  version has with fullscreen-exclusive apps.
- A configured shift is a nominal, not a delivered, displacement — see
  [gain](#nominal-versus-delivered-shift). With `LinearLight` on, the light
  moves by exactly the configured shift.

---

# Technical detail

Everything past this point is rationale and measurement. None of it is needed to
use the effect.

## Why a uniform shift

The QD-OLED triad puts **green at the top vertex** and **red/blue at the bottom
corners**, so a nominally grey edge shows a green halo above and a warm fringe
below. A constant sub-pixel shift is a convolution whose DC transfer is exactly
1, so it cancels the fringe without changing apparent stroke weight — which is
why the shift is applied uniformly and is never scaled per pixel by the text
mask.

This effect is the broader of the two available corrections: font sub-pixel
rendering reaches text only, whereas this corrects the whole window, icons and
UI chrome included.

## Where the default numbers come from

**OSORIO-321URX**, the default, is Osorio checked under a USB microscope. Solid
red, green and blue frames, fitted over the whole emitter lattice in two
sessions at different zoom, put red at (−0.07, +0.49) and blue at
(+0.41 … +0.43, +0.49) px relative to green. The content shifts are those
offsets negated. The vertical 0.49 confirms Osorio's 0.50. Horizontally the
red–blue spacing agrees, and green sits 0.08 px further right than Osorio
has it. With `LinearLight` on, the light moves by exactly these amounts, so
the measured geometry is the setting; no tuning factor sits between them.

**Osorio**, the reading it refines, was read directly off a labelled reference
diagram of this panel's subpixel layout — two sample points per emitter, so the repeating tile is visible, not
just one cell — and confirmed by eye on live hardware with `fringe-tune`.
Content shift is the emitter's position negated (an emitter sitting high or
left gets a positive, i.e. opposite, correction), except `GreenX`, which the
diagram places close enough to the R/B horizontal midpoint that live tuning,
not the diagram, got the final sign.

The values are written *uncentred* — `RedY`/`BlueY` −0.290 against `GreenY`
0.200. The shader subtracts the midpoint of the smallest and largest of the
three before resampling, so what reaches the resample is ±0.245 on every
channel vertically and no channel carries a larger fraction; writing the same
geometry pre-centred is bit-identical.

`presets.conf` (`OSORIO-321URX`, `OSORIO`) carries the measurements, the diagram coordinates and the full
derivation, plus twenty synthetic variants for by-eye comparison via
`fringe-presets`. `presets.legacy.conf` holds the earlier readings this
superseded — a lattice average from a macro photograph (0.437/0.449 px), a
single-cell scale drawing (0.472/0.441 px), and their consensus — along with
the sets that never earned a preset button. Nothing in either file is read at
runtime.

## Nominal versus delivered shift

A configured shift is a **nominal**, not a delivered, displacement. Catmull-Rom
is an interpolating kernel, not an ideal one, so its phase response
under-delivers the requested offset by an amount that grows with frequency:
measured 0.982 of nominal at f = 1/16 cyc/px and 0.735 at f = 0.25, the
text-edge end. Ask for the default geometry's ±0.245 px of centred green on
this path and roughly 0.180 px arrives where it matters most.

Gain depends on the fraction as well as the frequency — the in-phase Laplacian
term grows as its square and partly offsets the kernel's phase lag, so smaller
shifts deliver a slightly smaller proportion (the 0.225 px default that shipped
before OSORIO read 0.733 at f = 0.25, and a 0.10 px one before that read
0.680). The response stays proportional to within a few percent (see
`proportional-shift` below), so this is a scale factor to be aware of when
reading a preset, not a non-linearity.

### In light

The numbers above are in code values. The panel adds light, so what moves a
fringe is where each channel's *light* goes. On the default path, two things
make less light move than the code values suggest:

- On a black/white edge, Catmull-Rom's negative lobes and the Laplacian term
  overshoot below 0 and above 1. The clamp cuts that off, and the shift that
  the overshoot carried is lost.
- The rest is weighted by code value, and code values near black carry almost
  no light.

`linear-light-shift` measures it. With the default geometry (0.490 px between
green and red/blue), this path moves the light 0.286 px on a black/white edge
and 0.473 px on a 10 % / 90 % edge. So the same setting is a different correction
on different content. A microscope session on this panel showed it: the
emitter offset is 0.49 px, but the best setting by eye was 0.60–0.65 on
black text and lower on mid-contrast content.

**`LinearLight`** removes both losses. It decodes each tap to light with
KWin's own `encodingToNits()`, resamples with the tent, and adds no
compensation. This is the physical model of the panel: an emitter that sits a
fraction `f` off the pixel centre collects `1 − f` of one pixel's light and
`f` of its neighbour's. The weights are non-negative, so nothing overshoots.
Each channel's light moves by exactly its shift, at any contrast, and its
total light stays the same. With it on, set the shifts to the emitter
geometry itself: the defaults are that geometry.

The cost is the MTF compensation, which this path does not have. Fine detail
along the shift axis gets the softness of the area resample. `LinearLight` is
on by default. Turn it off to get the Catmull-Rom path and its compensation
back; then a shift needs a larger setting than the geometry, by an amount
that depends on the content.

## How it works

`OffscreenEffect` redirects each window into a texture, then redraws it through
[fringeshift.frag](src/shaders/fringeshift.frag):

- the three shifts are centred about zero, so no channel carries a larger
  fractional offset than the others (an uncentred set makes grey text read
  reddish — measured 16 % chromatic mismatch at the previous default, G +0.30
  / RB −0.15; OSORIO's own raw values, G +0.20 / RB −0.30, would carry the
  same kind of mismatch if the shader skipped this step);
- each shift splits into an exact integer texel step plus a fraction; only the
  fraction is filtered;
- per channel, a Catmull-Rom (or bilinear) resample gives true sub-pixel
  positioning, and a Laplacian term flattens the interpolator's MTF so the
  resample does not soften: |MTF−1| at a 0.10 px fraction drops from 0.200 to
  0.013 for bilinear, 0.056 to 0.011 for Catmull-Rom. That compensation is
  applied **per axis** — the resample is separable, so a horizontal fraction
  costs MTF horizontally and nothing more, and a purely vertical correction
  must leave horizontal detail untouched;
- one 5×5 texel window feeds all three channels' resamples and Laplacians:
  25 texture fetches per pixel. Fetching per channel took 64. The bound holds
  because every shift is clamped to ±1 px, so each centred shift is within
  ±1 px too;
- by default the resample runs in the source encoding, which keeps the mean
  code value of every edge. `LinearLight` runs it in light instead, which
  keeps each channel's light and moves it by exactly the shift — see
  [In light](#in-light).

## KWin version differences

The build reads `KWin_VERSION` and selects four shims:

| KWin | Change | Shim |
| --- | --- | --- |
| 6.7 | `GLShader::isValid()` removed; the loader returns null on failure | `FRINGESHIFT_GLSHADER_HAS_ISVALID` (≤ 6.6) |
| 6.7 | No `_core` rewrite of shader paths on a core-profile context | shader installed as both `fringeshift.frag` and `fringeshift_core.frag` |
| 6.8 | `Effect::drawWindow()` returns `[[nodiscard]] bool`; `OffscreenEffect` returns `false` when it cannot get a buffer | `FRINGESHIFT_DRAWWINDOW_RETURNS_BOOL` |
| 6.8 | The offscreen texture's y axis points down the screen | `@FRINGESHIFT_AXIS_FLIP_Y@` in the shader |

The 6.8 axis change comes from how `OffscreenEffect`
(`src/effect/offscreeneffect.cpp` in KWin) makes the window texture:

| | KWin 6.6 / 6.7 | KWin 6.8 |
| --- | --- | --- |
| Offscreen buffer | `GLTexture::allocate(GL_RGBA8, size)` + `GLFramebuffer` | `EglSwapchain` (dmabuf, `DRM_FORMAT_ARGB8888`) |
| Texture class | `GLTexture` | `EGLImageTexture` (from `importDmaBufAsTexture`) |
| Content transform | `OutputTransform{}` (identity) | `OutputTransform::FlipY` |
| `texcoord0` y in the shader | points **up** the screen | points **down** the screen |

The evidence in the KWin source:

- 6.6 `src/opengl/gltexture.cpp`, `GLTexture::allocate()`:
  `new GLTexture(..., OutputTransform{})`.
- 6.8 `src/opengl/eglimagetexture.cpp`: `GLTexture(target, textureId,
  internalFormat, size, 1, true, OutputTransform::FlipY)`.
- `RenderTarget` takes its transform from the colour attachment's
  `contentTransform()`. Thus 6.8 draws the window into the buffer flipped, and
  `texture()->matrix(NormalizedCoordinates)` flips the texture coordinates
  back.

With the 6.7 flip on 6.8, every vertical shift goes the wrong way. Green moves
up, red and blue move down, and the fringe gets larger. No test can see this
(see the note below), so check the direction by eye after a KWin update: with
the effect on (**Meta+Ctrl+F9**), the green halo above edges and the warm
fringe below them must get smaller.

## Measuring it

The numbers above are reproducible. `tests/` builds `fringeshift-measure`, which
compiles the shipped [fringeshift.frag](src/shaders/fringeshift.frag) (the copy
that CMake configured for your KWin version) on a surfaceless OpenGL ES 3
context, runs synthetic patterns through it, and measures the result — no
compositor, no GPU, no window on screen. KWin 6.8 runs on OpenGL ES only, so the
harness does the same, and it passes the shader through a copy of KWin's own
shader preprocessor ([tests/kwinpreprocess.cpp](tests/kwinpreprocess.cpp)). The
driver therefore compiles the same GLSL ES text that KWin gives it. A shader that
does not compile is a failure, never a skip.

```sh
cmake --build build && ctest --test-dir build --output-on-failure
```

`nix build .#` runs it too, on llvmpipe inside the sandbox. Where no usable GL
driver exists the harness exits 77 and ctest records a skip rather than a
failure.

KWin's `colormanagement.glsl` and `saturation.glsl` are compiled into the
compositor and cannot be resolved outside it, so the harness substitutes the
stubs in [tests/stubs/](tests/stubs/). The colour-management stub keeps a
real transfer function, KWin's sRGB branch and its inverse, so that the
`LinearLight` path decodes to light as it does in KWin. On the default path
that is a round trip, and the harness measures in the source encoding as
before. Colorimetry, tone mapping and saturation are identity.

The stubs cannot see a change in KWin's real include files, so two more tests,
`shader-kwin-es300` and `shader-kwin-es100`, compile the shader against them
with `glslangValidator`. They cover both prologues KWin can choose: GLSL ES 3.00
on a GLES 3 context, and GLSL ES 1.00 on GLES 2. They need no GPU. The flake
takes the include files from the source of the pinned `kdePackages.kwin`, and
the dev shell sets `FRINGESHIFT_KWIN_SHADER_DIR` for them. Without that variable
or `glslangValidator`, CMake prints a note and leaves the two tests out.

| Check | What it pins down |
| --- | --- |
| `identity` | an all-zero set is a true no-op, so unredirecting costs nothing |
| `alpha-untouched` | alpha is never resampled, so shadows and corners stay intact |
| `dc-transfer` | mean is preserved exactly — the stroke-weight argument |
| `mtf-at-f0.25` | the Laplacian term flattens the MTF; measured 1.0416 |
| `chromatic-match` | all three channels move the same distance, green opposed |
| `shift-gain` | how much of the nominal shift arrives, per frequency |
| `proportional-shift` | a smooth sub-pixel continuum, with no floor |
| `translation-invariant` | a shift common to all channels changes nothing |
| `integer-step-exact` | a whole-texel step costs no MTF at all |
| `separable-compensation` | a vertical correction leaves horizontal detail alone |
| `half-texel-stable` | at an exact half-texel fraction, the result does not depend on window size |
| `linear-light-shift` | with `LinearLight`, the light moves by exactly the shift on black/white and lifted edges; prints what the default path delivers |
| `linear-light-dc` | with `LinearLight`, each channel's total light is unchanged |

> [!NOTE]
> Direction is deliberately out of scope: the harness cannot see the axis flip
> KWin applies through `GLTexture::matrix()`, so every assertion is a magnitude
> or a relative channel direction. That `+GreenY` moves green *down the screen*
> remains a claim only a real session can confirm. This direction also depends
> on the KWin version: KWin 6.8 changed the offscreen texture to a `FlipY`
> content transform, so the build sets the shader's y flip from `KWin_VERSION`.
> A wrong flip passes every check here and makes the fringe larger on screen.

## Why the settings page will not turn sub-pixel rendering off for you

Writing `XftSubPixel` is the easy half. Making it reach running applications is
the Fonts module's apply path (X resources via `krdb`, plus a platform-theme
refresh), which most toolkits act on only at restart anyway — so a page that
wrote the key itself would look like it had worked when it had not. It reports
the setting and links to the module that owns it instead.
