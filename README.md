> [!CAUTION]
> This is a 100% Claude generated repository.

<img width="1450" height="514" alt="kde-effects-fringeshift" src="https://github.com/user-attachments/assets/4814f9da-b522-441d-b58d-5ebb625dfab7" />

# Fringe Shift

Per-channel 2D subpixel correction for QD-OLED, as a KWin effect (Plasma 6).

The QD-OLED triad puts **green at the top vertex** and **red/blue at the bottom
corners**, so a nominally grey edge shows a green halo above and a warm fringe
below. Fringe Shift shifts each channel's *content* by a fraction of a pixel in
the opposite direction. A constant sub-pixel shift is a convolution whose DC
transfer is exactly 1, so it cancels the fringe without changing apparent stroke
weight — which is why the shift is applied uniformly and is never scaled per
pixel by the text mask.

> [!IMPORTANT]
> Set *System Settings → Text & Fonts → Fonts → Anti-Aliasing → Sub-pixel
> rendering* to **None** before enabling this effect. That setting displaces the
> same three channels inside the glyph rasteriser, so the two corrections stack
> rather than combining — the fringing gets worse, not better.

From the shell that is:

```sh
kwriteconfig6 --file kdeglobals --group General --key XftSubPixel none
```

then log out and back in. The settings page checks the setting and says so if it
is still on, with a button through to the Fonts module; it does not change it
itself ([why](#why-the-settings-page-will-not-turn-sub-pixel-rendering-off-for-you)).
This effect is the broader of the two corrections anyway: sub-pixel rendering
reaches text only, whereas this corrects the whole window, icons and UI chrome
included.

## Build

```sh
nix develop                                    # toolchain + KWin/Qt/KF6 headers
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Or straight from the flake: `nix build .#` — the plugin lands in
`result/lib/qt-6/plugins/kwin/effects/plugins/fringeshift.so`.

> [!WARNING]
> An effect plugin is **not** binary compatible across KWin releases. Build it
> against the exact KWin you run; on NixOS that means letting the same nixpkgs
> provide both.

## Install

```sh
cmake --install build --prefix ~/.local
```

then `kwin_wayland --replace` (or log out and back in) and enable
**Fringe Shift** in *System Settings → Desktop Effects*.

On NixOS, add the flake's package to `environment.systemPackages` instead —
`~/.local` is not on KWin's plugin path there by default.

## Configuration

The **Configure…** button beside the effect in *System Settings → Desktop
Effects* opens a settings page with a slider and a spin box per channel axis,
the sharpen amount, and the two resample toggles. It writes the same keys as
below and tells the running compositor to re-read them, so changes take effect
on Apply.

A row of preset buttons across the top loads four of the archived parameter
sets — **MPE** and **SPE**, the two readings of the panel's emitter geometry,
**Osorio**, a correction converged by hand on live hardware and what the effect
ships with, and **Triad**, a deliberately conservative floor. Each button only
fills the controls below it: Apply still commits, Reset still takes the click
back, and the tooltip names the separations that set carries. `presets.conf`
holds the derivations, plus the sets that did not earn a button.

Everything is equally reachable from the shell. Values live in `kwinrc` under
`[Effect-fringeshift]`, in physical pixels, `+X` right and `+Y` down.

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

Those defaults carry two separations: 0.450 px vertically between green and the
red/blue pair beneath it (the green/magenta fringe itself), and 0.476 px
horizontally between red and blue (the orthogonal artefact — red on left edges,
blue on right). Why they are written the way they are is
[below](#where-the-default-numbers-come-from).

Shifts are clamped to ±1 px. `ShowMask` is a debug flag and is deliberately
absent from the settings page; set it by hand. **Meta+Ctrl+F9** toggles the
correction at runtime for A/B comparison (rebindable under *Shortcuts → KWin*).

With every shift at 0 and `Sharpen` at 0 the effect unredirects every window, so
an all-zero configuration costs nothing.

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

## Where the default numbers come from

The vertical pair is written *uncentred*, `GreenY 0.300` against `RedY`/`BlueY`
−0.150, which is the emitter geometry read literally: one upper emitter against
two lower ones. The shader subtracts the midpoint of the three before
resampling, so what reaches the resample is ±0.225 and no channel carries a
larger fraction; writing the same geometry centred is bit-identical.

`presets.conf` archives two independent measurements of the panel —
0.437/0.449 vertical/horizontal from the lattice average, 0.472/0.441 from the
single-cell drawing — which the shipped values sit slightly wider than.

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
