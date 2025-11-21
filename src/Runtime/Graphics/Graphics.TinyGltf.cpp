#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION 
// (Note: STB_IMAGE might collide if included elsewhere with IMPLEMENTATION. 
// If you get link errors, remove STB_IMAGE_IMPLEMENTATION here and ensure it's in RHI.Texture.cpp only)

// TinyGLTF headers
#include <tiny_gltf.h>