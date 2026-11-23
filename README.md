# RN Skia 2.x (Android, Ganesh/GL): re-drawing an imported AHardwareBuffer image draws black

Minimal repro: `SurfaceTexture.updateTexImage()`, called by `RNSkOpenGLCanvasProvider::renderToCanvas` before every
render of a non-opaque (TextureView) `Canvas`, can change the `GL_TEXTURE_EXTERNAL_OES` binding behind Ganesh's
texture-binding cache. An image from `Skia.Image.MakeImageFromNativeBuffer` is a `GL_TEXTURE_EXTERNAL_OES` texture, so a
later draw of the same image hits the stale cache, skips `glBindTexture` and samples the wrong texture (black).

AI assistance: the root-cause analysis, this repro and the fix were drafted with an AI assistant and checked on a device.

## What it contains
- `App.tsx` (48 lines, JS only): one image from `Skia.NativeBuffer.MakeTestBuffer`, imported on the UI runtime
  (`OpenGLContext` is thread_local) and drawn by canvas A every frame (the circle forces the redraw). Three cases:
  - **a**: A is a normal (non-opaque, TextureView) canvas.
  - **b**: A is `opaque` (SurfaceView), the control.
  - **c**: A `opaque` plus a second non-opaque canvas B that animates (cross-canvas case).
- `patches/@shopify+react-native-skia+2.14.0.patch`: the fix, three lines of code plus a comment (`patch-package`).
  It resets Ganesh's texture-binding state right after `updateTexImage()`.

## Versions
React Native 0.84.1 (New Architecture), `@shopify/react-native-skia` 2.14.0 (the latest on npm), Reanimated 4.3.1,
worklets 0.8.3, minSdk 26, arm64-v8a, Release build. Device: Xiaomi 23117RA68G (Android 16, Mali-G57).
On the 2.x branch head (8ca8396) the same call lives in an `updateTexImage()` helper of `RNSkOpenGLCanvasProvider`; the
fix goes in the same place there.

## Run
```sh
npm install                  # applies the fix through patch-package (postinstall)
npx patch-package --reverse  # for the run WITHOUT the fix
cd android && ./gradlew :app:assembleRelease -PreactNativeArchitectures=arm64-v8a
adb install -r app/build/outputs/apk/release/app-release.apk
```
Set `INITIAL` (`'a' | 'b' | 'c'`) in `App.tsx`, rebuild and start the app cold for each case (force-stop first).
The cache is per context (shared by all canvases on the UI thread), so after any TextureView render, an opaque canvas
that re-draws the same image is also affected until the context binds a different texture on that unit. Measure each
case from a cold start.

## The fix (`android/cpp/rnskia-android/RNSkOpenGLCanvasProvider.cpp`, right after `updateTexImage()` and its exception check)
```cpp
// updateTexImage() can change the GL_TEXTURE_EXTERNAL_OES binding of the current context behind
// Ganesh's texture-binding cache. Without this reset, re-drawing an already imported external image
// (e.g. MakeImageFromNativeBuffer) hits the stale cache, skips glBindTexture and samples the wrong texture.
if (auto *context = OpenGLContext::getInstance().getDirectContext()) {
  context->resetContext(kTextureBinding_GrGLBackendState);
}
```

## Results (20 s screen recording per case, cold start, per-frame luma over canvas A, black = luma < 30)
| case | before the fix | with the fix |
| --- | --- | --- |
| a: A non-opaque | 2173 frames, 100% black | 2151 frames, 0% black |
| b: A opaque (control) | not run (a fresh process shows the image) | 2103 frames, 0% black |
| c: A opaque + B animating | 2185 frames, 100% black | 2115 frames, 0% black |

Other checks (same device):
- A `<ColorMatrix>` child on the image is not needed: case a is 100% black without it.
- Buffers filled on the GPU (EGLImage + FBO, usage `GPU_SAMPLED_IMAGE | GPU_COLOR_OUTPUT`) or on the CPU with the same
  usage flags behave the same as `MakeTestBuffer` (they display in a fresh opaque canvas, and go black in a and c without
  the fix). The helper that made them is in the git history (first commit).
- Instrumentation (debug only, not in the repo): `GL_TEXTURE_BINDING_EXTERNAL_OES` read before and after
  `updateTexImage()` in case a was 0 before and 0 after on 77 of 77 samples. Once, a non-zero texture (1, presumably the
  imported image) was bound before the call and 0 after. `updateAndRelease() failed` is logged only once or twice per
  canvas creation, not per frame.

Recordings (first 8 s, top of the screen) are in `recordings/`: `before_a.mp4`, `after_a.mp4`, `before_c.mp4`, `after_c.mp4`.
