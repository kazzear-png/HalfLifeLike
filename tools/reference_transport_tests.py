#!/usr/bin/env python3
"""Behavior tests for ray advancement and diffuse/specular mixture transport."""
import numpy as np
import reference_pathtracer as r

class Diffuse:
    is_emitter = False
    albedo = np.array([0.5, 0.5, 0.5])
    roughness = 0.9
    metalness = 0.0
    def __init__(self): self.primary_origins = []
    def intersect(self, ro, rd, *, normals=True):
        if not normals:
            return np.full(len(ro), np.inf), None
        self.primary_origins.append(ro.copy())
        # Hit an upward-facing diffuse plane only from the initial ray origin.
        initial = ro[:,1] > 0.5
        t = np.where(initial, 1.0, np.inf)
        n = np.zeros_like(ro); n[:,1] = 1
        return t,n

class Emitter:
    is_emitter = True
    radiance = 1.0
    xmin, xmax, zmin, zmax, y, area = -0.5,0.5,-0.5,0.5,2.0,1.0
    def intersect(self,ro,rd,*,normals=True):
        return np.full(len(ro),np.inf),np.zeros_like(ro) if normals else None

# After the first bounce, the next query must originate at the first surface,
# not at the camera again. Input arrays must remain intact for callers.
n=2048;origin=np.tile([0.,1.,0.],(n,1));direction=np.tile([0.,-1.,0.],(n,1))
prim=Diffuse();emitter=Emitter()
a=r.trace([prim,emitter],emitter,origin,direction,np.random.RandomState(7),3)
assert len(prim.primary_origins)==2
assert np.allclose(prim.primary_origins[1][:,1],r.OFFSET)
assert np.all(origin[:,1]==1) and np.all(direction[:,1]==-1)
b=r.trace([Diffuse(),emitter],emitter,origin,direction,np.random.RandomState(7),1)
np.testing.assert_array_equal(a,b) # Escaping rays cannot collect the same direct light twice.
print('PASS ray advancement, immutable inputs, escaping-path energy')

# A primary emitter hit contributes emission once and then terminates.
class VisibleEmitter(Emitter):
    def intersect(self,ro,rd,*,normals=True):
        return np.ones(len(ro)),np.tile([0.,1.,0.],(len(ro),1))
v=VisibleEmitter()
out=r.trace([v],v,origin,direction,np.random.RandomState(7),6)
np.testing.assert_array_equal(out,np.ones((n,3)))
print('PASS emitter termination')

# Integrate a Lambertian unit furnace using a mixture of cosine and uniform
# hemisphere proposals. Correct mixture weighting should return albedo once.
rng=np.random.RandomState(9);count=400000;p=0.4
choose=rng.random(count)<p
u=rng.random(count)
cos=np.where(choose,u,np.sqrt(u))
pdf=p/(2*np.pi)+(1-p)*cos/np.pi
estimate=np.mean(0.5/np.pi*cos/pdf)
assert abs(estimate-.5)<.003,estimate
print('PASS mixture-PDF energy:',estimate)
