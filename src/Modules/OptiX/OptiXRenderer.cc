/**********************************************************************************
*                     Copyright (c) 2013-2015 Carson Brownlee
*         Texas Advanced Computing Center, University of Texas at Austin
*                       All rights reserved
*
*       This library is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public
* License as published by the Free Software Foundation; either
* version 2.1 of the License, or (at your option) any later version.
*
* This library is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with this library; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
**********************************************************************************/

#include <UseMPI.h>
#ifdef USE_MPI
#include <Engine/Display/NullDisplay.h>
#include <Engine/LoadBalancers/MPI_LoadBalancer.h>
#include <Engine/ImageTraversers/MPI_ImageTraverser.h>
#include <mpi.h>
#endif

#include "defines.h"
#include "OptiXRenderer.h"

#include <sutil.h>
#include <glm.h>
#include <ImageLoader.h>
#include <optixu/optixpp_namespace.h>
#include <optixu/optixu_aabb_namespace.h>
#include <optixu/optixu_matrix_namespace.h>
#include <sample6/random.h>

//#include <X11/Xlib.h>
//#include <X11/Xutil.h>

//#include "../gl_functions.h"
#include "CDTimer.h"
#include "OScene.h"
#include "ORenderable.h"
#include "common.h"
#include <Modules/Manta/AccelWork.h>
#include <OBJScene.h>

#include <Core/Util/Logger.h>

#include <Model/Primitives/KenslerShirleyTriangle.h>
#include <Engine/PixelSamplers/RegularSampler.h>
#include <Engine/Display/FileDisplay.h>
#include <Image/SimpleImage.h>
#include <Model/Materials/Dielectric.h>
#include <Model/Materials/ThinDielectric.h>
#include <Model/Materials/OrenNayar.h>
#include <Model/Materials/Transparent.h>
#include <Model/AmbientLights/AmbientOcclusionBackground.h>
#include <Model/AmbientLights/AmbientOcclusion.h>
#include <Model/Textures/TexCoordTexture.h>
#include <Model/Textures/Constant.h>
#include <Model/Primitives/Plane.h>
#include <Model/Primitives/Parallelogram.h>
#include <Model/Primitives/Cube.h>
#include <Model/Primitives/Disk.h>



#include <stdio.h>
#include <cassert>
#include <float.h>
#include <stack>
#include <algorithm>

#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <errno.h>



#include <arpa/inet.h>

#include <GL/gl.h>

#define USE_AO 0

using namespace optix;
using namespace glr;

std::string ptxPath( const std::string& base )
{
  return std::string("/work/01336/carson/opt/optix/build/lib/ptx/") + std::string("/sample5pp_generated_") + base + std::string(".ptx");
}

std::string ptxpath(std::string base, std::string subset)
{
  return std::string("/work/01336/carson/opt/optix/build/lib/ptx/") + base + std::string("_generated_") + subset + std::string(".ptx");
}

OptiXRenderer* OptiXRenderer::_singleton = NULL;

OptiXRenderer* OptiXRenderer::singleton()
{
  if (!_singleton)
    _singleton = new OptiXRenderer();
  return _singleton;
}


OptiXRenderer::OptiXRenderer()
:Renderer(), current_scene(NULL), next_scene(NULL),
_nid_counter(0), _depth(false), _width(0), _height(0), _frameNumber(0), _realFrameNumber(0)
{
  initialized=false;
  printf("%s::%s\n",typeid(*this).name(),__FUNCTION__);
  _format = "RGBA8";

  _gVoid = new OGeometryGeneratorVoid();
  _gTriangle = new OGeometryGeneratorTriangles();
  _gTriangleStrip = new OGeometryGeneratorTriangleStrip();
  _gQuads = new OGeometryGeneratorQuads();
  _gQuadStrip = new OGeometryGeneratorQuadStrip();
  _gLines = new OGeometryGeneratorLines();
  _gLineStrip= new OGeometryGeneratorLineStrip();

  _bufferMapped = false;
}

OptiXRenderer::~OptiXRenderer()
{
}

void OptiXRenderer::updateLights()
{

}

Renderable* OptiXRenderer::createRenderable(GeometryGenerator* gen)
{
  OGeometryGenerator* mg = dynamic_cast<OGeometryGenerator*>(gen);
  assert(mg);
  return new ORenderable(mg);
}

void  OptiXRenderer::updateMaterial()
{
  if (!initialized)
    return;
  GLMaterial m = gl_material;
  //TODO: DEBUG: hardcoding mat for debugging
  // m.diffuse = Color(RGB(1.0, 0.713726, .21569));
  //m.diffuse = Color(RGB(0.8, 0.8, 0.8));
  // m.specular = Manta::Color(RGB(.1, .1, .1));
  // m.ambient = Manta::Color(RGB(0, 0, 0));
  // m.shiny = 100;

}

