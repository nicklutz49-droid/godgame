#pragma once

#include <glm/glm.hpp>

#include "Mesh.h"

// A tiny built-in 5x7 pixel font (M7): text becomes flat quads (one per lit
// pixel) appended to a MeshData, drawn by the existing lit shader with
// uEmissive = 1 and fog off through a pixel-space ortho projection. No
// textures, no new shaders, no GL here - the geometry is headless-testable.
namespace font {

inline constexpr float kGlyphW = 5.0f;
inline constexpr float kGlyphH = 7.0f;
inline constexpr float kAdvance = 6.0f;  // glyph plus one pixel of air

// Append `text` starting at pixel (x, y) (top-left origin, +y down).
// Unknown characters render as blanks. Returns the pen x after the text.
float addText(MeshData& md, const char* text, float x, float y, float scale,
              const glm::vec3& color);

// Pixel width of `text` at `scale` (for centering).
float textWidth(const char* text, float scale);

}  // namespace font
