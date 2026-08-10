# Fringe Shift — codebase notes

A KWin (Plasma 6) effect plugin: per-channel 2D subpixel correction for QD-OLED.
`README.md` covers *what it does and why* (the optics, the DC-transfer argument,
the measured numbers). This file covers *how the code is laid out* — read the
README first for rationale, this for structure.

## Shape of the thing

Five source files, ~500 lines total, and the split is lopsided on purpose:

| File | Lines | Role |
| --- | --- | --- |
| [src/shaders/fringeshift.frag](src/shaders/fringeshift.frag) | ~250 | **All of the signal processing.** The actual product. |
| [src/fringeshifteffect.cpp](src/fringeshifteffect.cpp) | ~210 | KWin/Qt lifecycle glue: shader load, window redirection, shortcut. |
| [src/fringeshifteffect.h](src/fringeshifteffect.h) | ~90 | Class declaration. |
| [src/fringeshiftparams.h](src/fringeshiftparams.h) / [.cpp](src/fringeshiftparams.cpp) | ~200 | The shader's parameter set and its preconditions. |
| [src/fringeshiftconfig.kcfg](src/fringeshiftconfig.kcfg) | ~50 | KConfigXT schema → generates `FringeShiftSettings` at build time. |
| [src/kcm/](src/kcm/) | ~870 | The "Configure…" page: a `.ui` form plus a `KCModule`. A separate plugin. |
| [src/main.cpp](src/main.cpp) | ~16 | One macro. The plugin entry point. |
| [src/metadata.json](src/metadata.json) | ~19 | KPlugin metadata, embedded into the `.so` by the factory macro. |
| [tests/](tests/) | ~900 | Off-screen measurement harness. See "Testing" below. |

The C++ owns *no* image math. Every pixel decision lives in GLSL. Anything that
changes how the correction looks belongs in the `.frag`; the `.cpp` only decides
*whether* the shader runs and *what seven uniforms* it runs with.

## The C++ side

### Entry point

[src/main.cpp](src/main.cpp) is `KWIN_EFFECT_FACTORY_SUPPORTED(FringeShiftEffect,
"metadata.json", return FringeShiftEffect::supported();)`. That macro expands to a
`QObject` subclass of `KWin::EffectPluginFactory` carrying
`Q_PLUGIN_METADATA(IID EffectPluginFactory_iid FILE "metadata.json")`, whose
`createEffect()` news up a `FringeShiftEffect`. `supported()` gates on
`effects->isOpenGLCompositing()` — the effect never appears on a software
compositor.

### FringeShiftEffect

Derives from `KWin::OffscreenEffect` (→ `Effect` → `QObject`). Four overrides:
`reconfigure()`, `isActive()`, `requestedEffectChainPosition()`, `drawWindow()`.
Notably it does **not** override `apply()` or `deform()` — the whole transform is
expressed by handing the redirected window a custom shader via the protected
`setShader(w, m_shader.get())`, and letting `OffscreenEffect::drawWindow()` do the
rest.

`drawWindow()` exists only to *opt out*: on a transformed draw
(`mask & PAINT_WINDOW_TRANSFORMED` — an Overview thumbnail, a scaled window) it
hands off to `Effect::drawWindow()` and skips the offscreen path for that frame.
The correction is denominated in physical pixels and assumes the offscreen
texture maps 1:1 onto them; under a transform the shift is scaled along with the
window, and the unshifted alpha fetch (exact at 1:1) degrades to
nearest-neighbour and frays the very rounded corners it exists to protect. The
window stays redirected either way — only that frame's route changes.

**State**:

- `m_params` — a `FringeShiftParams`, the seven shader uniforms.
- `m_enabled` — runtime on/off from the global shortcut. **Not persisted**, and
  deliberately separate from config so A/B toggling doesn't dirty `kwinrc`.
- `m_windows` — `unordered_set<EffectWindow*>` of currently-redirected windows.
  This set *is* the activity state: `isActive()` is `!m_windows.empty()`.
