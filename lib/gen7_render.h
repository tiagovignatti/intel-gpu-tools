#ifndef GEN7_RENDER_H
#define GEN7_RENDER_H

#include "gen6_render.h"

#define GEN7_3DSTATE_URB_VS (0x7830 << 16)
#define GEN7_3DSTATE_URB_HS (0x7831 << 16)
#define GEN7_3DSTATE_URB_DS (0x7832 << 16)
#define GEN7_3DSTATE_URB_GS (0x7833 << 16)

#define GEN6_3DSTATE_SCISSOR_STATE_POINTERS	GEN6_3D(3, 0, 0xf)
#define GEN7_3DSTATE_CLEAR_PARAMS		GEN6_3D(3, 0, 0x04)
#define GEN7_3DSTATE_DEPTH_BUFFER		GEN6_3D(3, 0, 0x05)
#define GEN7_3DSTATE_STENCIL_BUFFER		GEN6_3D(3, 0, 0x06)
#define GEN7_3DSTATE_HIER_DEPTH_BUFFER		GEN6_3D(3, 0, 0x07)

#define GEN7_3DSTATE_GS				GEN6_3D(3, 0, 0x11)
#define GEN7_3DSTATE_CONSTANT_GS		GEN6_3D(3, 0, 0x16)
#define GEN7_3DSTATE_CONSTANT_HS		GEN6_3D(3, 0, 0x19)
#define GEN7_3DSTATE_CONSTANT_DS		GEN6_3D(3, 0, 0x1a)
#define GEN7_3DSTATE_HS				GEN6_3D(3, 0, 0x1b)
#define GEN7_3DSTATE_TE				GEN6_3D(3, 0, 0x1c)
#define GEN7_3DSTATE_DS				GEN6_3D(3, 0, 0x1d)
#define GEN7_3DSTATE_STREAMOUT			GEN6_3D(3, 0, 0x1e)
#define GEN7_3DSTATE_SBE			GEN6_3D(3, 0, 0x1f)
#define GEN7_3DSTATE_PS				GEN6_3D(3, 0, 0x20)
#define GEN7_3DSTATE_VIEWPORT_STATE_POINTERS_SF_CLIP	\
						GEN6_3D(3, 0, 0x21)
#define GEN7_3DSTATE_VIEWPORT_STATE_POINTERS_CC	GEN6_3D(3, 0, 0x23)
#define GEN7_3DSTATE_BLEND_STATE_POINTERS	GEN6_3D(3, 0, 0x24)
#define GEN7_3DSTATE_DS_STATE_POINTERS		GEN6_3D(3, 0, 0x25)
#define GEN7_3DSTATE_BINDING_TABLE_POINTERS_VS	GEN6_3D(3, 0, 0x26)
#define GEN7_3DSTATE_BINDING_TABLE_POINTERS_HS	GEN6_3D(3, 0, 0x27)
#define GEN7_3DSTATE_BINDING_TABLE_POINTERS_DS	GEN6_3D(3, 0, 0x28)
#define GEN7_3DSTATE_BINDING_TABLE_POINTERS_GS	GEN6_3D(3, 0, 0x29)
#define GEN7_3DSTATE_BINDING_TABLE_POINTERS_PS	GEN6_3D(3, 0, 0x2a)

#define GEN7_3DSTATE_SAMPLER_STATE_POINTERS_VS	GEN6_3D(3, 0, 0x2b)
#define GEN7_3DSTATE_SAMPLER_STATE_POINTERS_HS	GEN6_3D(3, 0, 0x2c)
#define GEN7_3DSTATE_SAMPLER_STATE_POINTERS_DS	GEN6_3D(3, 0, 0x2d)
#define GEN7_3DSTATE_SAMPLER_STATE_POINTERS_GS	GEN6_3D(3, 0, 0x2e)
#define GEN7_3DSTATE_SAMPLER_STATE_POINTERS_PS	GEN6_3D(3, 0, 0x2f)

#define GEN7_3DSTATE_PUSH_CONSTANT_ALLOC_VS	GEN6_3D(3, 1, 0x12)
#define GEN7_3DSTATE_PUSH_CONSTANT_ALLOC_HS	GEN6_3D(3, 1, 0x13)
#define GEN7_3DSTATE_PUSH_CONSTANT_ALLOC_DS	GEN6_3D(3, 1, 0x14)
#define GEN7_3DSTATE_PUSH_CONSTANT_ALLOC_GS	GEN6_3D(3, 1, 0x15)
#define GEN7_3DSTATE_PUSH_CONSTANT_ALLOC_PS	GEN6_3D(3, 1, 0x16)

