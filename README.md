# RN Skia 2.x (Android, Ganesh/GL): re-drawing an imported AHardwareBuffer image draws black

Minimal repro for: `SurfaceTexture.updateTexImage()`, called by `RNSkOpenGLCanvasProvider::renderToCanvas` before every
render of a non-opaque (TextureView) `Canvas`, rebinds `GL_TEXTURE_EXTERNAL_OES` behind Ganesh's texture-binding cache.
An image from `Skia.Image.MakeImageFromNativeBuffer` is a `GL_TEXTURE_EXTERNAL_OES` texture, so a later draw of the
same image hits the stale cache, skips `glBindTexture` and samples the wrong texture (black).

AI assistance: the root-cause analysis, this repro and the fix were drafted with an AI assistant and checked on a device.

## What it contains
- `App.tsx` (59 lines): one imported image, drawn by canvas A every frame. Three cases:
  - **a**: A is a normal (non-opaque, TextureView) canvas.
  - **b**: A is `opaque` (SurfaceView), the control.
  - **c**: A `opaque` plus a second non-opaque canvas B that animates (cross-canvas case).
- `android/rbhelper` (about 50 lines of C++, `rbhelper.cpp`): a tiny native module that allocates an RGBA8
  `AHardwareBuffer` (`GPU_SAMPLED_IMAGE | GPU_COLOR_OUTPUT`) and fills it on the GPU (EGLImage + FBO, scissored clears), or on
  the CPU (`KIND = 'cpu'`). The pointer goes to JS as hi/lo 32-bit numbers and is rebuilt as a BigInt on the UI runtime,
  where the image is imported (`OpenGLContext` is thread_local).
- `patches/@shopify+react-native-skia+2.14.0.patch`: the one-line fix (`patch-package`, applied on `npm install`).
  It resets Ganesh's texture-binding state right after `updateTexImage()`.

## Versions
React Native 0.84.1 (New Architecture), `@shopify/react-native-skia` 2.14.0, Reanimated 4.3.1, worklets 0.8.3, minSdk 26,
arm64-v8a, Release build. Device: Xiaomi 23117RA68G (Android 16, Mali-G57).

## Run
```sh
npm install --ignore-scripts            # to run WITHOUT the fix (skips patch-package)
cd android && ./gradlew :app:assembleRelease -PreactNativeArchitectures=arm64-v8a
adb install -r app/build/outputs/apk/release/app-release.apk
```
Set `INITIAL` (`'a' | 'b' | 'c'`) in `App.tsx` and start the app cold for each case. Switching cases at runtime is
possible with the buttons, but once any TextureView canvas has rendered, Ganesh's binding state stays stale and even
the opaque case can draw black, so measure each case from a fresh process.
To apply the fix: `npm install` (runs `patch-package`) or `npx patch-package`.

## The fix (2.x, `android/cpp/rnskia-android/RNSkOpenGLCanvasProvider.cpp`, inside the `if (_jSurfaceTexture)` block, after the exception check)
```cpp
if (auto *context = OpenGLContext::getInstance().getDirectContext()) {
  context->resetContext(kTextureBinding_GrGLBackendState);
}
```
(On 2.x head the same code lives in the `updateTexImage()` helper.)

## Results (20 s screen recording per case, per-frame luma over canvas A, black = luma < 30)
| case | before the fix | with the fix |
| --- | --- | --- |
| a: A non-opaque | 2217 frames, 100% black | 2192 frames, 0% black |
| b: A opaque (control) | 2165 frames, 0% black | 2118 frames, 0% black |
| c: A opaque + B animating | 2208 frames, 100% black | 2178 frames, 0% black |

Buffers that display in case b (cold start): GPU-written (this helper), CPU-written with GPU usage flags (`KIND = 'cpu'`),
and `Skia.NativeBuffer.MakeTestBuffer` (`KIND = 'skia'`). So the content or usage of the buffer does not matter.

Debug instrumentation (not in the repo): `GL_TEXTURE_BINDING_EXTERNAL_OES` read before and after `updateTexImage()`
in case a: 77 of 77 samples `before=0 after=0` (active unit `GL_TEXTURE0`). The first call after the first draw of the image
showed `before=1 after=0` in one run: Ganesh had texture 1 bound and `updateTexImage()` left it unbound.
`updateAndRelease() failed` is logged only once or twice per canvas creation, not per frame.

Recordings (first 8 s, top of the screen) are in `recordings/`: `before_a.mp4`, `after_a.mp4`, `before_c.mp4`, `after_c.mp4`.