void OptiXRenderer::useShadows(bool st)
{
}

void OptiXRenderer::setSize(int w, int h)
{

  printf("setSize %d %d\n", w,h);
  if (initialized && (w != _width || h != _height))
  {
    _width = w; _height = h;
      if (_bufferMapped)
    {
      buffer->unmap();
      _bufferMapped = false;
    }
    buffer = context->createBuffer( RT_BUFFER_OUTPUT, RT_FORMAT_UNSIGNED_BYTE4, _width, _height );
    updateCamera();
    #if USE_AO
    // if( m_rnd_seeds.get() == 0 ) {
    m_rnd_seeds = context->createBuffer( RT_BUFFER_INPUT, RT_FORMAT_UNSIGNED_INT,
      w, h);
    // }

    unsigned int* seeds = static_cast<unsigned int*>( m_rnd_seeds->map() );
    fillRandBuffer(seeds, w*h);
    m_rnd_seeds->unmap();
    context["rnd_seeds"]->setBuffer(m_rnd_seeds);
    #endif

  }
}


void OptiXRenderer::init()
{
  if (initialized)
    return;
  initialized=true;
  printf("%s::%s\n",typeid(*this).name(),__FUNCTION__);

  updateBackground();
  updateCamera();
  updateMaterial();
  if (!current_scene)
    current_scene = new OScene();
  if (!next_scene)
    next_scene = new OScene();


  int width, height;
  width=_width;
  height=_height;
  bool m_accum_enabled = false;
  enum CameraMode
  {
    CM_PINHOLE=0,
    CM_ORTHO
  };
  int m_camera_mode  = CM_PINHOLE;

  // Set up context
  context = optix::Context::create();
  // context->setRayTypeCount( 1 );
  // context->setEntryPointCount( 1 );
  context->setRayTypeCount( 3 );
  context->setEntryPointCount( 1 );
  context->setStackSize( 1180 );

  context[ "scene_epsilon"      ]->setFloat( 1e-3f );
  context[ "occlusion_distance" ]->setFloat( 100.0f );
  context[ "sqrt_occlusion_samples" ]->setInt( 10 );

  context[ "radiance_ray_type"   ]->setUint( 0u );
  //context[ "shadow_ray_type"     ]->setUint( 1u );
  context[ "max_depth"           ]->setInt( 5 );
  context[ "ambient_light_color" ]->setFloat( 0.2f, 0.2f, 0.2f );
  // context[ "output_buffer"       ]->set( createOutputBuffer(RT_FORMAT_UNSIGNED_BYTE4, width, height) );
  context[ "jitter_factor"       ]->setFloat( m_accum_enabled ? 1.0f : 0.0f );


  context["frame"]->setInt(0u);
  context["bg_color"]->setFloat(0.462f, 0.725f, 0.0f);
  context["bad_color"]->setFloat(1.0f, 1.0f, 0.0f );


  const std::string camera_name = m_camera_mode == CM_PINHOLE ? "pinhole_camera" : "orthographic_camera";
  const std::string camera_file = m_accum_enabled             ? "accum_camera.cu" :
  m_camera_mode == CM_PINHOLE ? "pinhole_camera.cu"  :
  "orthographic_camera.cu";

  const std::string camera_ptx  = ptxpath( "sample6", camera_file );
  Program ray_gen_program = context->createProgramFromPTXFile( camera_ptx, camera_name );
  context->setRayGenerationProgram( 0, ray_gen_program );


  // Exception program
  const std::string except_ptx  = ptxpath( "sample6", camera_file );
  context->setExceptionProgram( 0, context->createProgramFromPTXFile( except_ptx, "exception" ) );
  context[ "bad_color" ]->setFloat( 0.0f, 1.0f, 0.0f );


  // Miss program
  const std::string miss_ptx = ptxpath( "sample6", "constantbg.cu" );
  context->setMissProgram( 0, context->createProgramFromPTXFile( miss_ptx, "miss" ) );
  context[ "bg_color" ]->setFloat(  0.34f, 0.55f, 0.85f );


  typedef struct struct_BasicLight
  {
    #if defined(__cplusplus)
    typedef optix::float3 float3;
    #endif
    float3 pos;
    float3 color;
    int    casts_shadow;
    int    padding;      // make this structure 32 bytes -- powers of two are your friend!
  } BasicLight;

  int m_light_scale = 1.5;
  // Lights buffer
  //BasicLight lights[] = {
  //  { make_float3( -60.0f,  30.0f, -120.0f ), make_float3( 0.2f, 0.2f, 0.25f )*m_light_scale, 0, 0 },
  //  { make_float3( -60.0f,   0.0f,  120.0f ), make_float3( 0.1f, 0.1f, 0.10f )*m_light_scale, 0, 0 },
  //  { make_float3(  60.0f,  60.0f,   60.0f ), make_float3( 0.7f, 0.7f, 0.65f )*m_light_scale, 1, 0 }
  //};

  BasicLight lights[] = {
    { make_float3(  60.0f,  60.0f,   60.0f ), make_float3( 0.7f, 0.7f, 0.65f )*m_light_scale, 0, 0 }
  };

  Buffer light_buffer = context->createBuffer(RT_BUFFER_INPUT);
  light_buffer->setFormat(RT_FORMAT_USER);
  light_buffer->setElementSize(sizeof( BasicLight ) );
  light_buffer->setSize( sizeof(lights)/sizeof(lights[0]) );
  lights[0].casts_shadow = false;
  memcpy(light_buffer->map(), lights, sizeof(lights));
  light_buffer->unmap();

  context[ "lights" ]->set( light_buffer );

}

