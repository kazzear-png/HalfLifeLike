"""Linux headless test context using installed EGL/OpenGL libraries via ctypes."""
import ctypes as c
E=c.CDLL('libEGL.so.1')
def bind(lib,name,restype,args):
 f=getattr(lib,name);f.restype=restype;f.argtypes=args;return f
ptr=c.c_void_p;I=c.c_int;U=c.c_uint
getproc=bind(E,'eglGetProcAddress',ptr,[c.c_char_p])
getdisplay=c.CFUNCTYPE(ptr,U,ptr,c.POINTER(I))(getproc(b'eglGetPlatformDisplayEXT'))
display=getdisplay(0x31DD,None,None)
init=bind(E,'eglInitialize',U,[ptr,c.POINTER(I),c.POINTER(I)])
a=I();b=I();assert init(display,c.byref(a),c.byref(b)), 'EGL initialize failed'
assert bind(E,'eglBindAPI',U,[U])(0x30A2)
attrs=(I*9)(0x3033,1,0x3040,8,0x3024,8,0x3025,24,0x3038)
config=ptr();count=I()
assert bind(E,'eglChooseConfig',U,[ptr,c.POINTER(I),c.POINTER(ptr),I,c.POINTER(I)])(display,attrs,c.byref(config),1,c.byref(count)) and count.value
ctxattrs=(I*7)(0x3098,3,0x30FB,3,0x30FD,1,0x3038)
context=bind(E,'eglCreateContext',ptr,[ptr,ptr,ptr,c.POINTER(I)])(display,config,None,ctxattrs)
assert context
pattrs=(I*5)(0x3057,640,0x3056,360,0x3038)
surface=bind(E,'eglCreatePbufferSurface',ptr,[ptr,ptr,c.POINTER(I)])(display,config,pattrs)
assert surface
assert bind(E,'eglMakeCurrent',U,[ptr,ptr,ptr,ptr])(display,surface,surface,context)
G=c.CDLL('libGL.so.1')
def gl(name,result,args):return bind(G,name,result,args)
print('Headless GL:',gl('glGetString',c.c_char_p,[U])(0x1F02).decode())
