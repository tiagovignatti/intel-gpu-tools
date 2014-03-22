#ifndef GEN7_MEDIA_H
#define GEN7_MEDIA_H

#include <stdint.h>

#define GEN7_SURFACEFORMAT_R32G32B32A32_FLOAT             0x000
#define GEN7_SURFACEFORMAT_R32G32B32A32_SINT              0x001
#define GEN7_SURFACEFORMAT_R32G32B32A32_UINT              0x002
#define GEN7_SURFACEFORMAT_R32G32B32A32_UNORM             0x003
#define GEN7_SURFACEFORMAT_R32G32B32A32_SNORM             0x004
#define GEN7_SURFACEFORMAT_R64G64_FLOAT                   0x005
#define GEN7_SURFACEFORMAT_R32G32B32X32_FLOAT             0x006
#define GEN7_SURFACEFORMAT_R32G32B32A32_SSCALED           0x007
#define GEN7_SURFACEFORMAT_R32G32B32A32_USCALED           0x008
#define GEN7_SURFACEFORMAT_R32G32B32_FLOAT                0x040
#define GEN7_SURFACEFORMAT_R32G32B32_SINT                 0x041
#define GEN7_SURFACEFORMAT_R32G32B32_UINT                 0x042
#define GEN7_SURFACEFORMAT_R32G32B32_UNORM                0x043
#define GEN7_SURFACEFORMAT_R32G32B32_SNORM                0x044
#define GEN7_SURFACEFORMAT_R32G32B32_SSCALED              0x045
#define GEN7_SURFACEFORMAT_R32G32B32_USCALED              0x046
#define GEN7_SURFACEFORMAT_R16G16B16A16_UNORM             0x080
#define GEN7_SURFACEFORMAT_R16G16B16A16_SNORM             0x081
#define GEN7_SURFACEFORMAT_R16G16B16A16_SINT              0x082
#define GEN7_SURFACEFORMAT_R16G16B16A16_UINT              0x083
#define GEN7_SURFACEFORMAT_R16G16B16A16_FLOAT             0x084
#define GEN7_SURFACEFORMAT_R32G32_FLOAT                   0x085
#define GEN7_SURFACEFORMAT_R32G32_SINT                    0x086
#define GEN7_SURFACEFORMAT_R32G32_UINT                    0x087
#define GEN7_SURFACEFORMAT_R32_FLOAT_X8X24_TYPELESS       0x088
#define GEN7_SURFACEFORMAT_X32_TYPELESS_G8X24_UINT        0x089
#define GEN7_SURFACEFORMAT_L32A32_FLOAT                   0x08A
#define GEN7_SURFACEFORMAT_R32G32_UNORM                   0x08B
#define GEN7_SURFACEFORMAT_R32G32_SNORM                   0x08C
#define GEN7_SURFACEFORMAT_R64_FLOAT                      0x08D
#define GEN7_SURFACEFORMAT_R16G16B16X16_UNORM             0x08E
#define GEN7_SURFACEFORMAT_R16G16B16X16_FLOAT             0x08F
#define GEN7_SURFACEFORMAT_A32X32_FLOAT                   0x090
#define GEN7_SURFACEFORMAT_L32X32_FLOAT                   0x091
#define GEN7_SURFACEFORMAT_I32X32_FLOAT                   0x092
#define GEN7_SURFACEFORMAT_R16G16B16A16_SSCALED           0x093
#define GEN7_SURFACEFORMAT_R16G16B16A16_USCALED           0x094
#define GEN7_SURFACEFORMAT_R32G32_SSCALED                 0x095
#define GEN7_SURFACEFORMAT_R32G32_USCALED                 0x096
#define GEN7_SURFACEFORMAT_B8G8R8A8_UNORM                 0x0C0
#define GEN7_SURFACEFORMAT_B8G8R8A8_UNORM_SRGB            0x0C1
#define GEN7_SURFACEFORMAT_R10G10B10A2_UNORM              0x0C2
#define GEN7_SURFACEFORMAT_R10G10B10A2_UNORM_SRGB         0x0C3
#define GEN7_SURFACEFORMAT_R10G10B10A2_UINT               0x0C4
#define GEN7_SURFACEFORMAT_R10G10B10_SNORM_A2_UNORM       0x0C5
#define GEN7_SURFACEFORMAT_R8G8B8A8_UNORM                 0x0C7
#define GEN7_SURFACEFORMAT_R8G8B8A8_UNORM_SRGB            0x0C8
#define GEN7_SURFACEFORMAT_R8G8B8A8_SNORM                 0x0C9
#define GEN7_SURFACEFORMAT_R8G8B8A8_SINT                  0x0CA
#define GEN7_SURFACEFORMAT_R8G8B8A8_UINT                  0x0CB
#define GEN7_SURFACEFORMAT_R16G16_UNORM                   0x0CC
#define GEN7_SURFACEFORMAT_R16G16_SNORM                   0x0CD
#define GEN7_SURFACEFORMAT_R16G16_SINT                    0x0CE
#define GEN7_SURFACEFORMAT_R16G16_UINT                    0x0CF
#define GEN7_SURFACEFORMAT_R16G16_FLOAT                   0x0D0
#define GEN7_SURFACEFORMAT_B10G10R10A2_UNORM              0x0D1
#define GEN7_SURFACEFORMAT_B10G10R10A2_UNORM_SRGB         0x0D2
#define GEN7_SURFACEFORMAT_R11G11B10_FLOAT                0x0D3
#define GEN7_SURFACEFORMAT_R32_SINT                       0x0D6
#define GEN7_SURFACEFORMAT_R32_UINT                       0x0D7
#define GEN7_SURFACEFORMAT_R32_FLOAT                      0x0D8
#define GEN7_SURFACEFORMAT_R24_UNORM_X8_TYPELESS          0x0D9
#define GEN7_SURFACEFORMAT_X24_TYPELESS_G8_UINT           0x0DA
#define GEN7_SURFACEFORMAT_L16A16_UNORM                   0x0DF
#define GEN7_SURFACEFORMAT_I24X8_UNORM                    0x0E0
#define GEN7_SURFACEFORMAT_L24X8_UNORM                    0x0E1
#define GEN7_SURFACEFORMAT_A24X8_UNORM                    0x0E2
#define GEN7_SURFACEFORMAT_I32_FLOAT                      0x0E3
#define GEN7_SURFACEFORMAT_L32_FLOAT                      0x0E4
#define GEN7_SURFACEFORMAT_A32_FLOAT                      0x0E5
#define GEN7_SURFACEFORMAT_B8G8R8X8_UNORM                 0x0E9
#define GEN7_SURFACEFORMAT_B8G8R8X8_UNORM_SRGB            0x0EA
#define GEN7_SURFACEFORMAT_R8G8B8X8_UNORM                 0x0EB
#define GEN7_SURFACEFORMAT_R8G8B8X8_UNORM_SRGB            0x0EC
#define GEN7_SURFACEFORMAT_R9G9B9E5_SHAREDEXP             0x0ED
#define GEN7_SURFACEFORMAT_B10G10R10X2_UNORM              0x0EE
#define GEN7_SURFACEFORMAT_L16A16_FLOAT                   0x0F0
#define GEN7_SURFACEFORMAT_R32_UNORM                      0x0F1
#define GEN7_SURFACEFORMAT_R32_SNORM                      0x0F2
#define GEN7_SURFACEFORMAT_R10G10B10X2_USCALED            0x0F3
#define GEN7_SURFACEFORMAT_R8G8B8A8_SSCALED               0x0F4
#define GEN7_SURFACEFORMAT_R8G8B8A8_USCALED               0x0F5
#define GEN7_SURFACEFORMAT_R16G16_SSCALED                 0x0F6
#define GEN7_SURFACEFORMAT_R16G16_USCALED                 0x0F7
#define GEN7_SURFACEFORMAT_R32_SSCALED                    0x0F8
#define GEN7_SURFACEFORMAT_R32_USCALED                    0x0F9
#define GEN7_SURFACEFORMAT_B5G6R5_UNORM                   0x100
#define GEN7_SURFACEFORMAT_B5G6R5_UNORM_SRGB              0x101
#define GEN7_SURFACEFORMAT_B5G5R5A1_UNORM                 0x102
#define GEN7_SURFACEFORMAT_B5G5R5A1_UNORM_SRGB            0x103
#define GEN7_SURFACEFORMAT_B4G4R4A4_UNORM                 0x104
#define GEN7_SURFACEFORMAT_B4G4R4A4_UNORM_SRGB            0x105
#define GEN7_SURFACEFORMAT_R8G8_UNORM                     0x106
#define GEN7_SURFACEFORMAT_R8G8_SNORM                     0x107
#define GEN7_SURFACEFORMAT_R8G8_SINT                      0x108
#define GEN7_SURFACEFORMAT_R8G8_UINT                      0x109
#define GEN7_SURFACEFORMAT_R16_UNORM                      0x10A
#define GEN7_SURFACEFORMAT_R16_SNORM                      0x10B
#define GEN7_SURFACEFORMAT_R16_SINT                       0x10C
#define GEN7_SURFACEFORMAT_R16_UINT                       0x10D
#define GEN7_SURFACEFORMAT_R16_FLOAT                      0x10E
#define GEN7_SURFACEFORMAT_I16_UNORM                      0x111
#define GEN7_SURFACEFORMAT_L16_UNORM                      0x112
#define GEN7_SURFACEFORMAT_A16_UNORM                      0x113
#define GEN7_SURFACEFORMAT_L8A8_UNORM                     0x114
#define GEN7_SURFACEFORMAT_I16_FLOAT                      0x115
#define GEN7_SURFACEFORMAT_L16_FLOAT                      0x116
#define GEN7_SURFACEFORMAT_A16_FLOAT                      0x117
#define GEN7_SURFACEFORMAT_R5G5_SNORM_B6_UNORM            0x119
#define GEN7_SURFACEFORMAT_B5G5R5X1_UNORM                 0x11A
#define GEN7_SURFACEFORMAT_B5G5R5X1_UNORM_SRGB            0x11B
#define GEN7_SURFACEFORMAT_R8G8_SSCALED                   0x11C
#define GEN7_SURFACEFORMAT_R8G8_USCALED                   0x11D
#define GEN7_SURFACEFORMAT_R16_SSCALED                    0x11E
#define GEN7_SURFACEFORMAT_R16_USCALED                    0x11F
#define GEN7_SURFACEFORMAT_R8_UNORM                       0x140
#define GEN7_SURFACEFORMAT_R8_SNORM                       0x141
#define GEN7_SURFACEFORMAT_R8_SINT                        0x142
#define GEN7_SURFACEFORMAT_R8_UINT                        0x143
#define GEN7_SURFACEFORMAT_A8_UNORM                       0x144
#define GEN7_SURFACEFORMAT_I8_UNORM                       0x145
#define GEN7_SURFACEFORMAT_L8_UNORM                       0x146
#define GEN7_SURFACEFORMAT_P4A4_UNORM                     0x147
#define GEN7_SURFACEFORMAT_A4P4_UNORM                     0x148
#define GEN7_SURFACEFORMAT_R8_SSCALED                     0x149
#define GEN7_SURFACEFORMAT_R8_USCALED                     0x14A
#define GEN7_SURFACEFORMAT_R1_UINT                        0x181
#define GEN7_SURFACEFORMAT_YCRCB_NORMAL                   0x182
#define GEN7_SURFACEFORMAT_YCRCB_SWAPUVY                  0x183
#define GEN7_SURFACEFORMAT_BC1_UNORM                      0x186
#define GEN7_SURFACEFORMAT_BC2_UNORM                      0x187
#define GEN7_SURFACEFORMAT_BC3_UNORM                      0x188
#define GEN7_SURFACEFORMAT_BC4_UNORM                      0x189
#define GEN7_SURFACEFORMAT_BC5_UNORM                      0x18A
#define GEN7_SURFACEFORMAT_BC1_UNORM_SRGB                 0x18B
#define GEN7_SURFACEFORMAT_BC2_UNORM_SRGB                 0x18C
#define GEN7_SURFACEFORMAT_BC3_UNORM_SRGB                 0x18D
#define GEN7_SURFACEFORMAT_MONO8                          0x18E
#define GEN7_SURFACEFORMAT_YCRCB_SWAPUV                   0x18F
#define GEN7_SURFACEFORMAT_YCRCB_SWAPY                    0x190
#define GEN7_SURFACEFORMAT_DXT1_RGB                       0x191
#define GEN7_SURFACEFORMAT_FXT1                           0x192
#define GEN7_SURFACEFORMAT_R8G8B8_UNORM                   0x193
#define GEN7_SURFACEFORMAT_R8G8B8_SNORM                   0x194
#define GEN7_SURFACEFORMAT_R8G8B8_SSCALED                 0x195
#define GEN7_SURFACEFORMAT_R8G8B8_USCALED                 0x196
#define GEN7_SURFACEFORMAT_R64G64B64A64_FLOAT             0x197
#define GEN7_SURFACEFORMAT_R64G64B64_FLOAT                0x198
#define GEN7_SURFACEFORMAT_BC4_SNORM                      0x199
#define GEN7_SURFACEFORMAT_BC5_SNORM                      0x19A
#define GEN7_SURFACEFORMAT_R16G16B16_UNORM                0x19C
#define GEN7_SURFACEFORMAT_R16G16B16_SNORM                0x19D
#define GEN7_SURFACEFORMAT_R16G16B16_SSCALED              0x19E
#define GEN7_SURFACEFORMAT_R16G16B16_USCALED              0x19F

