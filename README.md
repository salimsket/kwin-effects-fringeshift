> [!CAUTION]
> This is a 100% Claude generated repository.

# FringeShift

<img width="1450" height="514" alt="kde-effects-fringeshift" src="https://github.com/user-attachments/assets/4814f9da-b522-441d-b58d-5ebb625dfab7" />

A KWin (Plasma 6) effect that kills QD-OLED colour fringing — the green halo
above edges and the warm one below — by shifting each colour channel's content
a fraction of a pixel the other way. Whole screen, not just text.

## KWin Effect config

<img width="997" height="729" alt="image" src="https://github.com/user-attachments/assets/2b72319c-5e33-424e-837c-daf7f94ea699" />

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
| KWin | Plasma 6 | **6.6.6** — dev package, provides `KWinConfig.cmake` + `effect/*.h` |
| libepoxy + EGL | any | tests only; skip with `-DFRINGESHIFT_BUILD_TESTS=OFF` |

Both KWin ≤ 6.6 and 6.7+ are handled — the build detects `KWin_VERSION` and
switches two API shims.

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
and spin box per channel axis, the sharpen amount, and the two resample toggles.
It writes the keys below and tells the compositor to re-read them, so Apply is
enough.

A row of preset buttons loads four archived parameter sets — **MPE** and **SPE**
(two readings of the panel's emitter geometry), **Osorio** (converged by hand on
live hardware; what ships), and **Triad** (a conservative floor). A button only
fills the controls; Apply still commits and Reset still undoes.

Same thing from the shell — `kwinrc`, group `[Effect-fringeshift]`, physical
pixels, `+X` right and `+Y` down:

```sh
kwriteconfig6 --file kwinrc --group Effect-fringeshift --key GreenY 0.12
kwriteconfig6 --file kwinrc --group Effect-fringeshift --key RedY -0.06
qdbus6 org.kde.KWin /KWin reconfigure
```

| Key | Default | Meaning |
| --- | --- | --- |
| `RedX` / `RedY` | `0.238` / `-0.150` | red content shift, px |
| `GreenX` / `GreenY` | `0.0` / `0.300` | green content shift, px |
| `BlueX` / `BlueY` | `-0.238` / `-0.150` | blue content shift, px |
| `Sharpen` | `0.0` | unsharp amount on top of the shift, 0 – 1.5 |
| `Bicubic` | `true` | Catmull-Rom resample; `false` = bilinear (softer, no ringing) |
| `Adaptive` | `true` | gate `Sharpen` to text-like pixels (never gates the shift) |
| `ShowMask` | `false` | debug: render the text mask instead of the image |

Shifts are clamped to ±1 px. `ShowMask` is a debug flag with no widget on the
settings page; set it by hand. With every shift at 0 and `Sharpen` at 0 the
effect unredirects every window, so an all-zero configuration costs nothing.

