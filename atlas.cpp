#include "atlas.hpp"
#include <stdio.h>

namespace PSX
{

FBAtlas::FBAtlas()
{
   fb_info.resize(NUM_BLOCKS_X * NUM_BLOCKS_Y);
   for (auto &f : fb_info)
      f = STATUS_FB_PREFER;
}

void FBAtlas::read_blit(Domain domain,
      unsigned x, unsigned y, unsigned width, unsigned height)
{
   sync_domain(domain, x, y, width, height);
   read_domain(domain, Stage::Compute, x, y, width, height);
}

void FBAtlas::write_blit(Domain domain,
      unsigned x, unsigned y, unsigned width, unsigned height)
{
   sync_domain(domain, x, y, width, height);
   write_domain(domain, Stage::Compute, x, y, width, height);
}

void FBAtlas::read_texture(unsigned x, unsigned y, unsigned width, unsigned height)
{
   auto domain = find_suitable_domain(x, y, width, height);
   sync_domain(domain, x, y, width, height);
   read_domain(domain, Stage::Compute, x, y, width, height);
}

void FBAtlas::write_domain(Domain domain, Stage stage,
      unsigned x, unsigned y, unsigned width, unsigned height)
{
   unsigned xbegin = x / BLOCK_WIDTH;
   unsigned xend = (x + width - 1) / BLOCK_WIDTH;
   unsigned ybegin = y / BLOCK_HEIGHT;
   unsigned yend = (y + height - 1) / BLOCK_HEIGHT;

   unsigned write_domains = 0;
   unsigned hazard_domains;
   unsigned resolve_domains;
   if (domain == Domain::Unscaled)
   {
      hazard_domains =
         STATUS_BLIT_FB_WRITE |
         STATUS_BLIT_FB_READ;

      if (stage == Stage::Compute)
         resolve_domains = STATUS_BLIT_FB_WRITE;
   }
   else
   {
      hazard_domains =
         STATUS_BLIT_SFB_WRITE |
         STATUS_BLIT_SFB_READ |
         STATUS_FRAGMENT_SFB_WRITE |
         STATUS_SCANOUT;

      // Draw call after draw call isn't really a hazard.
      if (stage == Stage::Fragment)
         hazard_domains &= ~STATUS_FRAGMENT_SFB_WRITE;

      if (stage == Stage::Compute)
         resolve_domains = STATUS_BLIT_SFB_WRITE;
      else if (stage == Stage::Fragment)
         resolve_domains = STATUS_FRAGMENT_SFB_WRITE;
   }

   for (unsigned y = ybegin; y <= yend; y++)
      for (unsigned x = xbegin; x <= xend; x++)
         write_domains |= info(x, y) & hazard_domains;

   if (write_domains)
      pipeline_barrier(write_domains);

   for (unsigned y = ybegin; y <= yend; y++)
      for (unsigned x = xbegin; x <= xend; x++)
         info(x, y) |= resolve_domains;
}

void FBAtlas::read_domain(Domain domain, Stage stage,
      unsigned x, unsigned y, unsigned width, unsigned height)
{
   unsigned xbegin = x / BLOCK_WIDTH;
   unsigned xend = (x + width - 1) / BLOCK_WIDTH;
   unsigned ybegin = y / BLOCK_HEIGHT;
   unsigned yend = (y + height - 1) / BLOCK_HEIGHT;

   unsigned write_domains = 0;
   unsigned hazard_domains;
   unsigned resolve_domains;
   if (domain == Domain::Unscaled)
   {
      hazard_domains = STATUS_BLIT_FB_WRITE;
      if (stage == Stage::Compute)
         resolve_domains = STATUS_BLIT_FB_READ;
      else if (stage == Stage::Fragment)
         resolve_domains = STATUS_FRAGMENT_FB_READ;
   }
   else
   {
      hazard_domains =
         STATUS_BLIT_SFB_WRITE |
         STATUS_FRAGMENT_SFB_WRITE;

      if (stage == Stage::Compute)
         resolve_domains = STATUS_BLIT_SFB_READ;
      else if (stage == Stage::Transfer)
         resolve_domains = STATUS_SCANOUT;
   }

   for (unsigned y = ybegin; y <= yend; y++)
      for (unsigned x = xbegin; x <= xend; x++)
         write_domains |= info(x, y) & hazard_domains;

   if (write_domains)
      pipeline_barrier(write_domains);

   for (unsigned y = ybegin; y <= yend; y++)
      for (unsigned x = xbegin; x <= xend; x++)
         info(x, y) |= resolve_domains;
}

void FBAtlas::sync_domain(Domain domain,
      unsigned x, unsigned y, unsigned width, unsigned height)
{
   unsigned xbegin = x / BLOCK_WIDTH;
   unsigned xend = (x + width - 1) / BLOCK_WIDTH;
   unsigned ybegin = y / BLOCK_HEIGHT;
   unsigned yend = (y + height - 1) / BLOCK_HEIGHT;

   // If we need to see a "clean" version
   // of a framebuffer domain, we need to see
   // anything other than this flag.
   unsigned dirty_bits =
      1u << (domain == Domain::Unscaled ? STATUS_SFB_ONLY : STATUS_FB_ONLY);
   unsigned bits = 0;

   for (unsigned y = ybegin; y <= yend; y++)
      for (unsigned x = xbegin; x <= xend; x++)
         bits |= 1u << (info(x, y) & STATUS_OWNERSHIP_MASK);

   unsigned write_domains = 0;

   // We're asserting that a region is up to date, but it's
   // not, so we have to resolve it.
   if ((bits & dirty_bits) == 0)
      return;

   // For scaled domain,
   // we need to blit from unscaled domain to scaled.
   unsigned ownership;
   unsigned hazard_domains;
   unsigned resolve_domains;
   if (domain == Domain::Scaled)
   {
      ownership = STATUS_FB_ONLY;
      hazard_domains = 
         STATUS_BLIT_FB_WRITE |
         STATUS_BLIT_SFB_WRITE |
         STATUS_BLIT_SFB_READ |
         STATUS_FRAGMENT_SFB_WRITE |
         STATUS_SCANOUT;

      resolve_domains =
         STATUS_BLIT_FB_READ |
         STATUS_FB_PREFER |
         STATUS_BLIT_SFB_WRITE;
   }
   else
   {
      ownership = STATUS_SFB_ONLY;
      hazard_domains = 
         STATUS_BLIT_FB_WRITE |
         STATUS_BLIT_SFB_WRITE |
         STATUS_BLIT_FB_READ;

      resolve_domains =
         STATUS_BLIT_SFB_READ |
         STATUS_SFB_PREFER |
         STATUS_BLIT_FB_WRITE;
   }

   for (unsigned y = ybegin; y <= yend; y++)
   {
      for (unsigned x = xbegin; x <= xend; x++)
      {
         auto &mask = info(x, y);
         // If our block isn't in the ownership class we want,
         // we need to read from one block and write to the other.
         // We might have to wait for writers on read,
         // and add hazard masks for our writes
         // so other readers can wait for us.
         if ((mask & STATUS_OWNERSHIP_MASK) == ownership)
         {
            write_domains |= mask & hazard_domains;
            mask &= ~STATUS_OWNERSHIP_MASK;
            mask |= resolve_domains;
         }
      }
   }

   // If we hit any hazard, resolve it.
   if (write_domains)
      pipeline_barrier(write_domains);
}

Domain FBAtlas::find_suitable_domain(unsigned x, unsigned y,
      unsigned width, unsigned height)
{
   unsigned xbegin = x / BLOCK_WIDTH;
   unsigned xend = (x + width - 1) / BLOCK_WIDTH;
   unsigned ybegin = y / BLOCK_HEIGHT;
   unsigned yend = (y + height - 1) / BLOCK_HEIGHT;

   for (unsigned y = ybegin; y <= yend; y++)
   {
      for (unsigned x = xbegin; x <= xend; x++)
      {
         unsigned i = info(x, y);
         if (i == STATUS_FB_ONLY || i == STATUS_FB_PREFER)
            return Domain::Unscaled;
      }
   }
   return Domain::Scaled;
}

void FBAtlas::write_fragment(unsigned x, unsigned y,
      unsigned width, unsigned height)
{
   (void)x;
   (void)y;
   (void)width;
   (void)height;

   if (!renderpass.inside)
   {
      sync_domain(Domain::Scaled, x, y, width, height);
      write_domain(Domain::Scaled, Stage::Fragment,
            renderpass.x,
            renderpass.y,
            renderpass.width,
            renderpass.height);
      renderpass.inside = true;
      renderpass.clean_clear = false;
   }
}

void FBAtlas::clear_rect(unsigned x, unsigned y,
      unsigned width, unsigned height)
{
   sync_domain(Domain::Scaled, x, y, width, height);

   if (x == renderpass.x &&
       y == renderpass.y &&
       width == renderpass.width &&
       height == renderpass.height)
   {
      renderpass.inside = true;
      renderpass.clean_clear = true;
      discard_render_pass();
      write_domain(Domain::Scaled, Stage::Fragment, x, y, width, height);
   }
   else if (!renderpass.inside)
   {
      // Fill
      write_domain(Domain::Scaled, Stage::Transfer, x, y, width, height);
   }
   else
   {
      // Clear quad
      write_domain(Domain::Scaled, Stage::Fragment, x, y, width, height);
   }
}

void FBAtlas::set_draw_rect(unsigned x, unsigned y,
      unsigned width, unsigned height)
{
   if (!renderpass.inside)
   {
      renderpass.x = x;
      renderpass.y = y;
      renderpass.width = width;
      renderpass.height = height;
   }
   else if (renderpass.x != x ||
            renderpass.y != y ||
            renderpass.width != width ||
            renderpass.height != height)
   {
      flush_render_pass();
      renderpass.inside = false;
   }
}

void FBAtlas::flush_render_pass()
{
}

void FBAtlas::discard_render_pass()
{
}

void FBAtlas::pipeline_barrier(unsigned domains)
{
   unsigned compute_stages =
      STATUS_BLIT_FB_READ |
      STATUS_BLIT_FB_WRITE |
      STATUS_BLIT_SFB_READ |
      STATUS_BLIT_SFB_WRITE;

   unsigned transfer_stages = STATUS_SCANOUT;

   unsigned fragment_stages =
      STATUS_FRAGMENT_FB_READ |
      STATUS_FRAGMENT_SFB_WRITE;

   if (domains & compute_stages)
      fprintf(stderr, "Wait for compute\n");
   if (domains & transfer_stages)
      fprintf(stderr, "Wait for transfer\n");
   if (domains & fragment_stages)
      fprintf(stderr, "Wait for fragment\n");

   for (auto &f : fb_info)
      f &= ~domains;
}

}