#define GEN7_SURFACERETURNFORMAT_FLOAT32  	0
#define GEN7_SURFACERETURNFORMAT_S1       	1

#define GEN7_SURFACE_1D				0
#define GEN7_SURFACE_2D				1
#define GEN7_SURFACE_3D				2
#define GEN7_SURFACE_CUBE			3
#define GEN7_SURFACE_BUFFER			4
#define GEN7_SURFACE_NULL			7

#define GEN7_FLOATING_POINT_IEEE_754		0
#define GEN7_FLOATING_POINT_NON_IEEE_754	1

#define GFXPIPE(Pipeline,Opcode,Subopcode) ((3 << 29) |			\
						((Pipeline) << 27) |	\
						((Opcode) << 24) |	\
						((Subopcode) << 16))

#define GEN7_PIPELINE_SELECT			GFXPIPE(1, 1, 4)
# define PIPELINE_SELECT_3D			(0 << 0)
# define PIPELINE_SELECT_MEDIA			(1 << 0)

#define GEN7_STATE_BASE_ADDRESS			GFXPIPE(0, 1, 1)
# define BASE_ADDRESS_MODIFY			(1 << 0)

#define GEN7_MEDIA_VFE_STATE			GFXPIPE(2, 0, 0)
#define GEN7_MEDIA_CURBE_LOAD			GFXPIPE(2, 0, 1)
#define GEN7_MEDIA_INTERFACE_DESCRIPTOR_LOAD	GFXPIPE(2, 0, 2)
#define GEN7_MEDIA_OBJECT			GFXPIPE(2, 1, 0)

