#include "atlas.hpp"

using namespace PSX;

int main()
{
   FBAtlas atlas;

   atlas.write_compute(Domain::Unscaled, { 0, 0, 8, 8 });
   atlas.read_compute(Domain::Scaled, { 0, 0, 8, 8 });
   atlas.read_compute(Domain::Unscaled, { 0, 0, 8, 8 });

   atlas.write_compute(Domain::Unscaled, { 8, 8, 8, 8 });
   atlas.write_compute(Domain::Unscaled, { 16, 16, 8, 8 });
   atlas.write_compute(Domain::Scaled, { 32, 16, 8, 8 });

   atlas.set_draw_rect({ 64, 64, 256, 256 });
   atlas.clear_rect({ 64, 64, 256, 256 });
   atlas.set_texture_window({ 400, 400, 8, 8 });
   atlas.write_fragment();
}