- `m_shader` — `unique_ptr<GLShader>`, owned here, borrowed by every redirected
  window.

**The central invariant** — `m_windows` is non-empty **iff** `shouldRedirect()`.
Every path that could change the answer funnels through `updateWindows()`, which
either redirects the whole stacking order or unredirects everything.
`slotWindowAdded()` re-checks it because a new window arrives outside that funnel.

`FringeShiftParams::isIdentity()` is the cost gate inside it: with all shifts
zero and `Sharpen` zero the shader would be a no-op, so the effect drops every
redirection and an all-zero config costs literally nothing. `showMask` counts as
non-identity because it replaces the output.

### FringeShiftParams

The shader cannot validate its own uniforms, so its preconditions — shifts
clamped to ±1 px, `sharpen` to 0–1.5, everything finite — live in
[fringeshiftparams.h](src/fringeshiftparams.h) next to the uniform names, rather
than scattered across the effect, the kcfg, and the shader's comments.

**Finiteness is a separate step from clamping, not a consequence of it.**
`std::clamp` is specified as `v < lo ? lo : hi < v ? hi : v`, so both
comparisons against a NaN are false and it returns the NaN untouched — and Qt's
double parser accepts the literals `nan` and `inf`, so a typo in `kwinrc` is a
real route to one. A single NaN uniform poisons every tap of the shader's 4×4
footprint and blanks the window. `clampFinite()` degrades non-finite input to
zero rather than to a substitute, which makes the result `isIdentity()` — a
garbled config costs nothing instead of silently displacing channels.

`upload()` is templated on the shader rather than taking `KWin::GLShader`. That
is the module's second adapter: the measurement harness has no KWin to link
against, but uploads through the exact same uniform names the compositor does.
The `UniformSink` concept states what that seam requires, so a mismatch is one
diagnostic rather than an instantiation trace through the caller.
`clamped()` and `isIdentity()` are pure, and are the only C++ in the project
with no KWin dependency at all.

It is built as a small static library so both the plugin and the harness can
link it. The kcfg files are attached to *that* target, not the plugin.

### Three non-obvious constraints, all already load-bearing

These are the parts that took real debugging. Do not "clean them up".