struct gen7_interface_descriptor_data
{
	struct {
		uint32_t pad0:6;
		uint32_t kernel_start_pointer:26;
	} desc0;

	struct {
		uint32_t pad0:7;
		uint32_t software_exception_enable:1;
		uint32_t pad1:3;
		uint32_t maskstack_exception_enable:1;
		uint32_t pad2:1;
		uint32_t illegal_opcode_exception_enable:1;
		uint32_t pad3:2;
		uint32_t floating_point_mode:1;
		uint32_t thread_priority:1;
		uint32_t single_program_flow:1;
		uint32_t pad4:13;
	} desc1;

	struct {
		uint32_t pad0:2;
		uint32_t sampler_count:3;
		uint32_t sampler_state_pointer:27;
	} desc2;

	struct {
		uint32_t binding_table_entry_count:5;
		uint32_t binding_table_pointer:27;
	} desc3;

	struct {
		uint32_t constant_urb_entry_read_offset:16;
		uint32_t constant_urb_entry_read_length:16;
	} desc4;

	struct {
		uint32_t num_threads:8;
		uint32_t barrier_return_byte:8;
		uint32_t shared_local_memory_size:5;
		uint32_t barrier_enable:1;
		uint32_t rounding_mode:2;
		uint32_t barrier_return_grf_offset:8;
	} desc5;

	struct {
		uint32_t cross_thread_constant_data_read_length:8;
		uint32_t pad0:24;
	} desc6;

	struct {
		uint32_t pad0;
	} desc7;
};

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
        uint32_t tiled_mode:2;
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

#endif /* GEN7_MEDIA_H */
