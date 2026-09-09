// Headless integration harness used by tools/verify_realtime_gi.py.
// Uses real engine meshes, shaders, HDR targets and GI code with an EGL context.
#include "rendering/DiffuseGI.h"
#include "rendering/Renderer.h"
#include "rendering/Mesh.h"
#include "rendering/Camera.h"
#include "assets/OBJ.h"
#include "cornell_scene_gen.h"
#include "shaders.h"
#include <string>
#include <cstring>
#include <cstdio>
#include <vector>

#ifndef GI_TEST_WIDTH
#define GI_TEST_WIDTH 640
#endif
static constexpr int width = GI_TEST_WIDTH, height = width * 9 / 16;

extern "C" int gi_render_test(engine::gl::ProcAddressFn resolver, const char* geometry,
                              const char* output, int variant, int frames, int giOn, int darkReset, int giOnly) {
    using namespace engine;
    if (!gl::load(resolver)) return 1;
    Renderer renderer; renderer.init(); renderer.setClearColor(0,0,0,1);
    if (!renderer.initHDR(width,height,4)) return 2;
    Camera camera; camera.setPosition(Vec3(0,2.75f,8.35f));
    camera.setPerspective(cornell::kCameraFovYRadians,static_cast<float>(width)/height,0.1f,100);
    std::string source = shaders::kPbrFragment;
    const std::string version = "#version 330 core\n";
    source.insert(source.find(version)+version.size(),std::string("#define AREA_FAST_BUILD 1\n#define SURFACE_GI_BUILD 1\n")+DiffuseGI::samplingSource());
    if (giOnly) source = std::string("#version 330 core\n") + DiffuseGI::samplingSource() +
        "in vec3 vWorldPos; in vec3 vNormal; uniform vec3 uAlbedo; uniform float uMetalness; out vec4 FragColor;\n"
        "void main(){FragColor=vec4(0.96*(1.0-uMetalness)*uAlbedo*giLookup(vWorldPos,normalize(vNormal)),1.0);}";
    std::fprintf(stderr,"[GI test] Building scene shader\n");
    Shader shader = Shader::fromSource(shaders::kPbrVertex,source.c_str());
    Shader unlit = Shader::fromSource(shaders::kUnlitVertex,shaders::kUnlitFragment);
    if (!shader.valid() || !unlit.valid()) return 3;
    std::fprintf(stderr,"[GI test] Scene shader linked\n");
    const auto* def = cornell::kVariants[variant];
    std::vector<Mesh> meshes(def->meshCount);
    std::vector<const cornell::MaterialDef*> materials(def->meshCount,nullptr);
    std::vector<int> giIndices(def->meshCount,-1);
    std::vector<GiSurface> surfaces;
    float boxesLo[16]{},boxesHi[16]{},spheres[16]{};
    int boxes=0,sphereCount=0,emitter=-1;
    for (unsigned i=0;i<def->meshCount;++i) {
        auto model=loadOBJ(std::string(geometry)+"/"+def->meshes[i].file,LoadObjOptions{false,0.0f});
        if(!model.ok || !meshes[i].create(model.vertices.data(),static_cast<unsigned>(model.vertices.size()),model.indices.data(),static_cast<unsigned>(model.indices.size())))return 4;
        for(const auto& entry:cornell::kMaterialTable)
            if(std::strcmp(entry.name,def->meshes[i].material)==0)materials[i]=&entry.def;
        if(!materials[i]) {emitter=static_cast<int>(i);continue;}
        Aabb b;b.seed(Vec3(model.vertices[0].x,model.vertices[0].y,model.vertices[0].z));
        for(const auto& v:model.vertices)b.grow(Vec3(v.x,v.y,v.z));
        bool sphere=std::strncmp(def->meshes[i].material,"sphere_",7)==0;
        int mask=63;
        if(!sphere){
            float e[3]={b.max.x-b.min.x,b.max.y-b.min.y,b.max.z-b.min.z};
            float n[3]={model.vertices[0].nx,model.vertices[0].ny,model.vertices[0].nz};
            for(int a=0;a<3;++a)if(e[a]<1e-6f)mask=1<<(a*2+(n[a]>0?1:0));
        }
        GiSurface s;
        s.lo={b.min.x,b.min.y,b.min.z,sphere?1.0f:0.0f};
        s.hi={b.max.x,b.max.y,b.max.z,static_cast<float>(mask)};
        auto* m=materials[i];s.material={m->albedo[0],m->albedo[1],m->albedo[2],m->metalness};
        giIndices[i]=static_cast<int>(surfaces.size());surfaces.push_back(s);
        if(sphere){
            float* a=spheres+4*sphereCount++;a[0]=(b.min.x+b.max.x)*.5f;a[1]=(b.min.y+b.max.y)*.5f;a[2]=(b.min.z+b.max.z)*.5f;a[3]=(b.max.x-b.min.x)*.5f;
        } else if(std::strncmp(def->meshes[i].material,"block_",6)==0 || std::strcmp(def->meshes[i].material,"baffle")==0){
            float* a=boxesLo+4*boxes;float* c=boxesHi+4*boxes++;
            a[0]=b.min.x;a[1]=b.min.z;a[2]=b.min.y;c[0]=b.max.x;c[1]=b.max.z;c[2]=b.max.y;
        }
    }
    DiffuseGI gi;
    std::fprintf(stderr,"[GI test] Building GI shader\n");
    if(!gi.setScene(surfaces) || !gi.init(16,8,32))return 5;
    std::fprintf(stderr,"[GI test] GI shader linked\n");
    shader.bind();
    shader.setMat4("uViewProj",camera.viewProjection());shader.setMat4("uModel",Mat4::identity());shader.setMat4("uNormalMat",Mat4::identity());
    shader.setFloat3("uViewPos",camera.position());shader.setInt("uAreaOn",1);shader.setInt("uShadowOn",1);
    shader.setFloat3("uAreaCenter",0,5.49f,0);shader.setFloat("uAreaLe",12);shader.setFloat("uShadowBias",.01f);
    shader.setFloat4("uEmitterHalf",.65f,.525f,0,0);
    shader.setInt("uShadowBoxCount",boxes);shader.setFloat4Array("uShadowBoxMin[0]",boxesLo,boxes);shader.setFloat4Array("uShadowBoxMax[0]",boxesHi,boxes);
    shader.setInt("uShadowSphereCount",sphereCount);shader.setFloat4Array("uShadowSphere[0]",spheres,sphereCount);
    unlit.bind();unlit.setMat4("uViewProj",camera.viewProjection());unlit.setMat4("uModel",Mat4::identity());unlit.setFloat3("uTint",giOnly?0:12,giOnly?0:12,giOnly?0:12);
    for(int frame=0;frame<frames;++frame){
        if (frame % 16 == 0) std::fprintf(stderr,"[GI test] frame %d/%d\n",frame,frames);
        if(darkReset && frame==frames-1){
            gi.setLight(Vec3(0,5.49f,0),.65f,.525f,0);
            shader.bind();shader.setFloat("uAreaLe",0);unlit.bind();unlit.setFloat3("uTint",0,0,0);
        }
        renderer.setViewport(0,0,width,height);renderer.beginFrame();
        if(giOn){gi.update();renderer.bindSceneTarget(width,height);}
        shader.bind();
        for(unsigned i=0;i<meshes.size();++i){
            auto* mat=materials[i];if(!mat)continue;
            shader.setFloat3("uAlbedo",mat->albedo[0],mat->albedo[1],mat->albedo[2]);shader.setFloat("uRoughness",mat->roughness);shader.setFloat("uMetalness",mat->metalness);
            if(giOn)gi.bind(shader,giIndices[i]);else shader.setInt("uGiSurface",-1);
            renderer.drawIndexed(meshes[i]);
        }
        unlit.bind();if(emitter>=0)renderer.drawIndexed(meshes[emitter]);renderer.endFrame();
    }
    std::vector<unsigned char> pixels(width*height*4);
    if(!renderer.readBackbufferPixels(width,height,pixels.data()))return 6;
    FILE* f=std::fopen(output,"wb");if(!f)return 7;
    std::fprintf(f,"P6\n%d %d\n255\n",width,height);
    for(int y=height-1;y>=0;--y)for(int x=0;x<width;++x)std::fwrite(&pixels[(y*width+x)*4],1,3,f);
    if(std::fclose(f)!=0)return 8;
    return gl::GetError()==gl::NoError?0:9;
}