//TODO: updating pixelsampler mid flight crashes manta
void OptiXRenderer::setNumSamples(int,int,int samples)
{
}

void OptiXRenderer::setNumThreads(int t)
{
}

size_t OptiXRenderer::generateNID()
{
  return 0;
  // return ++_nid_counter;
}

Renderable* OptiXRenderer::getRenderable(size_t nid)
{
  return _map_renderables[nid];
}

void* OptiXRenderer::renderLoop(void* t)
{
}

void OptiXRenderer::internalRender()
{
}


void OptiXRenderer::render()
{
  //printf("optix render\n");
  if (!initialized)
    return;
  if (next_scene->instances.size() == 0)
    return;

  int width, height;
  width=_width;
  height=_height;
  bool m_accum_enabled = false;
  enum CameraMode
  {
    CM_PINHOLE=0,
    CM_ORTHO
  };
  int m_camera_mode  = CM_PINHOLE;


  optix::Variable output_buffer = context["output_buffer"];
  //static bool once = false;
  //if (!once)
  //{
  //  buffer = context->createBuffer( RT_BUFFER_OUTPUT, RT_FORMAT_UNSIGNED_BYTE4, width, height );
  //  once = true;
  //}
  output_buffer->set(buffer);

  // Ray generation program
  // std::string ptx_path( ptxPath( "pinhole_camera.cu" ) );
  // std::cout << "ptx_path: " << ptx_path << std::endl;
  // Program ray_gen_program = context->createProgramFromPTXFile( ptx_path, "pinhole_camera" );
  // context->setRayGenerationProgram( 0, ray_gen_program );


  GLuRayRenderParameters& p = params;
  optix::float3 cam_eye = { p.camera_eye.x(), p.camera_eye.y(), p.camera_eye.z() };
  optix::float3 lookat  = { p.camera_dir.x()+p.camera_eye.x(), p.camera_dir.y()+p.camera_eye.y(), p.camera_dir.z()+p.camera_eye.z() };
  optix::float3 up      = { p.camera_up.x(), p.camera_up.y(), p.camera_up.z() };
  float  hfov    = p.camera_vfov;
  float  aspect_ratio = static_cast<float>(width) / static_cast<float>(height);
  optix::float3 camera_u, camera_v, camera_w;
  sutilCalculateCameraVariables( &cam_eye.x, &lookat.x, &up.x, hfov, aspect_ratio,
    &camera_u.x, &camera_v.x, &camera_w.x );

  context["eye"]->setFloat( cam_eye );
  context["U"]->setFloat( camera_u );
  context["V"]->setFloat( camera_v );
  context["W"]->setFloat( camera_w );

  //optix::Geometry sphere = context->createGeometry();
  //sphere->setPrimitiveCount( 1u );
  //sphere->setBoundingBoxProgram( context->createProgramFromPTXFile( ptxPath( "sphere.cu" ), "bounds" ) );
  //sphere->setIntersectionProgram( context->createProgramFromPTXFile( ptxPath( "sphere.cu" ), "intersect" ) );
  //sphere["sphere"]->setFloat( 0, 0, 0, 1.5 );

  //create material
  //optix::Program chp = context->createProgramFromPTXFile( ptxPath( "normal_shader.cu" ), "closest_hit_radiance" );

  //optix::Material material = context->createMaterial();
  //material->setClosestHitProgram( 0, chp );

  // Create geometry instance
  //optix::GeometryInstance gi = context->createGeometryInstance();
  //gi->setMaterialCount( 1 );
  //gi->setGeometry( sphere );
  //gi->setMaterial( 0, material );

  // Create geometry group
  static optix::Group top_group = context->createGroup();
  int validChildren=0;
  int childIndex=0;
  for(vector<GRInstance>::iterator itr = next_scene->instances.begin(); itr != next_scene->instances.end(); itr++)
  {
    // Manta::AffineTransform mt = itr->transform;
    Renderable* ren = itr->renderable;
    ORenderable* er = dynamic_cast<ORenderable*>(ren);
    if (er->isBuilt())
    {
      validChildren++;
    }
  }
  top_group->setChildCount( validChildren );
  for(vector<GRInstance>::iterator itr = next_scene->instances.begin(); itr != next_scene->instances.end(); itr++)
  {
    Manta::AffineTransform mt = itr->transform;
    Renderable* ren = itr->renderable;
    ORenderable* er = dynamic_cast<ORenderable*>(ren);
    if (er->isBuilt())
    {
      // optix::Transform transform = context->createTransform();
      if (!er->transform)
      {
        er->transform = new optix::Transform;
        *(er->transform) = context->createTransform();
      }
      optix::Transform transform = *(er->transform);
      transform->setChild(er->gg);
      // // if (er->gg->getAcceleration()->isDirty())
      //   // printf("accel is dirty\n");
      float m[16] = {mt(0,0),mt(0,1),mt(0,2),mt(0,3),
       mt(1,0),mt(1,1),mt(1,2),mt(1,3),
       mt(2,0),mt(2,1),mt(2,2),mt(2,3),
       0,0,0,1 };

       transform->setMatrix( false, m, NULL );

       top_group->setChild( childIndex++, transform);
                     // top_group->setChild( childIndex++, er->gg);
     }
   }
   next_scene->instances.resize(0);

   optix::Acceleration acceleration = context->createAcceleration("NoAccel", "NoAccel");
   top_group->setAcceleration( acceleration );
   acceleration->markDirty();

   static bool once = false;
   if (!once)
   {
    once=false;
    context["top_object"]->set( top_group );
    context[ "top_shadower" ]->set( top_group );
  }

    //context->validate();
  //static bool once2 = false;
  //if (!once2)
  //  {
  //  once2=true;
    //context->compile();
  //}

    //launch

    // Run

  if (_bufferMapped)
  {
    buffer->unmap();
    _bufferMapped = false;
  }
  context->compile();
  context->launch( 0, width, height );
  buffer = context["output_buffer"]->getBuffer();

  GLvoid* imageData = buffer->map();
  _bufferMapped = true;
  RTformat buffer_format = buffer->getFormat();
  assert( imageData );

  GLenum gl_data_type = GL_FALSE;
  GLenum gl_format = GL_FALSE;

  switch (buffer_format) {
    case RT_FORMAT_UNSIGNED_BYTE4:
    gl_data_type = GL_UNSIGNED_BYTE;
    gl_format    = GL_BGRA;
    _format = "BGRA8";
    break;

    case RT_FORMAT_FLOAT:
    gl_data_type = GL_FLOAT;
    gl_format    = GL_LUMINANCE;
    _format = "float1";
    break;

    case RT_FORMAT_FLOAT3:
    gl_data_type = GL_FLOAT;
    gl_format    = GL_RGB;
    _format="float3";
    break;

    case RT_FORMAT_FLOAT4:
    gl_data_type = GL_FLOAT;
    gl_format    = GL_RGBA;
    _format="float4";
    break;

    default:
    fprintf(stderr, "Unrecognized buffer data type or format.\n");
    exit(2);
    break;
  }

  RTsize elementSize = buffer->getElementSize();
  int align = 1;
  if      ((elementSize % 8) == 0) align = 8;
  else if ((elementSize % 4) == 0) align = 4;
  else if ((elementSize % 2) == 0) align = 2;

  _framebuffer.byteAlign = align;
  _framebuffer.width = _width;
  _framebuffer.height = _height;
  _framebuffer.data = (void*)imageData;
  _framebuffer.format = "BGRA8";

}



