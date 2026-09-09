#!/usr/bin/env python3
"""Linux EGL integration test: real shader compilation, rendering, and history reset.

python3 tools/verify_realtime_gi.py --out gi-validation
Requires installed EGL/OpenGL, a C++17 compiler, Python, Pillow and NumPy.
Software Mesa validates behavior but does not establish RTX performance.
"""
import argparse
import ctypes as c
from pathlib import Path
import re
import subprocess
import tempfile


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,default=Path('gi-validation'))
    parser.add_argument('--compile-only',action='store_true')
    parser.add_argument('--full-lighting',action='store_true',help='render existing PPC direct lighting too; can compile slowly on software drivers')
    parser.add_argument('--width',type=int,default=640)
    parser.add_argument('--frames',type=int,default=48)
    args=parser.parse_args()
    if args.width<64 or args.width>640:parser.error('width must be between 64 and 640')
    if args.frames<2:parser.error('frames must be at least 2')
    import egl_test_context as ctx
    root=Path(__file__).resolve().parents[1]
    regex=re.compile(r'const char\*\s+(\w+)\s*=\s*R"GLSL\((.*?)\)GLSL"',re.S)
    gi=dict(regex.findall((root/'engine/src/rendering/DiffuseGI.cpp').read_text()))
    create=ctx.gl('glCreateShader',ctx.U,[ctx.U])
    source=ctx.gl('glShaderSource',None,[ctx.U,ctx.I,c.POINTER(c.c_char_p),c.POINTER(ctx.I)])
    compile_shader=ctx.gl('glCompileShader',None,[ctx.U])
    status=ctx.gl('glGetShaderiv',None,[ctx.U,ctx.U,c.POINTER(ctx.I)])
    log=ctx.gl('glGetShaderInfoLog',None,[ctx.U,ctx.I,c.POINTER(ctx.I),c.c_char_p])
    delete=ctx.gl('glDeleteShader',None,[ctx.U])
    count=0
    for rel in ['sandbox/src/shaders.h','engine/src/rendering/Renderer.cpp','engine/src/rendering/ShadowHeightfield.cpp','engine/src/rendering/DiffuseGI.cpp']:
        for name,body in regex.findall((root/rel).read_text()):
            if name=='kGiCommon':continue
            variants=[('',body)]
            if name=='kPbrFragment':
                variants=[]
                for area in (0,1):
                    for enable in (0,1):
                        insert=f'#define AREA_FAST_BUILD {area}\n#define SURFACE_GI_BUILD {enable}\n'+(gi['kGiCommon'] if enable else '')
                        variants.append((f'_area{area}_gi{enable}',body.replace('#version 330 core\n','#version 330 core\n'+insert,1)))
            if name=='kGiUpdateFragment':
                variants=[('',body.replace('#version 330 core\n','#version 330 core\n'+gi['kGiCommon'],1))]
            for suffix,text in variants:
                shader=create(0x8B31 if name.lower().endswith(('vertex','vs')) else 0x8B30)
                src=c.c_char_p(text.encode());source(shader,1,c.byref(src),None);compile_shader(shader)
                ok=ctx.I();status(shader,0x8B81,c.byref(ok))
                if not ok.value:
                    buf=c.create_string_buffer(16000);log(shader,len(buf),None,buf)
                    raise RuntimeError(name+suffix+'\n'+buf.value.decode())
                delete(shader);count+=1;print('PASS',name+suffix,flush=True)
    print(count,'shader variants compiled',flush=True)
    if args.compile_only:return
    args.out.mkdir(parents=True,exist_ok=True)
    print('Render mode:', 'full direct + indirect' if args.full_lighting else 'indirect-only diagnostic',flush=True)
    with tempfile.TemporaryDirectory(prefix='gi-test-build-') as build:
        library=Path(build)/'gi-tests.so'
        src=['assets/OBJ.cpp','math/Mat4.cpp','math/Frustum.cpp','rendering/Camera.cpp','rendering/GL.cpp','rendering/Shader.cpp','rendering/Mesh.cpp','rendering/Renderer.cpp','rendering/DiffuseGI.cpp']
        subprocess.run(['g++',f'-DGI_TEST_WIDTH={args.width}','-std=c++17','-O2','-shared','-fPIC','-I'+str(root/'engine/src'),'-I'+str(root/'sandbox/src'),str(root/'engine/tests/gi_gl_tests.cpp'),*[str(root/'engine/src'/s) for s in src],'-o',str(library)],check=True)
        lib=c.CDLL(str(library))
        callback=c.CFUNCTYPE(ctx.ptr,c.c_char_p)(lambda name:ctx.getproc(name))
        render=lib.gi_render_test;render.restype=ctx.I
        render.argtypes=[type(callback),c.c_char_p,c.c_char_p,ctx.I,ctx.I,ctx.I,ctx.I,ctx.I]
        geo=str(root/'benchmarks/cornell_box/geometry').encode()
        for variant in range(3):
            for enabled in (0,1):
                name=f'cornell0{variant+1}-gi{enabled}'
                code=render(callback,geo,str(args.out/(name+'.ppm')).encode(),variant,args.frames if enabled else 1,enabled,0,0 if args.full_lighting else 1)
                if code:raise RuntimeError(f'{name}: GL integration code {code}')
                print('PASS rendered',name,flush=True)
        code=render(callback,geo,str(args.out/'dark-reset.ppm').encode(),0,4,1,1,0 if args.full_lighting else 1)
        if code:raise RuntimeError(f'dark reset: GL integration code {code}')
    from PIL import Image
    import numpy as np
    dark=np.array(Image.open(args.out/'dark-reset.ppm'))
    assert dark.max()<=2, 'Light-off reset retained stale GI'
    a=np.array(Image.open(args.out/'cornell01-gi0.ppm')).astype(float)
    b=np.array(Image.open(args.out/'cornell01-gi1.ppm')).astype(float)
    # Front of the tall box receives no direct light; bounce must illuminate it.
    scale=args.width/640
    region=(slice(int(175*scale),int(325*scale)),slice(int(235*scale),int(278*scale)))
    before=a[region].mean();after=b[region].mean()
    assert after>before+5,(before,after)
    print(f'PASS indirect light on unlit box: {before:.2f} -> {after:.2f}')
    print('PASS light-off history invalidation')
    for p in args.out.glob('*.ppm'):
        Image.open(p).save(p.with_suffix('.png'))
    print('Headless integration passed. Measure actual frame costs on the target GPU.')

if __name__=='__main__':main()
