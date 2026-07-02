#include "gl.h"

#include <SDL.h>

GLApi gl;

bool GLApi::loadAll() {
  bool ok = true;
#define GL_LOAD_MEMBER(ret, name, args)                                     \
  name = reinterpret_cast<ret(GLCALL*) args>(                               \
      SDL_GL_GetProcAddress("gl" #name));                                   \
  if (!name) {                                                              \
    SDL_Log("Missing OpenGL function: gl%s", #name);                        \
    ok = false;                                                             \
  }
  GL_FUNC_LIST(GL_LOAD_MEMBER)
#undef GL_LOAD_MEMBER
  return ok;
}