/* Some random bits that we care about */
#define GEN7_VB0_BUFFER_ADDR_MOD_EN		(1 << 14)
#define GEN7_WM_DISPATCH_ENABLE			(1 << 29)
#define GEN7_3DSTATE_PS_PERSPECTIVE_PIXEL_BARYCENTRIC (1 << 11)
#define GEN7_3DSTATE_PS_ATTRIBUTE_ENABLED	 (1 << 10)

/* Random shifts */
#define GEN7_3DSTATE_WM_MAX_THREADS_SHIFT 24
#define HSW_3DSTATE_WM_MAX_THREADS_SHIFT 23

/* Shamelessly ripped from mesa */
struct gen7_surface_state
{
	struct {
		uint32_t cube_pos_z:1;
		uint32_t cube_neg_z:1;
		uint32_t cube_pos_y:1;
		uint32_t cube_neg_y:1;
		uint32_t cube_pos_x:1;
		uint32_t cube_neg_x:1;
		uint32_t pad2:2;
		uint32_t render_cache_read_write:1;
		uint32_t pad1:1;
		uint32_t surface_array_spacing:1;
		uint32_t vert_line_stride_ofs:1;
		uint32_t vert_line_stride:1;
		uint32_t tile_walk:1;
		uint32_t tiled_surface:1;
		uint32_t horizontal_alignment:1;
		uint32_t vertical_alignment:2;
		uint32_t surface_format:9;     /**< BRW_SURFACEFORMAT_x */
		uint32_t pad0:1;
		uint32_t is_array:1;
		uint32_t surface_type:3;       /**< BRW_SURFACE_1D/2D/3D/CUBE */
	} ss0;

	struct {
		uint32_t base_addr;
	} ss1;

	struct {
		uint32_t width:14;
		uint32_t pad1:2;
		uint32_t height:14;
		uint32_t pad0:2;
	} ss2;

	struct {
		uint32_t pitch:18;
		uint32_t pad:3;
		uint32_t depth:11;
	} ss3;

	struct {
		uint32_t multisample_position_palette_index:3;
		uint32_t num_multisamples:3;
		uint32_t multisampled_surface_storage_format:1;
		uint32_t render_target_view_extent:11;
		uint32_t min_array_elt:11;
		uint32_t rotation:2;
		uint32_t pad0:1;
	} ss4;

	struct {
		uint32_t mip_count:4;
		uint32_t min_lod:4;
		uint32_t pad1:12;
		uint32_t y_offset:4;
		uint32_t pad0:1;
		uint32_t x_offset:7;
	} ss5;

	struct {
		uint32_t pad; /* Multisample Control Surface stuff */
	} ss6;

	struct {
		uint32_t resource_min_lod:12;

		/* Only on Haswell */
		uint32_t pad0:4;
		uint32_t shader_chanel_select_a:3;
		uint32_t shader_chanel_select_b:3;
		uint32_t shader_chanel_select_g:3;
		uint32_t shader_chanel_select_r:3;

		uint32_t alpha_clear_color:1;
		uint32_t blue_clear_color:1;
		uint32_t green_clear_color:1;
		uint32_t red_clear_color:1;
	} ss7;
};

struct gen7_sampler_state
{
	struct
	{
		uint32_t aniso_algorithm:1;
		uint32_t lod_bias:13;
		uint32_t min_filter:3;
		uint32_t mag_filter:3;
		uint32_t mip_filter:2;
		uint32_t base_level:5;
		uint32_t pad1:1;
		uint32_t lod_preclamp:1;
		uint32_t default_color_mode:1;
		uint32_t pad0:1;
		uint32_t disable:1;
	} ss0;

	struct
	{
		uint32_t cube_control_mode:1;
		uint32_t shadow_function:3;
		uint32_t pad:4;
		uint32_t max_lod:12;
		uint32_t min_lod:12;
	} ss1;

	struct
	{
		uint32_t pad:5;
		uint32_t default_color_pointer:27;
	} ss2;

	struct
	{
		uint32_t r_wrap_mode:3;
		uint32_t t_wrap_mode:3;
		uint32_t s_wrap_mode:3;
		uint32_t pad:1;
		uint32_t non_normalized_coord:1;
		uint32_t trilinear_quality:2;
		uint32_t address_round:6;
		uint32_t max_aniso:3;
		uint32_t chroma_key_mode:1;
		uint32_t chroma_key_index:2;
		uint32_t chroma_key_enable:1;
		uint32_t pad0:6;
	} ss3;
};

struct gen7_sf_clip_viewport {
	struct {
		float m00;
		float m11;
		float m22;
		float m30;
		float m31;
		float m32;
	} viewport;

	uint32_t pad0[2];

	struct {
		float xmin;
		float xmax;
		float ymin;
		float ymax;
	} guardband;

	float pad1[4];
};

struct gen6_scissor_rect
{
	uint32_t xmin:16;
	uint32_t ymin:16;
	uint32_t xmax:16;
	uint32_t ymax:16;
};

#endif