1. **The shortcut is `Meta+Ctrl+F9`, not `Ctrl+Alt+F9`**
   ([fringeshifteffect.cpp:29-34](src/fringeshifteffect.cpp#L29-L34)). KWin's
   `VirtualTerminalFilter` runs *ahead* of the global-shortcut filter and eats the
   entire `Ctrl+Alt+F<n>` range as a VT switch. On a host with no fbcon that
   presents as a session freeze.

2. **`toggle()` must call `effects->makeOpenGLContextCurrent()` first, and bail
   if it fails** ([fringeshifteffect.cpp:151-161](src/fringeshifteffect.cpp#L151-L161)).
   KWin only guarantees a current GL context in create/destroy/reconfigure and
   inside the paint stages — a global-shortcut handler is none of those.
   `redirect()` and `unredirect()` create and destroy per-window textures and
   FBOs; doing that with no context current wedges the driver hard enough to take
   the session down. The call returns `bool`, so ignoring it would walk into that
   very failure; a toggle that quietly does nothing is the better outcome.

3. **`requestedEffectChainPosition()` returns 98** — late, because the correction
   is about the pixels that actually reach the panel, so it must run after effects
   that alter window contents.

### Config

`fringeshiftconfig.kcfgc` + `.kcfg` generate a singleton `KWin::FringeShiftSettings`
at build time (`kconfig_add_kcfg_files`), bound to group `[Effect-fringeshift]` in
`kwinrc`. `readConfig()` re-reads and clamps: shifts to ±1 px (beyond that it isn't
a fringe fix, and the shader's 4×4 Catmull-Rom footprint stops covering the taps),
`Sharpen` to 0–1.5, and non-finite values to zero. `reconfigure()` requires the
`ReconfigureAll` *bit* — tested with `&`, not compared for equality, so adding a
flag upstream cannot silently turn config reloads off.

The **"Configure…"** button is a second plugin, [src/kcm/](src/kcm/), installed to
`kwin/effects/configs/` and found only because the effect's `metadata.json` carries
`"X-KDE-ConfigModule": "kwin_fringeshift_config"` as a *sibling* of `KPlugin` —
nested inside it, the button greys out with no diagnostic. It links
`fringeshiftparams` rather than regenerating the `.kcfgc`, because two copies of a
singleton in one process means the second `instance()` is dropped with only a
`qDebug` line. Three things in it are load-bearing:

- **The sliders are not bound; the spin boxes are.** A `QSlider` carries an `int`,
  so binding one to a `Double` entry quantises ±1 px to `-1, 0, 1` with no error.
  `pairSlider()` drives the bound spin box from the slider instead, which keeps
  Apply/Reset and "Defaults" working for free.
- **`self()` needs an `instance()` first.** The schema is `kcfgfile arg="true"`, so
  the singleton has no config of its own; inside KWin the effect binds it, but in
  systemsettings nothing else will, and `self()` calls `qFatal()` if nobody has.
  The KCM then calls `load()`, not `read()` — this process is not the one KWin
  reparses `kwinrc` for.
- **`reconfigureEffect("fringeshift")` must match `KPlugin.Id`.** Nothing checks
  it; a mismatch saves correctly and changes nothing until the compositor restarts.
- **The preset buttons write widgets, not `FringeShiftSettings`.** Same reason
  `pairSlider()` does: the manager watches the bound widgets, so going through
  them is what dirties the page, makes Apply commit and Reset revert, and keeps a
  click out of `kwinrc` until the user asks. Writing the settings object would
  desynchronise the dirty state from both the widgets and the file. They set every
  bound entry *except* `ShowMask`, which has no widget to turn back off. The four
  parameter sets are `constexpr FringeShiftParams` in
  [FringeShiftKCM.cpp](src/kcm/FringeShiftKCM.cpp) — a second copy of four
  `presets.conf` blocks, because nothing reads that file at runtime. `osorio`
  currently equals the `.kcfg` defaults; that is coincidence, not a link.

`ShowMask` has no widget on purpose — it is a debug flag, and the dialog leaves
unbound entries alone.

The page's one piece of content that is not a `kcfg_` binding is the
`KMessageWidget` about font sub-pixel rendering. It reads
`kdeglobals [General] XftSubPixel`, a setting **another module owns**, so it
`reparseConfiguration()`s on every `load()` rather than caching, and an *absent*
key counts as "not off" — with no key the decision falls to fontconfig, which
often answers RGB. Its button launches the Fonts KCM instead of writing the key:
writing it is easy, but making it reach running applications is that module's
apply path (`krdb` X resources plus a platform-theme refresh) and most toolkits
re-read it only at restart, so a page that wrote the file would look like it had
worked.

Everything the dialog does is also reachable as `kwriteconfig6` + a D-Bus
`reconfigure` call, as documented in the README. That workflow depends on KWin
reparsing `kwinrc` before it dispatches `reconfigure()`: `fromConfig()` goes
through `KCoreConfigSkeleton::read()`, which uses KConfig's *in-memory* values
and does not reload from disk (`load()` is the one that would). `presets.conf` is a reference
archive of parameter sets — *nothing reads it*, it is paste-into-`kwinrc` material,
including its notes on converting FreeType 26.6 emitter tables into content shifts.

## The shader

[src/shaders/fringeshift.frag](src/shaders/fringeshift.frag) is written in
core-profile spelling (`#version 140`, `in`/`out`/`texture()`), because KWin's
preprocessor rewrites those and substitutes its own `#version` for legacy
contexts. **One file covers both profiles**, which is why it avoids GLSL 1.30+
built-ins (`round()`, integer `clamp()`, `textureSize()` — hence the
`textureWidth`/`textureHeight` uniforms).

Pipeline in `main()`:

1. `textMask()` (25 taps) — only evaluated when `adaptive` is on *and*
   (`sharpen > 0.001` or `showMask`). It gates **sharpness only**; the geometric
   shift is never scaled by it, because a per-pixel-scaled displacement is a
   *warp*, and the mask swings 1→0 inside a single glyph.
2. Axis flip (`texcoord0`'s y points up; the UI convention is +Y down).
3. **Centre the three shifts about zero** — a shift common to all channels
   translates the image invisibly while leaving relative channel geometry
   bit-identical, and it equalises the per-channel *fractional* shifts, which is
   what the resample's MTF loss actually depends on.
4. Split each shift into integer texel step (exact, free) + fraction in
   [-0.5, 0.5]. Only the fraction is filtered, so only the fraction feeds the
   compensation term.
5. Per channel: Catmull-Rom (or bilinear) resample + a Laplacian term whose
   coefficient `ca` (0.8625 bicubic / 1.7216 bilinear) flattens the interpolator's
   MTF so the resample doesn't soften. **Per axis, not isotropic** — `ca * f * f`
   against a Laplacian kept split into its x and y components, because the
   resample is separable: the x fraction costs MTF along x only. A single
   coefficient from `dot(f, f)` charges each axis for both, which sharpens
   across a purely vertical shift; `separable-compensation` pins it.
   `ca` was fitted on a 1D grating, where `dot(f, f)` degenerates to `fy²`,
   so it carries over to the split form unchanged.
6. **Alpha is fetched unshifted** and RGB clamps to `a`, not 1 — the texture holds
   *premultiplied* content, and displacing coverage would fray rounded corners and
   shadows.
7. Same tail as KWin's `base.frag`, in the same order:
   `encodingToNits` → `colorimetryTransform` → `adjustSaturation` → `modulation` →
   `doTonemapping` → `nitsToDestinationEncoding`. The resample above runs in the
   **source encoding, not linear light** — in linear light the taps re-weight
   toward the bright side of every edge, i.e. exactly the stroke-weight change
   this effect exists to avoid.

The `ShaderTrait` set at load time
([fringeshifteffect.cpp:70](src/fringeshifteffect.cpp#L70)) —
`MapTexture | Modulate | AdjustSaturation | TransformColorspace` — is what makes
those tail includes resolve. Changing the tail means changing the traits to match.

## Build

CMake + ECM, `kcoreaddons_add_plugin` into `kwin/effects/plugins`. Two version
shims, both keyed off `KWin_VERSION`:

- **`FRINGESHIFT_GLSHADER_HAS_ISVALID`** — KWin ≤ 6.6 has `GLShader::isValid()`;
  6.7 dropped it and signals failure with a null return instead.
- **`fringeshift_core.frag`** — KWin ≤ 6.6 rewrites a shader path to its `_core`
  variant on a core-profile context; 6.7 loads the path verbatim. The one authored
  shader is `configure_file`d under both names into a generated `.qrc` so either
  resolver finds it.

Effect plugins are **not** binary compatible across KWin releases. `flake.nix`
pins nixpkgs to the exact revision the host NixOS generation was built from so
`kdePackages.kwin` here is bit-identical to the KWin that will load the plugin;
`nix flake update` alone silently breaks the plugin. This machine currently
resolves KWin **6.6.6** (`build/CMakeCache.txt` → `KWin_DIR`).

## Testing

`ctest --test-dir build` runs `fringeshift-measure`, which compiles the **shipped**
`.frag` against a surfaceless EGL context and measures it. `nix build .#` runs it
too, on llvmpipe inside the sandbox. Exit 77 means no usable GL driver and is
wired to CTest's `SKIP_RETURN_CODE`, so a driverless host skips rather than fails
— but only if nothing has failed yet, because the four parameter-domain checks
run *before* the EGL setup and a failure there is real on any host.

- [tests/shaderrunner.cpp](tests/shaderrunner.cpp) — the seam. EGL setup, textual
  `#include` resolution (GLSL has none; KWin's preprocessor does this in
  production), FBO render, readback. RGBA32F throughout, because 8-bit
  quantisation is the same order as the MTF errors being measured.
- [tests/stubs/](tests/stubs/) — identity stand-ins for KWin's
  `colormanagement.glsl` and `saturation.glsl`, which are compiled into the
  compositor and unresolvable outside it. Faithful because the resample runs in
  the source encoding, before that tail.
- [tests/measure.cpp](tests/measure.cpp) — fourteen checks. Ten measure the
  shader, each tied to a specific README claim. The other four
  (`checkParameterDomain()`) cover `FringeShiftParams`, the only pure code here:
  the clamp range, the sharpen floor, non-finite rejection, and the
  `isIdentity()` cost gate. They need no GL and so run first.

Three things to know before touching the thresholds:

1. **Displacement gain is not 1, and is not a constant of the kernel.** Measured
   0.982 of nominal at f = 1/16 and **0.733 at f = 0.25** for the shipped
   preset's centred 0.225 px. Catmull-Rom is symmetric but not ideal, and its
   phase response under-delivers by more as frequency rises. `shift-gain` and
   `proportional-shift` both pin values that **move when the default preset
   moves** — gain rises with the fraction, because the in-phase Laplacian term
   is quadratic in it and partly offsets the phase lag. The 0.10 px default read
   0.979 / 0.680, and its `proportional-shift` spread was half the current one.
   The closed form is in the check's comment and reproduces the measured values
   to five digits — if either check moves, compute the expected value before
   assuming the harness is wrong.
2. **Direction is out of scope.** The harness cannot reproduce KWin's axis flip
   (`GLTexture::matrix()`), so every assertion is a magnitude or a *relative*
   channel direction. `+GreenY` moving green down the screen is confirmable only
   in a real session.
3. **Nearly everything here measures along y, on a y-varying grating.** That is
   the geometry the effect exists for, but it means a whole axis goes unwatched,
   and the two facts compound: on a 1D pattern the perpendicular Laplacian taps
   cancel, and with `fx = 0` an isotropic MTF coefficient equals a per-axis one.
   Both sides of that identity vanish at once, which is how the shader shipped a
   real cross-axis defect with all thirteen other checks passing *unchanged*.
   `separable-compensation` is the only check looking down the x axis. A new
   check that only varies along y inherits the same blind spot.

   The shipped defaults are no longer vertical-only — they carry
   `RedX +0.238 / BlueX -0.238` — so `defaults` now moves an x-varying grating.
   `separable-compensation` builds a vertical-only set by zeroing the x
   components rather than passing `defaults`; handing it the raw defaults
   asserts the negation of separability and fails on a correct shader. Its
   second half needs the mirror set (defaults with the *y* components zeroed),
   and derives that too — every magnitude in that check tracks the preset, so
   none of them can go stale while still passing.

Adding a check is cheap: `ShaderRunner::run(image, params)` is the whole
interface.

## Where to make changes

- **Correction behaviour / quality** → the `.frag`, essentially always. Run
  `ctest` afterwards: the checks exist to catch what a visual A/B cannot.
- **New tunable** → `.kcfg` entry, a member + clamp in `FringeShiftParams`, a
  line in its `upload()`, the `uniform` in the `.frag`, a `kcfg_`-named widget in
  [src/kcm/FringeShiftKCM.ui](src/kcm/FringeShiftKCM.ui), README table row.
- **When the effect runs at all** → `shouldRedirect()` / `updateWindows()`,
  keeping the `m_windows`-non-empty-iff invariant above.
- **KWin version support** → `CMakeLists.txt` shims + `flake.nix` pin, together.
