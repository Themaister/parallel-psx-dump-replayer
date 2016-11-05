#pragma once

namespace PSX
{
static const uint32_t quad_vert[] =
#include "quad.vert.inc"
;
static const uint32_t scaled_quad_frag[] =
#include "scaled.quad.frag.inc"
;
static const uint32_t unscaled_quad_frag[] =
#include "unscaled.quad.frag.inc"
;
static const uint32_t copy_vram_comp[] =
#include "copy_vram.comp.inc"
;
static const uint32_t copy_vram_masked_comp[] =
#include "copy_vram.masked.comp.inc"
;
static const uint32_t resolve_to_scaled[] =
#include "resolve.scaled.comp.inc"
;
static const uint32_t resolve_to_unscaled_2[] =
#include "resolve.unscaled.2.comp.inc"
;
static const uint32_t resolve_to_unscaled_4[] =
#include "resolve.unscaled.4.comp.inc"
;
static const uint32_t resolve_to_unscaled_8[] =
#include "resolve.unscaled.8.comp.inc"
;
static const uint32_t opaque_flat_vert[] =
#include "opaque.flat.vert.inc"
;
static const uint32_t opaque_flat_frag[] =
#include "opaque.flat.frag.inc"
;
static const uint32_t opaque_textured_vert[] =
#include "opaque.textured.vert.inc"
;
static const uint32_t opaque_textured_frag[] =
#include "opaque.textured.frag.inc"
;
static const uint32_t opaque_semitrans_frag[] =
#include "semitrans.opaque.textured.frag.inc"
;
static const uint32_t semitrans_frag[] =
#include "semitrans.trans.textured.frag.inc"
;
static const uint32_t blit_vram_unscaled_comp[] =
#include "blit_vram.unscaled.comp.inc"
;
static const uint32_t blit_vram_scaled_comp[] =
#include "blit_vram.scaled.comp.inc"
;
static const uint32_t blit_vram_unscaled_masked_comp[] =
#include "blit_vram.masked.unscaled.comp.inc"
;
static const uint32_t blit_vram_scaled_masked_comp[] =
#include "blit_vram.masked.scaled.comp.inc"
;
static const uint32_t feedback_add_frag[] =
#include "feedback.add.frag.inc"
;
static const uint32_t feedback_avg_frag[] =
#include "feedback.avg.frag.inc"
;
static const uint32_t feedback_sub_frag[] =
#include "feedback.sub.frag.inc"
;
static const uint32_t feedback_add_quarter_frag[] =
#include "feedback.add_quarter.frag.inc"
;
}