The defaults carry two separations: 0.450 px vertically between green and the
red/blue pair beneath it (the green/magenta fringe itself), and 0.476 px
horizontally between red and blue (red on left edges, blue on right). Why they
are written the way they are is [below](#where-the-default-numbers-come-from).

## Known limits

- Every window pays one offscreen pass per frame while the effect is enabled.
- Alpha is not shifted, so window shadows and rounded corners stay intact; the
  premultiplied RGB near an alpha edge is filtered along with the colour.
- Shifts are in physical pixels and correct on HiDPI, but they assume the panel
  is in its native orientation — a rotated output has a rotated triad, which
  this does not model.
- Nothing outside the compositor is corrected. `OffscreenEffect` deliberately
  does not block direct scanout, so a fullscreen client that KWin hands straight
  to the display controller is shown uncorrected — the same honest limit the
  Windows version has with fullscreen-exclusive apps.
- A configured shift is a nominal, not a delivered, displacement — see
  [gain](#nominal-versus-delivered-shift).

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

The vertical pair is written *uncentred*, `GreenY 0.300` against `RedY`/`BlueY`
−0.150, which is the emitter geometry read literally: one upper emitter against
two lower ones. The shader subtracts the midpoint of the three before
resampling, so what reaches the resample is ±0.225 and no channel carries a
larger fraction; writing the same geometry centred is bit-identical.

`presets.conf` archives two independent measurements of the panel —
0.437/0.449 vertical/horizontal from the lattice average, 0.472/0.441 from the
single-cell drawing — which the shipped values sit slightly wider than. It also
holds the derivations and the sets that did not earn a preset button; nothing
reads it at runtime.

## Nominal versus delivered shift

A configured shift is a **nominal**, not a delivered, displacement. Catmull-Rom
is an interpolating kernel, not an ideal one, so its phase response
under-delivers the requested offset by an amount that grows with frequency:
measured 0.982 of nominal at f = 1/16 cyc/px and 0.733 at f = 0.25, the
text-edge end. Ask for the default preset's ±0.225 px of centred green and
roughly 0.165 px arrives where it matters most.

Gain depends on the fraction as well as the frequency — the in-phase Laplacian
term grows as its square and partly offsets the kernel's phase lag, so smaller
shifts deliver a slightly smaller proportion (the former 0.10 px default read
0.680 at f = 0.25, not 0.733). The response stays proportional to within a few
percent (see `proportional-shift` below), so this is a scale factor to be aware
of when reading a preset, not a non-linearity.

## How it works

`OffscreenEffect` redirects each window into a texture, then redraws it through
[fringeshift.frag](src/shaders/fringeshift.frag):

- the three shifts are centred about zero, so no channel carries a larger
  fractional offset than the others (an uncentred set makes grey text read
  reddish — measured 16 % chromatic mismatch at G +0.30 / RB −0.15);
- each shift splits into an exact integer texel step plus a fraction; only the
  fraction is filtered;
- per channel, a Catmull-Rom (or bilinear) resample gives true sub-pixel
  positioning, and a Laplacian term flattens the interpolator's MTF so the
  resample does not soften: |MTF−1| at a 0.10 px fraction drops from 0.200 to
  0.013 for bilinear, 0.056 to 0.011 for Catmull-Rom. That compensation is
  applied **per axis** — the resample is separable, so a horizontal fraction
  costs MTF horizontally and nothing more, and a purely vertical correction
  must leave horizontal detail untouched;
- resampling happens in the source encoding, not linear light — in linear light
  the taps re-weight toward the bright side of every edge, which is exactly the
  stroke-weight change this effect must avoid.

## Measuring it

The numbers above are reproducible. `tests/` builds `fringeshift-measure`, which
compiles the shipped [fringeshift.frag](src/shaders/fringeshift.frag) against a
surfaceless EGL context, runs synthetic patterns through it, and measures the
result — no compositor, no GPU, no window on screen.

```sh
cmake --build build && ctest --test-dir build --output-on-failure
```

`nix build .#` runs it too, on llvmpipe inside the sandbox. Where no usable GL
driver exists the harness exits 77 and ctest records a skip rather than a
failure.

KWin's `colormanagement.glsl` and `saturation.glsl` are compiled into the
compositor and cannot be resolved outside it, so the harness substitutes the
identity stubs in [tests/stubs/](tests/stubs/). That is faithful to what is
under test: the resample deliberately happens in the source encoding, *before*
that tail runs.

| Check | What it pins down |
| --- | --- |
| `identity` | an all-zero set is a true no-op, so unredirecting costs nothing |
| `alpha-untouched` | alpha is never resampled, so shadows and corners stay intact |
| `dc-transfer` | mean is preserved exactly — the stroke-weight argument |
| `mtf-at-f0.25` | the Laplacian term flattens the MTF; measured 1.0319 |
| `chromatic-match` | all three channels move the same distance, green opposed |
| `shift-gain` | how much of the nominal shift arrives, per frequency |
| `proportional-shift` | a smooth sub-pixel continuum, with no floor |
| `translation-invariant` | a shift common to all channels changes nothing |
| `integer-step-exact` | a whole-texel step costs no MTF at all |
| `separable-compensation` | a vertical correction leaves horizontal detail alone |

> [!NOTE]
> Direction is deliberately out of scope: the harness cannot see the axis flip
> KWin applies through `GLTexture::matrix()`, so every assertion is a magnitude
> or a relative channel direction. That `+GreenY` moves green *down the screen*
> remains a claim only a real session can confirm.

## Why the settings page will not turn sub-pixel rendering off for you

Writing `XftSubPixel` is the easy half. Making it reach running applications is
the Fonts module's apply path (X resources via `krdb`, plus a platform-theme
refresh), which most toolkits act on only at restart anyway — so a page that
wrote the key itself would look like it had worked when it had not. It reports
the setting and links to the module that owns it instead.
