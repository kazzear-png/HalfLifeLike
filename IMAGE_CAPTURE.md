Realtime GI is now on by default in supported Cornell scenes. Allow at least 64 frames for a settled capture. Use `--gi 0` to compare corrected direct lighting. Old screenshots include the previous emitter-angle defect and are not pixel-equality targets. See REALTIME_GI.md.

# Image capture / PPC validation

The sandbox writes the final post-tonemap framebuffer as binary PPM.
Parent folders are created automatically.

## Save a PPC benchmark frame

```bat
build\bin\debug\sandbox.exe --scene cornell01 --area-light 1 --area-fast 1 --benchmark 1000 --width 1280 --height 720 --out captures\ppc\cornell01.ppm --report captures\ppc\cornell01.txt
```

`--benchmark N` automatically captures the final measured frame, so a separate
`--frames` argument is not needed.

## Save the legacy 16-point image

```bat
build\bin\debug\sandbox.exe --scene cornell01 --area-light 0 --benchmark 1000 --width 1280 --height 720 --out captures\legacy16\cornell01.ppm
```

This is *not* the brute-force reference. It is the legacy 4x4 point-light grid transport.

## Render a direct-only area-light reference

From the project root:

```bat
python tools\reference_pathtracer.py --variant cornell01 --res 1280 --spp 512 --direct-only --seed 7 --out captures\reference_direct\cornell01.ppm
```

This samples the real rectangular emitter and performs exact visibility rays at
the primary surface, but stops before indirect bounces. It is the appropriate
reference for evaluating PPC direct-shadow/area-light visibility.

## Render the full path-traced reference

```bat
python tools\reference_pathtracer.py --variant cornell01 --res 1280 --spp 512 --bounces 8 --seed 7 --out captures\reference_full\cornell01.ppm
```

The full reference contains indirect transport/GI and therefore should not be
expected to match a direct-only realtime PPC renderer pixel-for-pixel.