void OptiXRenderer::syncInstances()
{}

void OptiXRenderer::updateCamera()
{
    //JOAO: put camera update here
}

void OptiXRenderer::updateBackground()
{
}

void OptiXRenderer::addInstance(Renderable* ren)
{
  if (!ren->isBuilt())
  {
    std::cerr << "addInstance: renderable not built by rendermanager\n";
    return;
  }
  next_scene->instances.push_back(GRInstance(ren, current_transform));
}

void OptiXRenderer::addRenderable(Renderable* ren)
{
  ORenderable* oren = dynamic_cast<ORenderable*>(ren);
  if (!oren)
  {
    printf("error: OptiXRenderer::addRenderable wrong renderable type\n");
    return;
  }
    // static int hackCounter=0;  //VTK tends to compute things twice
    // if (hackCounter++ < 2)
    //   return;

  Manta::Mesh* mesh = oren->_data->mesh;
  assert(mesh);
  size_t numTexCoords = mesh->texCoords.size();
  size_t numPositions = mesh->vertices.size();
  printf("addrenderable called mesh indices/3 vertices normals texcoords: %d %d %d %d \n", mesh->vertex_indices.size()/3, mesh->vertices.size(), mesh->vertexNormals.size(),
   mesh->texCoords.size());
  size_t numTriangles = mesh->vertex_indices.size()/3;
    //
    // hack!
    //
    // mesh->vertexNormals.resize(0);
    // for(int i=0; i < 1000;i++)
  {
      // Manta::Vector n = Manta::Vector(0,0,1);
        // mesh->vertexNormals.push_back(n);
  }
  if (!mesh->vertexNormals.size())
  {

    for(int i =0; i < numTriangles;i++)
    {
      Manta::Vector v1 = mesh->vertices[mesh->vertex_indices[i*3] ];
      Manta::Vector v2 = mesh->vertices[mesh->vertex_indices[i*3+1] ];
      Manta::Vector v3 = mesh->vertices[mesh->vertex_indices[i*3+2] ];
      Manta::Vector n = Manta::Cross(v2-v1,v3-v1);

      n.normalize();
        // n = Manta::Vector(0,0,1);
      mesh->vertexNormals.push_back(n);
      mesh->vertexNormals.push_back(n);
      mesh->vertexNormals.push_back(n);
    }
  }

  size_t numNormals = mesh->vertexNormals.size();
  for (int i =0; i < numNormals; i++)
  {
      // Manta::Vector n = mesh->vertexNormals[i];
          // if (Manta::Dot(n, Manta::Vector(0,0,1)) < 0.0)
    {
          // printf("inverting normal: %f %f %f\n", n[0], n[1], n[2]);
          // n=n*Manta::Vector(-1,-1,-1);
          // mesh->vertexNormals[i]=n;
    }
        // else{
          // printf("normal: %f %f %f\n", n[0], n[1], n[2]);
        // }
  }
    // assert(mesh->vertices.size() == numTriangles*3);


  optix::Geometry sphere = context->createGeometry();
  sphere->setPrimitiveCount( 1u );
  sphere->setBoundingBoxProgram( context->createProgramFromPTXFile( ptxPath( "sphere.cu" ), "bounds" ) );
  sphere->setIntersectionProgram( context->createProgramFromPTXFile( ptxPath( "sphere.cu" ), "intersect" ) );
  sphere["sphere"]->setFloat( 0, 0, 0, 1.5 );


  optix::Matrix4x4 transform = optix::Matrix4x4::identity();
    //
    // vertex data
    //
    //   unsigned int num_vertices  = model->numvertices;
    // unsigned int num_texcoords = model->numtexcoords;
    // unsigned int num_normals   = model->numnormals;
  unsigned int num_vertices = numPositions;
  unsigned int num_texcoords = 0;
  unsigned int num_normals = numNormals;

    // Create vertex buffer
  optix::Buffer m_vbuffer = context->createBuffer( RT_BUFFER_INPUT, RT_FORMAT_FLOAT3, numPositions );
  optix::float3* vbuffer_data = static_cast<optix::float3*>( m_vbuffer->map() );

    // Create normal buffer
  optix::Buffer m_nbuffer = context->createBuffer( RT_BUFFER_INPUT, RT_FORMAT_FLOAT3, num_normals );
  optix::float3* nbuffer_data = static_cast<optix::float3*>( m_nbuffer->map() );

    // Create texcoord buffer
  optix::Buffer m_tbuffer = context->createBuffer( RT_BUFFER_INPUT, RT_FORMAT_FLOAT2, num_texcoords );
  optix::float2* tbuffer_data = static_cast<optix::float2*>( m_tbuffer->map() );

    // Transform and copy vertices.
  for ( unsigned int i = 0; i < num_vertices; ++i )
  {
      // const optix::float3 v3 = *((optix::float3*)&mesh->vertices[(i)*3]);
    const optix::float3 v3 = make_float3(mesh->vertices[(i)].x(), mesh->vertices[(i)].y(), mesh->vertices[(i)].z());
    optix::float4 v4 = make_float4( v3, 1.0f );
    vbuffer_data[i] = optix::make_float3( transform*v4 );
  }

    // Transform and copy normals.
    // const optix::Matrix4x4 norm_transform = transform.inverse().transpose();
  for( unsigned int i = 0; i < num_normals; ++i )
  {
      // const optix::float3 v3 = *((optix::float3*)&mesh->vertexNormals[(i)*3]);
    mesh->vertexNormals[i].normalize();
    const optix::float3 v3 = make_float3(mesh->vertexNormals[(i)].x(), mesh->vertexNormals[(i)].y(),mesh->vertexNormals[(i)].z());
      // optix::float4 v4 = make_float4( v3, 1.0f );
      // nbuffer_data[i] = make_float3( transform*v4 );
    nbuffer_data[i] = v3;
      // nbuffer_data[i] = make_float3( 1.0,0,0 );
  }

    // Copy texture coordinates.
    // memcpy( static_cast<void*>( tbuffer_data ),
    //         static_cast<void*>( &(model->texcoords[2]) ),
    //         sizeof( float )*num_texcoords*2 );

    // Calculate bbox of model
    // for( unsigned int i = 0; i < num_vertices; ++i )
    // m_aabb.include( vbuffer_data[i] );

    // Unmap buffers.
  m_vbuffer->unmap();
  m_nbuffer->unmap();
  m_tbuffer->unmap();

    //
    // create instance
    //

    // Load triangle_mesh programs
    // if( !m_intersect_program.get() ) {
  std::string path = std::string("/work/01336/carson/opt/optix/build/lib/ptx/") + std::string("/cuda_compile_ptx_generated_triangle_mesh.cu.ptx");
  optix::Program m_intersect_program = context->createProgramFromPTXFile( path, "mesh_intersect" );
    // }

    // if( !m_bbox_program.get() ) {
  path = std::string("/work/01336/carson/opt/optix/build/lib/ptx/") + std::string("/cuda_compile_ptx_generated_triangle_mesh.cu.ptx");
  optix::Program m_bbox_program = context->createProgramFromPTXFile( path, "mesh_bounds" );
    // }

  std::vector<GeometryInstance> instances;

    // Loop over all groups -- grab the triangles and material props from each group
  unsigned int triangle_count = 0u;
  unsigned int group_count = 0u;
    // for ( GLMgroup* obj_group = model->groups;
    // obj_group != 0;
    // obj_group = obj_group->next, group_count++ ) {

  unsigned int num_triangles = numTriangles;
    // unsigned int num_triangles = obj_group->numtriangles;
    // if ( num_triangles == 0 ) continue;

    // Create vertex index buffers
  optix::Buffer vindex_buffer = context->createBuffer( RT_BUFFER_INPUT, RT_FORMAT_INT3, num_triangles );
  optix::int3* vindex_buffer_data = static_cast<optix::int3*>( vindex_buffer->map() );

  optix::Buffer tindex_buffer = context->createBuffer( RT_BUFFER_INPUT, RT_FORMAT_INT3, num_triangles );
  optix::int3* tindex_buffer_data = static_cast<optix::int3*>( tindex_buffer->map() );

  optix::Buffer nindex_buffer = context->createBuffer( RT_BUFFER_INPUT, RT_FORMAT_INT3, num_triangles );
  optix::int3* nindex_buffer_data = static_cast<optix::int3*>( nindex_buffer->map() );

  Buffer mbuffer = context->createBuffer( RT_BUFFER_INPUT, RT_FORMAT_UNSIGNED_INT, num_triangles );
  unsigned int* mbuffer_data = static_cast<unsigned int*>( mbuffer->map() );

    // optix::Geometry oMesh;

  for ( unsigned int i = 0; i < numTriangles; ++i ) {

      // unsigned int tindex = mesh->indices[i];
    int3 vindices;
      // vindices.x = mesh->indices[ tindex ].vindices[0] - 1;
      // vindices.y = model->triangles[ tindex ].vindices[1] - 1;
      // vindices.z = model->triangles[ tindex ].vindices[2] - 1;
    vindices.x = mesh->vertex_indices[i*3+0];
    vindices.y = mesh->vertex_indices[i*3+1];
    vindices.z = mesh->vertex_indices[i*3+2];
    assert( vindices.x <= static_cast<int>(numPositions) );
    assert( vindices.y <= static_cast<int>(numPositions) );
    assert( vindices.z <= static_cast<int>(numPositions) );

    vindex_buffer_data[ i ] = vindices;

    optix::int3 nindices;
    nindices.x = mesh->vertex_indices[i*3+0];
    nindices.y = mesh->vertex_indices[i*3+1];
    nindices.z = mesh->vertex_indices[i*3+2];
      // nindices.x=0;
      // nindices.y=0;
      // nindices.z=0;


    int3 tindices;
      tindices.x = -1;  //model->triangles[ tindex ].tindices[0] - 1;
      tindices.y = -1;  //model->triangles[ tindex ].tindices[1] - 1;
      tindices.z = -1;  //model->triangles[ tindex ].tindices[2] - 1;

      nindex_buffer_data[ i ] = nindices;
      tindex_buffer_data[ i ] = tindices;
      mbuffer_data[ i ] = 0; // See above TODO

    }
    vindex_buffer->unmap();
    tindex_buffer->unmap();
    nindex_buffer->unmap();
    mbuffer->unmap();

    std::vector<int> tri_reindex;

    // if (m_large_geom) {
    //   if( m_ASBuilder == std::string("Sbvh") || m_ASBuilder == std::string("KdTree")) {
    //     m_ASBuilder = "MedianBvh";
    //     m_ASTraverser = "Bvh";
    //   }

    //   float* vertex_buffer_data = static_cast<float*>( m_vbuffer->map() );
    //   RTsize nverts;
    //   m_vbuffer->getSize(nverts);

    //   tri_reindex.resize(num_triangles);
    //   RTgeometry geometry;

    //   unsigned int usePTX32InHost64 = 0;
    //   rtuCreateClusteredMeshExt( context->get(), usePTX32InHost64, &geometry,
    //                             (unsigned int)nverts, vertex_buffer_data,
    //                              num_triangles, (const unsigned int*)vindex_buffer_data,
    //                              mbuffer_data,
    //                              m_nbuffer->get(),
    //                             (const unsigned int*)nindex_buffer_data,
    //                             m_tbuffer->get(),
    //                             (const unsigned int*)tindex_buffer_data);
    //   mesh = optix::Geometry::take(geometry);

    //   m_vbuffer->unmap();
    //   rtBufferDestroy( vindex_buffer->get() );
    // } else
    // {
    // Create the mesh object
    optix::Geometry oMesh = context->createGeometry();
    oMesh->setPrimitiveCount( num_triangles );
    oMesh->setIntersectionProgram( m_intersect_program);
    oMesh->setBoundingBoxProgram( m_bbox_program );
    oMesh[ "vertex_buffer" ]->setBuffer( m_vbuffer );
    oMesh[ "vindex_buffer" ]->setBuffer( vindex_buffer );
    oMesh[ "normal_buffer" ]->setBuffer( m_nbuffer );
    oMesh[ "texcoord_buffer" ]->setBuffer( m_tbuffer );
    oMesh[ "tindex_buffer" ]->setBuffer( tindex_buffer );
    oMesh[ "nindex_buffer" ]->setBuffer( nindex_buffer );
    oMesh[ "material_buffer" ]->setBuffer( mbuffer );
    // }

    // Create the geom instance to hold mesh and material params
    // loadMaterialParams( instance, obj_group->material );
    // instances.push_back( instance );
    // }



    optix::Material material           = context->createMaterial();
    //create material
    // std::string path = std::string(sutilSamplesPtxDir()) + "/cuda_compile_ptx_generated_obj_material.cu.ptx";
    #if !USE_AO
    // optix::Program closest_hit = context->createProgramFromPTXFile( "/work/01336/carson/opt/optix/SDK-precompiled-samples/ptx/cuda_compile_ptx_generated_obj_material.cu.ptx", "closest_hit_radiance" );
    // optix::Program any_hit     = context->createProgramFromPTXFile( "/work/01336/carson/opt/optix/SDK-precompiled-samples/ptx/cuda_compile_ptx_generated_obj_material.cu.ptx", "any_hit_shadow" );
    optix::Program closest_hit = context->createProgramFromPTXFile( "/work/01336/carson/opt/optix/build/lib/ptx/cuda_compile_ptx_generated_phong.cu.ptx", "closest_hit_radiance" );
    // optix::Program any_hit     = context->createProgramFromPTXFile( "/work/01336/carson/opt/optix/SDK-precompiled-samples/ptx/cuda_compile_ptx_generated_phong.cu.ptx", "any_hit_shadow" );

    material->setClosestHitProgram( 0u, closest_hit );
    #else
    float m_ao_sample_mult = 0.4;
    // context["sqrt_occlusion_samples"]->setInt( 3 * m_ao_sample_mult );
    // context["sqrt_diffuse_samples"]->setInt( 3 );
    optix::Program closest_hit = context->createProgramFromPTXFile( "/work/01336/carson/opt/optix/build/lib/ptx/sample6_generated_ambocc.cu.ptx", "closest_hit_radiance" );
    optix::Program any_hit = context->createProgramFromPTXFile( "/work/01336/carson/opt/optix/build/lib/ptx/sample6_generated_ambocc.cu.ptx", "any_hit_occlusion" );
    material->setClosestHitProgram( 0u, closest_hit );
    material->setAnyHitProgram( 1u, any_hit );
    material["occlusion_distance"]->setFloat(30.0f);
    material["sqrt_occlusion_samples"]->setInt(6);
    #endif


    // optix::Program chp = context->createProgramFromPTXFile( ptxPath( "normal_shader.cu" ), "closest_hit_radiance" );
    // const std::string ptx_path = ptxpath("sample6", "ambocc.cu");

    // material->setAnyHitProgram( 1u, any_hit );
    // material->setClosestHitProgram( 0, chp );




    // optix::Material material = context->createMaterial();
    GeometryInstance instance = context->createGeometryInstance( oMesh, &material, &material+1 );

    GLMaterial m = gl_material;
  //TODO: DEBUG: hardcoding mat for debugging
  // m.diffuse = Color(RGB(1.0, 0.713726, .21569));
  //m.diffuse = Color(RGB(0.8, 0.8, 0.8));
  // m.specular = Manta::Color(RGB(.1, .1, .1));
  // m.ambient = Manta::Color(RGB(0, 0, 0));
  // m.shiny = 100;

    instance["Kd"]->setFloat(m.diffuse[0],m.diffuse[1],m.diffuse[2]);
    instance["Ks"]->setFloat(m.specular[0],m.specular[1],m.specular[2]);
    instance["reflectivity"]->setFloat(0,0,0);
    instance["phong_exp"]->setFloat(m.shiny);
    // instance[ "emissive" ]->setFloat( 0.0f, 0.0f, 0.0f );
    // instance[ "phong_exp" ]->setFloat( 32.0f );
    // instance[ "reflectivity" ]->setFloat( 0.3f, 0.3f, 0.3f );
    // instance[ "illum" ]->setInt( 2 );

    instance["ambient_map"]->setTextureSampler( loadTexture( context, "", make_float3( 0.2f, 0.2f, 0.2f ) ) );
    instance["diffuse_map"]->setTextureSampler( loadTexture( context, "", make_float3( 0.8f, 0.8f, 0.8f ) ) );
    instance["specular_map"]->setTextureSampler( loadTexture( context, "", make_float3( 0.0f, 0.0f, 0.0f ) ) );



    // instance->setMaterialCount( 1 );
    // instance->setGeometry(mesh);
    // instance->setMaterial(material);


    optix:GeometryGroup geometrygroup = context->createGeometryGroup();
    geometrygroup->setChildCount( 1 );
    optix::Acceleration acceleration = context->createAcceleration("Sbvh", "Bvh");

    acceleration->setProperty( "vertex_buffer_name", "vertex_buffer" );
    acceleration->setProperty( "index_buffer_name", "vindex_buffer" );
    // acceleration->setProperty( "refine", m_ASRefine );

    geometrygroup->setAcceleration( acceleration );
    acceleration->markDirty();
    geometrygroup->setChild(0, instance);

    oren->gi = new optix::GeometryInstance;
    *(oren->gi) = instance;
    oren->gg = geometrygroup;

    // Create geometry instance
    // optix::GeometryInstance gi = context->createGeometryInstance();
    // gi->setMaterialCount( 1 );
    // gi->setGeometry( sphere );
    // gi->setMaterial( 0, material );
    oren->setBuilt(true);

  }


  void OptiXRenderer::deleteRenderable(Renderable* ren)
  {
    //TODO: DELETE RENDERABLES
    ORenderable* r = dynamic_cast<ORenderable*>(ren);
    // printf("deleting renderable of size: %d\n", er->_data->mesh->vertex_indices.size()/3);
    /*if (er->isBuilt())*/
    /*g_device->rtClear(er->_data->d_mesh);*/
    r->setBuilt(false);
    /*er->_data->mesh->vertexNormals.resize(0);*/
    // delete er->_data->mesh;  //embree handles clearing the data... not sure how to get it to not do that with rtclear yet
  }

  void OptiXRenderer::addTexture(int handle, int target, int level, int internalFormat, int width, int height, int border, int format, int type, void* data)
  {
  }

  void OptiXRenderer::deleteTexture(int handle)
  {

  }

  GeometryGenerator* OptiXRenderer::getGeometryGenerator(int type)
  {
    switch(type)
    {
      case GL_TRIANGLES:
      {
        return _gTriangle;
      }
      case GL_TRIANGLE_STRIP:
      {
        return _gTriangleStrip;
      }
      case GL_QUADS:
      {
        return _gQuads;
      }
      case GL_QUAD_STRIP:
      {
        return _gQuadStrip;
      }
      //case GL_LINES:
      //{
      ////			gen = rm->GLines;
      ////break;
      //}
      //case GL_LINE_STRIP:
      //{
      ////			gen = rm->GLineStrip;
      ////break;
      //}
      //case GL_POLYGON:
      //{
      ////this is temporary for visit, need to support other than quads
      ////break;
      //}
      default:
      {
        return _gVoid;
      }
    }
  }

glr::Renderer* createOptiXRenderer(){ return OptiXRenderer::singleton(); }
