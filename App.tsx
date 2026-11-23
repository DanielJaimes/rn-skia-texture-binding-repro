import React, { useEffect, useState } from 'react';
import { NativeModules, Pressable, Text, View } from 'react-native';
import { Canvas, Circle, ColorMatrix, Image, Skia, type SkImage } from '@shopify/react-native-skia';
import { runOnUI, useDerivedValue, useFrameCallback, useSharedValue } from 'react-native-reanimated';

// 'gpu' = AHardwareBuffer filled on the GPU (native helper), 'cpu' = same flags, filled on the CPU,
// 'skia' = Skia.NativeBuffer.MakeTestBuffer
const KIND: 'gpu' | 'cpu' | 'skia' = 'gpu';
// Start the app directly in the case under test: once a TextureView canvas has rendered, Ganesh's
// texture-binding state stays stale even after switching to an opaque canvas.
const INITIAL: Case = 'a';
type Case = 'a' | 'b' | 'c'; // a: canvas A (TextureView) | b: A opaque | c: A opaque + animated canvas B
const ID = [1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0];

export default function App() {
  const [c, setC] = useState<Case>(INITIAL);
  const image = useSharedValue<SkImage | null>(null);
  const t = useSharedValue(0);
  useEffect(() => {
    (async () => {
      const [hi, lo] = KIND === 'skia' ? [0, 0] : await NativeModules.RBHelper.make(KIND);
      // The image must be imported on the UI runtime (OpenGLContext is thread_local).
      runOnUI(() => {
        'worklet';
        const ptr = KIND === 'skia'
          ? Skia.NativeBuffer.MakeTestBuffer(512, 512)
          : BigInt(hi) * BigInt(4294967296) + BigInt(lo);
        image.value = Skia.Image.MakeImageFromNativeBuffer(ptr);
      })();
    })();
  }, [image]);
  useFrameCallback(() => { t.value += 1; });
  const cx = useDerivedValue(() => 20 + ((t.value * 3) % 260));

  return (
    <View style={{ flex: 1, backgroundColor: '#222', paddingTop: 40 }}>
      <View style={{ flexDirection: 'row', gap: 8, padding: 8 }}>
        {(['a', 'b', 'c'] as Case[]).map((k) => (
          <Pressable key={k} testID={`case-${k}`} onPress={() => setC(k)}
            style={{ padding: 12, backgroundColor: c === k ? '#4a4' : '#555' }}>
            <Text style={{ color: '#fff' }}>case {k}</Text>
          </Pressable>
        ))}
      </View>
      {/* Canvas A re-draws the same imported image every frame (the circle forces the redraw). */}
      <Canvas key={c} opaque={c !== 'a'} style={{ width: 300, height: 300 }}>
        <Image image={image} x={0} y={0} width={300} height={300} fit="fill">
          <ColorMatrix matrix={ID} />
        </Image>
        <Circle cx={cx} cy={20} r={10} color="white" />
      </Canvas>
      {c === 'c' && (
        <Canvas style={{ width: 300, height: 150 }}>
          <Circle cx={cx} cy={75} r={30} color="magenta" />
        </Canvas>
      )}
    </View>
  );
}
