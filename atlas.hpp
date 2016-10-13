#pragma once

#include <stdint.h>
#include <vector>

namespace PSX
{
static const unsigned FB_WIDTH = 1024;
static const unsigned FB_HEIGHT = 512;
static const unsigned BLOCK_WIDTH = 8;
static const unsigned BLOCK_HEIGHT = 8;
static const unsigned NUM_BLOCKS_X = FB_WIDTH / BLOCK_WIDTH;
static const unsigned NUM_BLOCKS_Y = FB_HEIGHT / BLOCK_HEIGHT;

enum class Domain : unsigned
{
   Unscaled,
   Scaled
};

enum class Stage : unsigned
{
   Compute,
   Transfer,
   Fragment
};

enum StatusFlag
{
   STATUS_FB_ONLY = 0,
   STATUS_FB_PREFER = 1,
   STATUS_SFB_ONLY = 2,
   STATUS_SFB_PREFER = 3,
   STATUS_OWNERSHIP_MASK = 3,
   STATUS_BLIT_FB_READ = 1 << 2,
   STATUS_BLIT_FB_WRITE = 1 << 3,
   STATUS_BLIT_SFB_READ = 1 << 4,
   STATUS_BLIT_SFB_WRITE = 1 << 5,
   STATUS_FRAGMENT_FB_READ = 1 << 6,
   STATUS_FRAGMENT_SFB_WRITE = 1 << 7,
   STATUS_SCANOUT = 1 << 8
};
using StatusFlags = uint16_t;

class FBAtlas
{
   public:
      FBAtlas();

      void read_blit(Domain domain, unsigned x, unsigned y, unsigned width, unsigned height);
      void write_blit(Domain domain, unsigned x, unsigned y, unsigned width, unsigned height);
      void read_texture(unsigned x, unsigned y, unsigned width, unsigned height);
      void write_fragment(unsigned x, unsigned y, unsigned width, unsigned height);
      void clear_rect(unsigned x, unsigned y, unsigned width, unsigned height);
      void set_draw_rect(unsigned x, unsigned y, unsigned width, unsigned height);

   private:
      std::vector<StatusFlags> fb_info;

      void read_domain(Domain domain, Stage stage,
            unsigned x, unsigned y, unsigned width, unsigned height);
      void write_domain(Domain domain, Stage stage,
            unsigned x, unsigned y, unsigned width, unsigned height);
      void sync_domain(Domain domain,
            unsigned x, unsigned y, unsigned width, unsigned height);
      Domain find_suitable_domain(unsigned x, unsigned y, unsigned width, unsigned height);

      struct
      {
         unsigned x = 0;
         unsigned y = 0;
         unsigned width = 0;
         unsigned height = 0;
         bool inside = false;
         bool clean_clear = false;
         bool wait_for_blit = false;
      } renderpass;

      StatusFlags &info(unsigned block_x, unsigned block_y)
      {
         return fb_info[NUM_BLOCKS_X * block_y + block_x];
      }

      const StatusFlags &info(unsigned block_x, unsigned block_y) const
      {
         return fb_info[NUM_BLOCKS_X * block_y + block_x];
      }

      void pipeline_barrier(unsigned domains);
      void flush_render_pass();
      void discard_render_pass();
};
}
