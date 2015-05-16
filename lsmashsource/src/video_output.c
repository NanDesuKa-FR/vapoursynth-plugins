/*****************************************************************************
 * video_output.c
 *****************************************************************************
 * Copyright (C) 2013-2015 L-SMASH Works project
 *
 * Authors: Yusuke Nakamura <muken.the.vfrmaniac@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *****************************************************************************/

/* This file is available under an ISC license. */

#include <string.h>

/* Libav */
#include <libavcodec/avcodec.h>         /* Decoder */
#include <libswscale/swscale.h>         /* Colorspace converter */
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>

#include "VapourSynth.h"

#include "../common/utils.h"

#include "video_output.h"

static void make_black_background_planar_yuv8
(
    VSFrameRef  *vs_frame,
    const VSAPI *vsapi
)
{
    for( int i = 0; i < 3; i++ )
        memset( vsapi->getWritePtr( vs_frame, i ), i ? 0x80 : 0x00, vsapi->getStride( vs_frame, i ) * vsapi->getFrameHeight( vs_frame, i ) );
}

static void make_black_background_planar_yuv16
(
    VSFrameRef  *vs_frame,
    const VSAPI *vsapi
)
{
    int shift = vsapi->getFrameFormat( vs_frame )->bitsPerSample - 8;
    for( int i = 0; i < 3; i++ )
    {
        int v = i ? 0x00000080 << shift : 0x00000000;
        uint8_t *data = vsapi->getWritePtr( vs_frame, i );
        uint8_t *end  = data + vsapi->getStride( vs_frame, i ) * vsapi->getFrameHeight( vs_frame, i );
        while( data < end )
        {
            /* Assume little endianess. */
            data[0] = v;
            data[1] = v >> 8;
            data += 2;
        }
    }
}

static void make_black_background_planar_rgb
(
    VSFrameRef  *vs_frame,
    const VSAPI *vsapi
)
{
    for( int i = 0; i < 3; i++ )
        memset( vsapi->getWritePtr( vs_frame, i ), 0x00, vsapi->getStride( vs_frame, i ) * vsapi->getFrameHeight( vs_frame, i ) );
}

static void make_frame_planar_yuv
(
    lw_video_scaler_handler_t *vshp,
    AVFrame                   *av_picture,
    const component_reorder_t *component_reorder,
    VSFrameRef                *vs_frame,
    VSFrameContext            *frame_ctx,
    const VSAPI               *vsapi
)
{
    AVPicture vs_picture =
    {
        /* data */
        {
            vsapi->getWritePtr( vs_frame, 0 ),
            vsapi->getWritePtr( vs_frame, 1 ),
            vsapi->getWritePtr( vs_frame, 2 ),
            NULL
        },
        /* linesize */
        {
            vsapi->getStride( vs_frame, 0 ),
            vsapi->getStride( vs_frame, 1 ),
            vsapi->getStride( vs_frame, 2 ),
            0
        }
    };
    sws_scale( vshp->sws_ctx, (const uint8_t* const*)av_picture->data, av_picture->linesize, 0, vshp->input_height, vs_picture.data, vs_picture.linesize );
}

static void make_frame_planar_rgb
(
    lw_video_scaler_handler_t *vshp,
    AVFrame                   *av_picture,
    const component_reorder_t *component_reorder,
    VSFrameRef                *vs_frame,
    VSFrameContext            *frame_ctx,
    const VSAPI               *vsapi
)
{
    AVPicture vs_picture =
    {
        /* data */
        {
            vsapi->getWritePtr( vs_frame, component_reorder[0] ),
            vsapi->getWritePtr( vs_frame, component_reorder[1] ),
            vsapi->getWritePtr( vs_frame, component_reorder[2] ),
            NULL
        },
        /* linesize */
        {
            vsapi->getStride( vs_frame, component_reorder[0] ),
            vsapi->getStride( vs_frame, component_reorder[1] ),
            vsapi->getStride( vs_frame, component_reorder[2] ),
            0
        }

    };
    sws_scale( vshp->sws_ctx, (const uint8_t* const*)av_picture->data, av_picture->linesize, 0, vshp->input_height, vs_picture.data, vs_picture.linesize );
}

static void make_frame_planar_rgb8
(
    lw_video_scaler_handler_t *vshp,
    AVFrame                   *av_picture,
    const component_reorder_t *component_reorder,
    VSFrameRef                *vs_frame,
    VSFrameContext            *frame_ctx,
    const VSAPI               *vsapi
)
{
    uint8_t *vs_frame_data[3] =
        {
            vsapi->getWritePtr( vs_frame, 0 ),
            vsapi->getWritePtr( vs_frame, 1 ),
            vsapi->getWritePtr( vs_frame, 2 )
        };
    const VSFormat *vs_format = vsapi->getFrameFormat( vs_frame );
    int av_num_components = vs_format->numPlanes + (component_reorder[3] == -1 ? 0 : 1);
    int vs_frame_linesize = vsapi->getStride( vs_frame, 0 );
    int vs_pixel_offset   = 0;
    int av_pixel_offset   = 0;
    for( int i = 0; i < vshp->input_height; i++ )
    {
        uint8_t *av_pixel   = av_picture->data[0] + av_pixel_offset;
        uint8_t *av_pixel_r = av_pixel + component_reorder[0];
        uint8_t *av_pixel_g = av_pixel + component_reorder[1];
        uint8_t *av_pixel_b = av_pixel + component_reorder[2];
        uint8_t *vs_pixel_r = vs_frame_data[0] + vs_pixel_offset;
        uint8_t *vs_pixel_g = vs_frame_data[1] + vs_pixel_offset;
        uint8_t *vs_pixel_b = vs_frame_data[2] + vs_pixel_offset;
        for( int j = 0; j < vshp->input_width; j++ )
        {
            *(vs_pixel_r++) = *av_pixel_r;
            *(vs_pixel_g++) = *av_pixel_g;
            *(vs_pixel_b++) = *av_pixel_b;
            av_pixel_r += av_num_components;
            av_pixel_g += av_num_components;
            av_pixel_b += av_num_components;
        }
        av_pixel_offset += av_picture->linesize[0];
        vs_pixel_offset += vs_frame_linesize;
    }
}

static void make_frame_planar_rgb16
(
    lw_video_scaler_handler_t *vshp,
    AVFrame                   *av_picture,
    const component_reorder_t *component_reorder,
    VSFrameRef                *vs_frame,
    VSFrameContext            *frame_ctx,
    const VSAPI               *vsapi
)
{
    uint8_t *vs_frame_data[3] =
        {
            vsapi->getWritePtr( vs_frame, 0 ),
            vsapi->getWritePtr( vs_frame, 1 ),
            vsapi->getWritePtr( vs_frame, 2 )
        };
    const VSFormat *vs_format = vsapi->getFrameFormat( vs_frame );
    int av_num_components = vs_format->numPlanes + (component_reorder[3] == -1 ? 0 : 1);
    int vs_frame_linesize = vsapi->getStride( vs_frame, 0 );
    int vs_pixel_offset   = 0;
    int av_pixel_offset   = 0;
    for( int i = 0; i < vshp->input_height; i++ )
    {
        uint16_t *av_pixel   = (uint16_t *)(av_picture->data[0] + av_pixel_offset);
        uint16_t *av_pixel_r = av_pixel + component_reorder[0];
        uint16_t *av_pixel_g = av_pixel + component_reorder[1];
        uint16_t *av_pixel_b = av_pixel + component_reorder[2];
        uint16_t *vs_pixel_r = (uint16_t *)(vs_frame_data[0] + vs_pixel_offset);
        uint16_t *vs_pixel_g = (uint16_t *)(vs_frame_data[1] + vs_pixel_offset);
        uint16_t *vs_pixel_b = (uint16_t *)(vs_frame_data[2] + vs_pixel_offset);
        for( int j = 0; j < vshp->input_width; j++ )
        {
            *(vs_pixel_r++) = *av_pixel_r;
            *(vs_pixel_g++) = *av_pixel_g;
            *(vs_pixel_b++) = *av_pixel_b;
            av_pixel_r += av_num_components;
            av_pixel_g += av_num_components;
            av_pixel_b += av_num_components;
        }
        av_pixel_offset += av_picture->linesize[0];
        vs_pixel_offset += vs_frame_linesize;
    }
}

VSPresetFormat get_vs_output_pixel_format( const char *format_name )
{
    if( !format_name )
        return pfNone;
    static const struct
    {
        const char     *format_name;
        VSPresetFormat  vs_output_pixel_format;
    } format_table[] =
        {
            { "YUV420P8",  pfYUV420P8  },
            { "YUV422P8",  pfYUV422P8  },
            { "YUV444P8",  pfYUV444P8  },
            { "YUV410P8",  pfYUV410P8  },
            { "YUV411P8",  pfYUV411P8  },
            { "YUV440P8",  pfYUV440P8  },
            { "YUV420P9",  pfYUV420P9  },
            { "YUV422P9",  pfYUV422P9  },
            { "YUV444P9",  pfYUV444P9  },
            { "YUV420P10", pfYUV420P10 },
            { "YUV422P10", pfYUV422P10 },
            { "YUV444P10", pfYUV444P10 },
            { "YUV420P16", pfYUV420P16 },
            { "YUV422P16", pfYUV422P16 },
            { "YUV444P16", pfYUV444P16 },
            { "RGB24",     pfRGB24     },
            { "RGB27",     pfRGB27     },
            { "RGB30",     pfRGB30     },
            { "RGB48",     pfRGB48     },
            { NULL,        pfNone      }
        };
    for( int i = 0; format_table[i].format_name; i++ )
        if( strcasecmp( format_name, format_table[i].format_name ) == 0 )
            return format_table[i].vs_output_pixel_format;
    return pfNone;
}

static enum AVPixelFormat vs_to_av_output_pixel_format( VSPresetFormat vs_output_pixel_format )
{
    static const struct
    {
        VSPresetFormat     vs_output_pixel_format;
        enum AVPixelFormat av_output_pixel_format;
    } format_table[] =
        {
            { pfYUV420P8,  AV_PIX_FMT_YUV420P     },
            { pfYUV422P8,  AV_PIX_FMT_YUV422P     },
            { pfYUV444P8,  AV_PIX_FMT_YUV444P     },
            { pfYUV410P8,  AV_PIX_FMT_YUV410P     },
            { pfYUV411P8,  AV_PIX_FMT_YUV411P     },
            { pfYUV440P8,  AV_PIX_FMT_YUV440P     },
            { pfYUV420P9,  AV_PIX_FMT_YUV420P9LE  },
            { pfYUV422P9,  AV_PIX_FMT_YUV422P9LE  },
            { pfYUV444P9,  AV_PIX_FMT_YUV444P9LE  },
            { pfYUV420P10, AV_PIX_FMT_YUV420P10LE },
            { pfYUV422P10, AV_PIX_FMT_YUV422P10LE },
            { pfYUV444P10, AV_PIX_FMT_YUV444P10LE },
            { pfYUV420P16, AV_PIX_FMT_YUV420P16LE },
            { pfYUV422P16, AV_PIX_FMT_YUV422P16LE },
            { pfYUV444P16, AV_PIX_FMT_YUV444P16LE },
            { pfRGB24,     AV_PIX_FMT_GBRP        },
            { pfRGB27,     AV_PIX_FMT_GBRP9LE     },
            { pfRGB30,     AV_PIX_FMT_GBRP10LE    },
            { pfRGB48,     AV_PIX_FMT_GBRP16LE    },
            { pfNone,      AV_PIX_FMT_NONE        }
        };
    for( int i = 0; format_table[i].vs_output_pixel_format != pfNone; i++ )
        if( vs_output_pixel_format == format_table[i].vs_output_pixel_format )
            return format_table[i].av_output_pixel_format;
    return AV_PIX_FMT_NONE;
}

static const component_reorder_t *get_component_reorder( enum AVPixelFormat av_output_pixel_format )
{
    static const struct
    {
        enum AVPixelFormat  av_output_pixel_format;
        component_reorder_t component_reorder[4];
    } reorder_table[] =
        {
            /* YUV */
            { AV_PIX_FMT_YUV420P,     {  0,  1,  2, -1 } },
            { AV_PIX_FMT_YUV422P,     {  0,  1,  2, -1 } },
            { AV_PIX_FMT_YUV444P,     {  0,  1,  2, -1 } },
            { AV_PIX_FMT_YUV410P,     {  0,  1,  2, -1 } },
            { AV_PIX_FMT_YUV411P,     {  0,  1,  2, -1 } },
            { AV_PIX_FMT_YUV440P,     {  0,  1,  2, -1 } },
            { AV_PIX_FMT_YUV420P9LE,  {  0,  1,  2, -1 } },
            { AV_PIX_FMT_YUV422P9LE,  {  0,  1,  2, -1 } },
            { AV_PIX_FMT_YUV444P9LE,  {  0,  1,  2, -1 } },
            { AV_PIX_FMT_YUV420P10LE, {  0,  1,  2, -1 } },
            { AV_PIX_FMT_YUV422P10LE, {  0,  1,  2, -1 } },
            { AV_PIX_FMT_YUV444P10LE, {  0,  1,  2, -1 } },
            { AV_PIX_FMT_YUV420P16LE, {  0,  1,  2, -1 } },
            { AV_PIX_FMT_YUV422P16LE, {  0,  1,  2, -1 } },
            { AV_PIX_FMT_YUV444P16LE, {  0,  1,  2, -1 } },
            /* RGB */
            { AV_PIX_FMT_GBRP,        {  1,  2,  0, -1 } },
            { AV_PIX_FMT_GBRP9LE,     {  1,  2,  0, -1 } },
            { AV_PIX_FMT_GBRP10LE,    {  1,  2,  0, -1 } },
            { AV_PIX_FMT_GBRP16LE,    {  1,  2,  0, -1 } },
            { AV_PIX_FMT_RGB24,       {  0,  1,  2, -1 } },
            { AV_PIX_FMT_ARGB,        {  1,  2,  3,  0 } },
            { AV_PIX_FMT_RGBA,        {  0,  1,  2,  3 } },
            { AV_PIX_FMT_ABGR,        {  3,  2,  1,  0 } },
            { AV_PIX_FMT_BGRA,        {  2,  1,  0,  3 } },
            { AV_PIX_FMT_BGR48LE,     {  2,  1,  0, -1 } },
            { AV_PIX_FMT_NONE,        {  0,  1,  2,  3 } }
        };
    int i = 0;
    while( reorder_table[i].av_output_pixel_format != AV_PIX_FMT_NONE )
    {
        if( av_output_pixel_format == reorder_table[i].av_output_pixel_format )
            break;
        ++i;
    }
    return reorder_table[i].component_reorder;
}

static inline int set_frame_maker
(
    vs_video_output_handler_t *vs_vohp,
    int                        av_output_is_planar_rgb
)
{
    static const struct
    {
        VSPresetFormat              vs_output_pixel_format;
        int                         av_output_is_planar_rgb;
        func_make_black_background *func_make_black_background;
        func_make_frame            *func_make_frame;
    } frame_maker_table[] =
        {
            { pfYUV420P8,  0, make_black_background_planar_yuv8,  make_frame_planar_yuv   },
            { pfYUV422P8,  0, make_black_background_planar_yuv8,  make_frame_planar_yuv   },
            { pfYUV444P8,  0, make_black_background_planar_yuv8,  make_frame_planar_yuv   },
            { pfYUV410P8,  0, make_black_background_planar_yuv8,  make_frame_planar_yuv   },
            { pfYUV411P8,  0, make_black_background_planar_yuv8,  make_frame_planar_yuv   },
            { pfYUV440P8,  0, make_black_background_planar_yuv8,  make_frame_planar_yuv   },
            { pfYUV420P9,  0, make_black_background_planar_yuv16, make_frame_planar_yuv   },
            { pfYUV422P9,  0, make_black_background_planar_yuv16, make_frame_planar_yuv   },
            { pfYUV444P9,  0, make_black_background_planar_yuv16, make_frame_planar_yuv   },
            { pfYUV420P10, 0, make_black_background_planar_yuv16, make_frame_planar_yuv   },
            { pfYUV422P10, 0, make_black_background_planar_yuv16, make_frame_planar_yuv   },
            { pfYUV444P10, 0, make_black_background_planar_yuv16, make_frame_planar_yuv   },
            { pfYUV420P16, 0, make_black_background_planar_yuv16, make_frame_planar_yuv   },
            { pfYUV422P16, 0, make_black_background_planar_yuv16, make_frame_planar_yuv   },
            { pfYUV444P16, 0, make_black_background_planar_yuv16, make_frame_planar_yuv   },
            { pfRGB24,     1, make_black_background_planar_rgb,   make_frame_planar_rgb   },
            { pfRGB27,     1, make_black_background_planar_rgb,   make_frame_planar_rgb   },
            { pfRGB30,     1, make_black_background_planar_rgb,   make_frame_planar_rgb   },
            { pfRGB48,     1, make_black_background_planar_rgb,   make_frame_planar_rgb   },
            { pfRGB24,     0, make_black_background_planar_rgb,   make_frame_planar_rgb8  },
            { pfRGB48,     0, make_black_background_planar_rgb,   make_frame_planar_rgb16 },
            { pfNone,      0, NULL,                               NULL                    }
        };
    for( int i = 0; frame_maker_table[i].vs_output_pixel_format != pfNone; i++ )
        if( vs_vohp->vs_output_pixel_format == frame_maker_table[i].vs_output_pixel_format
         && av_output_is_planar_rgb         == frame_maker_table[i].av_output_is_planar_rgb )
        {
            vs_vohp->make_black_background = frame_maker_table[i].func_make_black_background;
            vs_vohp->make_frame            = frame_maker_table[i].func_make_frame;
            return 0;
        }
    vs_vohp->make_black_background = NULL;
    vs_vohp->make_frame            = NULL;
    return -1;
}

int determine_colorspace_conversion
(
    lw_video_output_handler_t *vohp,
    enum AVPixelFormat         input_pixel_format
)
{
    avoid_yuv_scale_conversion( &input_pixel_format );
    static const struct
    {
        enum AVPixelFormat  av_input_pixel_format;
        VSPresetFormat      vs_output_pixel_format;
        int                 enable_scaler;
    } conversion_table[] =
        {
            { AV_PIX_FMT_YUV420P,     pfYUV420P8,  0 },
            { AV_PIX_FMT_NV12,        pfYUV420P8,  1 },
            { AV_PIX_FMT_NV21,        pfYUV420P8,  1 },
            { AV_PIX_FMT_YUV422P,     pfYUV422P8,  0 },
            { AV_PIX_FMT_UYVY422,     pfYUV422P8,  1 },
            { AV_PIX_FMT_YUYV422,     pfYUV422P8,  1 },
            { AV_PIX_FMT_YUV444P,     pfYUV444P8,  0 },
            { AV_PIX_FMT_YUV410P,     pfYUV410P8,  0 },
            { AV_PIX_FMT_YUV411P,     pfYUV411P8,  0 },
            { AV_PIX_FMT_UYYVYY411,   pfYUV411P8,  1 },
            { AV_PIX_FMT_YUV440P,     pfYUV440P8,  0 },
            { AV_PIX_FMT_YUV420P9LE,  pfYUV420P9,  0 },
            { AV_PIX_FMT_YUV420P9BE,  pfYUV420P9,  1 },
            { AV_PIX_FMT_YUV422P9LE,  pfYUV422P9,  0 },
            { AV_PIX_FMT_YUV422P9BE,  pfYUV422P9,  1 },
            { AV_PIX_FMT_YUV444P9LE,  pfYUV444P9,  0 },
            { AV_PIX_FMT_YUV444P9BE,  pfYUV444P9,  1 },
            { AV_PIX_FMT_YUV420P10LE, pfYUV420P10, 0 },
            { AV_PIX_FMT_YUV420P10BE, pfYUV420P10, 1 },
            { AV_PIX_FMT_YUV422P10LE, pfYUV422P10, 0 },
            { AV_PIX_FMT_YUV422P10BE, pfYUV422P10, 1 },
            { AV_PIX_FMT_YUV444P10LE, pfYUV444P10, 0 },
            { AV_PIX_FMT_YUV444P10BE, pfYUV444P10, 1 },
            { AV_PIX_FMT_YUV420P16LE, pfYUV420P16, 0 },
            { AV_PIX_FMT_YUV420P16BE, pfYUV420P16, 1 },
            { AV_PIX_FMT_YUV422P16LE, pfYUV422P16, 0 },
            { AV_PIX_FMT_YUV422P16BE, pfYUV422P16, 1 },
            { AV_PIX_FMT_YUV444P16LE, pfYUV444P16, 0 },
            { AV_PIX_FMT_YUV444P16BE, pfYUV444P16, 1 },
            { AV_PIX_FMT_GBRP,        pfRGB24,     0 },
            { AV_PIX_FMT_GBRP9LE,     pfRGB48,     0 },
            { AV_PIX_FMT_GBRP9BE,     pfRGB48,     1 },
            { AV_PIX_FMT_GBRP10LE,    pfRGB48,     0 },
            { AV_PIX_FMT_GBRP10BE,    pfRGB48,     1 },
            { AV_PIX_FMT_GBRP16LE,    pfRGB48,     0 },
            { AV_PIX_FMT_GBRP16BE,    pfRGB48,     1 },
            { AV_PIX_FMT_BGR24,       pfRGB24,     0 },
            { AV_PIX_FMT_RGB24,       pfRGB24,     0 },
            { AV_PIX_FMT_ARGB,        pfRGB24,     0 },
            { AV_PIX_FMT_RGBA,        pfRGB24,     0 },
            { AV_PIX_FMT_ABGR,        pfRGB24,     0 },
            { AV_PIX_FMT_BGRA,        pfRGB24,     0 },
            { AV_PIX_FMT_BGR48LE,     pfRGB48,     0 },
            { AV_PIX_FMT_BGR48BE,     pfRGB48,     1 },
            { AV_PIX_FMT_NONE,        pfNone,      1 }
        };
    vs_video_output_handler_t *vs_vohp = (vs_video_output_handler_t *)vohp->private_handler;
    if( vs_vohp->variable_info || vs_vohp->vs_output_pixel_format == pfNone )
    {
        /* Determine by input pixel format. */
        for( int i = 0; conversion_table[i].vs_output_pixel_format != pfNone; i++ )
            if( input_pixel_format == conversion_table[i].av_input_pixel_format )
            {
                vs_vohp->vs_output_pixel_format = conversion_table[i].vs_output_pixel_format;
                vohp->scaler.enabled            = conversion_table[i].enable_scaler;
                break;
            }
    }
    else
    {
        /* Determine by both input pixel format and output pixel format. */
        int i = 0;
        while( conversion_table[i].vs_output_pixel_format != pfNone )
        {
            if( input_pixel_format              == conversion_table[i].av_input_pixel_format
             && vs_vohp->vs_output_pixel_format == conversion_table[i].vs_output_pixel_format )
            {
                vohp->scaler.enabled = conversion_table[i].enable_scaler;
                break;
            }
            ++i;
        }
        if( conversion_table[i].vs_output_pixel_format == pfNone )
            vohp->scaler.enabled = 1;
    }
    vohp->scaler.output_pixel_format = vohp->scaler.enabled
                                     ? vs_to_av_output_pixel_format( vs_vohp->vs_output_pixel_format )
                                     : input_pixel_format;
    vs_vohp->component_reorder = get_component_reorder( vohp->scaler.output_pixel_format );
    int av_output_flags = av_pix_fmt_desc_get( vohp->scaler.output_pixel_format )->flags;
    return set_frame_maker( vs_vohp, (av_output_flags & AV_PIX_FMT_FLAG_PLANAR) && (av_output_flags & AV_PIX_FMT_FLAG_RGB) );
}

VSFrameRef *new_output_video_frame
(
    lw_video_output_handler_t *vohp,
    int                        width,
    int                        height,
    enum AVPixelFormat         pixel_format,
    VSFrameContext            *frame_ctx,
    VSCore                    *core,
    const VSAPI               *vsapi
)
{
    vs_video_output_handler_t *vs_vohp = (vs_video_output_handler_t *)vohp->private_handler;
    VSFrameRef                *vs_frame;
    if( vs_vohp->variable_info )
    {
        if( determine_colorspace_conversion( vohp, pixel_format ) )
        {
            if( frame_ctx )
                vsapi->setFilterError( "lsmas: failed to determin colorspace conversion.", frame_ctx );
            return NULL;
        }
        const VSFormat *vs_format = vsapi->getFormatPreset( vs_vohp->vs_output_pixel_format, core );
        vs_frame = vsapi->newVideoFrame( vs_format, width, height, NULL, core );
    }
    else
    {
        if( pixel_format != vohp->scaler.input_pixel_format
         && determine_colorspace_conversion( vohp, pixel_format ) )
        {
            if( frame_ctx )
                vsapi->setFilterError( "lsmas: failed to determin colorspace conversion.", frame_ctx );
            return NULL;
        }
        vs_frame = vsapi->copyFrame( vs_vohp->background_frame, core );
    }
    return vs_frame;
}

typedef struct
{
    VSFrameRef  *vs_frame_buffer;
    const VSAPI *vsapi;
} vs_video_buffer_handler_t;

VSFrameRef *make_frame
(
    lw_video_output_handler_t *vohp,
    AVCodecContext            *ctx,
    AVFrame                   *av_frame
)
{
    vs_video_output_handler_t *vs_vohp = (vs_video_output_handler_t *)vohp->private_handler;
    VSFrameContext *frame_ctx = vs_vohp->frame_ctx;
    VSCore         *core      = vs_vohp->core;
    const VSAPI    *vsapi     = vs_vohp->vsapi;
    if( vs_vohp->direct_rendering && !vohp->scaler.enabled && av_frame->opaque )
    {
        /* Render from the decoder directly. */
        vs_video_buffer_handler_t *vs_vbhp = (vs_video_buffer_handler_t *)av_frame->opaque;
        return vs_vbhp ? (VSFrameRef *)vs_vbhp->vsapi->cloneFrameRef( vs_vbhp->vs_frame_buffer ) : NULL;
    }
    if( !vs_vohp->make_frame )
        return NULL;
    /* Convert pixel format if needed. We don't change the presentation resolution. */
    enum AVPixelFormat *input_pixel_format = (enum AVPixelFormat *)&av_frame->format;
    int yuv_range = avoid_yuv_scale_conversion( input_pixel_format );
    if( ctx->color_range == AVCOL_RANGE_MPEG || ctx->color_range == AVCOL_RANGE_JPEG )
        yuv_range = (ctx->color_range == AVCOL_RANGE_JPEG);
    lw_video_scaler_handler_t *vshp = &vohp->scaler;
    if( !vshp->sws_ctx
     || vshp->input_width        != ctx->width
     || vshp->input_height       != ctx->height
     || vshp->input_pixel_format != *input_pixel_format
     || vshp->input_colorspace   != ctx->colorspace
     || vshp->input_yuv_range    != yuv_range )
    {
        /* Update scaler. */
        vshp->sws_ctx = update_scaler_configuration( vshp->sws_ctx, vshp->flags,
                                                     ctx->width, ctx->height,
                                                     *input_pixel_format, vshp->output_pixel_format,
                                                     ctx->colorspace, yuv_range );
        if( !vshp->sws_ctx )
        {
            if( frame_ctx )
                vsapi->setFilterError( "lsmas: failed to update scaler settings.", frame_ctx );
            return NULL;
        }
        vshp->input_width        = ctx->width;
        vshp->input_height       = ctx->height;
        vshp->input_pixel_format = *input_pixel_format;
        vshp->input_colorspace   = ctx->colorspace;
        vshp->input_yuv_range    = yuv_range;
    }
    /* Make video frame. */
    VSFrameRef *vs_frame = new_output_video_frame( vohp, vshp->input_width, vshp->input_height, *input_pixel_format, frame_ctx, core, vsapi );
    if( vs_frame )
        vs_vohp->make_frame( vshp, av_frame, vs_vohp->component_reorder, vs_frame, frame_ctx, vsapi );
    else if( frame_ctx )
        vsapi->setFilterError( "lsmas: failed to allocate a output video frame.", frame_ctx );
    return vs_frame;
}

static int vs_check_dr_available
(
    AVCodecContext    *ctx,
    enum AVPixelFormat pixel_format
)
{
    if( !(ctx->codec->capabilities & CODEC_CAP_DR1) )
        return 0;
    static enum AVPixelFormat dr_support_pix_fmt[] =
        {
            AV_PIX_FMT_YUV420P,
            AV_PIX_FMT_YUV422P,
            AV_PIX_FMT_YUV444P,
            AV_PIX_FMT_YUV410P,
            AV_PIX_FMT_YUV411P,
            AV_PIX_FMT_YUV440P,
            AV_PIX_FMT_YUV420P9LE,
            AV_PIX_FMT_YUV422P9LE,
            AV_PIX_FMT_YUV444P9LE,
            AV_PIX_FMT_YUV420P10LE,
            AV_PIX_FMT_YUV422P10LE,
            AV_PIX_FMT_YUV444P10LE,
            AV_PIX_FMT_YUV420P16LE,
            AV_PIX_FMT_YUV422P16LE,
            AV_PIX_FMT_YUV444P16LE,
            AV_PIX_FMT_GBRP,
            AV_PIX_FMT_GBRP9LE,
            AV_PIX_FMT_GBRP10LE,
            AV_PIX_FMT_GBRP16LE,
            AV_PIX_FMT_NONE
        };
    for( int i = 0; dr_support_pix_fmt[i] != AV_PIX_FMT_NONE; i++ )
        if( dr_support_pix_fmt[i] == pixel_format )
            return 1;
    return 0;
}

static void vs_video_release_buffer_handler
(
    void    *opaque,
    uint8_t *data
)
{
    vs_video_buffer_handler_t *vs_vbhp = (vs_video_buffer_handler_t *)opaque;
    if( !vs_vbhp )
        return;
    if( vs_vbhp->vsapi && vs_vbhp->vsapi->freeFrame )
        vs_vbhp->vsapi->freeFrame( vs_vbhp->vs_frame_buffer );
    free( vs_vbhp );
}

static void vs_video_unref_buffer_handler
(
    void    *opaque,
    uint8_t *data
)
{
    /* Decrement the reference-counter to the video buffer handler by 1.
     * Delete it by vs_video_release_buffer_handler() if there are no reference to it i.e. the reference-counter equals zero. */
    AVBufferRef *vs_buffer_ref = (AVBufferRef *)opaque;
    av_buffer_unref( &vs_buffer_ref );
}

static inline int vs_create_plane_buffer
(
    vs_video_buffer_handler_t *vs_vbhp,
    AVBufferRef               *vs_buffer_handler,
    AVFrame                   *av_frame,
    int                        av_plane,
    int                        vs_plane
)
{
    AVBufferRef *vs_buffer_ref = av_buffer_ref( vs_buffer_handler );
    if( !vs_buffer_ref )
    {
        av_buffer_unref( &vs_buffer_handler );
        return -1;
    }
    av_frame->linesize[av_plane] = vs_vbhp->vsapi->getStride( vs_vbhp->vs_frame_buffer, vs_plane );
    int vs_plane_size = vs_vbhp->vsapi->getFrameHeight( vs_vbhp->vs_frame_buffer, vs_plane )
                      * av_frame->linesize[av_plane];
    av_frame->buf[av_plane] = av_buffer_create( vs_vbhp->vsapi->getWritePtr( vs_vbhp->vs_frame_buffer, vs_plane ),
                                                vs_plane_size,
                                                vs_video_unref_buffer_handler,
                                                vs_buffer_ref,
                                                0 );
    if( !av_frame->buf[av_plane] )
        return -1;
    av_frame->data[av_plane] = av_frame->buf[av_plane]->data;
    return 0;
}

static int vs_video_get_buffer
(
    AVCodecContext *ctx,
    AVFrame        *av_frame,
    int             flags
)
{
    av_frame->opaque = NULL;
    lw_video_output_handler_t *lw_vohp = (lw_video_output_handler_t *)ctx->opaque;
    vs_video_output_handler_t *vs_vohp = (vs_video_output_handler_t *)lw_vohp->private_handler;
    enum AVPixelFormat pix_fmt = ctx->pix_fmt;
    avoid_yuv_scale_conversion( &pix_fmt );
    if( (!vs_vohp->variable_info && lw_vohp->scaler.input_pixel_format != pix_fmt)
     || !vs_check_dr_available( ctx, pix_fmt ) )
    {
        lw_vohp->scaler.enabled = 1;
        return avcodec_default_get_buffer2( ctx, av_frame, 0 );
    }
    else
        lw_vohp->scaler.enabled = 0;
    /* New VapourSynth video frame buffer. */
    vs_video_buffer_handler_t *vs_vbhp = malloc( sizeof(vs_video_buffer_handler_t) );
    if( !vs_vbhp )
    {
        av_frame_unref( av_frame );
        return AVERROR( ENOMEM );
    }
    av_frame->opaque = vs_vbhp;
    av_frame->width  = ctx->width;
    av_frame->height = ctx->height;
    av_frame->format = ctx->pix_fmt;
    avcodec_align_dimensions2( ctx, &av_frame->width, &av_frame->height, av_frame->linesize );
    VSFrameRef *vs_frame_buffer = new_output_video_frame( lw_vohp, av_frame->width, av_frame->height, pix_fmt,
                                                          vs_vohp->frame_ctx, vs_vohp->core, vs_vohp->vsapi );
    if( !vs_frame_buffer )
    {
        free( vs_vbhp );
        av_frame_unref( av_frame );
        return AVERROR( ENOMEM );
    }
    vs_vbhp->vs_frame_buffer = vs_frame_buffer;
    vs_vbhp->vsapi           = vs_vohp->vsapi;
    /* Create frame buffers for the decoder.
     * The callback vs_video_release_buffer_handler() shall be called when no reference to the video buffer handler is present.
     * The callback vs_video_unref_buffer_handler() decrements the reference-counter by 1. */
    memset( av_frame->buf,      0, sizeof(av_frame->buf) );
    memset( av_frame->data,     0, sizeof(av_frame->data) );
    memset( av_frame->linesize, 0, sizeof(av_frame->linesize) );
    AVBufferRef *vs_buffer_handler = av_buffer_create( NULL, 0, vs_video_release_buffer_handler, vs_vbhp, 0 );
    if( !vs_buffer_handler )
    {
        vs_video_release_buffer_handler( vs_vbhp, NULL );
        av_frame_unref( av_frame );
        return AVERROR( ENOMEM );
    }
    vs_vohp->component_reorder = get_component_reorder( pix_fmt );
    for( int i = 0; i < 3; i++ )
        if( vs_create_plane_buffer( vs_vbhp, vs_buffer_handler, av_frame, i, vs_vohp->component_reorder[i] ) < 0 )
            goto fail;
    /* Here, a variable 'vs_buffer_handler' itself is not referenced by any pointer. */
    av_buffer_unref( &vs_buffer_handler );
    av_frame->nb_extended_buf = 0;
    av_frame->extended_data   = av_frame->data;
    return 0;
fail:
    av_frame_unref( av_frame );
    av_buffer_unref( &vs_buffer_handler );
    return AVERROR( ENOMEM );
}

func_get_buffer_t *setup_video_rendering
(
    lw_video_output_handler_t *lw_vohp,
    AVCodecContext            *ctx,
    VSVideoInfo               *vi,
    int                        width,
    int                        height
)
{
    vs_video_output_handler_t *vs_vohp = (vs_video_output_handler_t *)lw_vohp->private_handler;
    vs_vohp->direct_rendering &= vs_check_dr_available( ctx, ctx->pix_fmt );
    if( vs_vohp->variable_info )
    {
        vi->format = NULL;
        vi->width  = 0;
        vi->height = 0;
    }
    else
    {
        const VSAPI *vsapi = vs_vohp->vsapi;
        vi->format = vsapi->getFormatPreset( vs_vohp->vs_output_pixel_format, vs_vohp->core );
        vi->width  = width;
        vi->height = height;
        if( vs_vohp->direct_rendering )
        {
            /* Align output width and height for direct rendering. */
            int linesize_align[AV_NUM_DATA_POINTERS];
            enum AVPixelFormat input_pixel_format = ctx->pix_fmt;
            ctx->pix_fmt = lw_vohp->scaler.output_pixel_format;
            avcodec_align_dimensions2( ctx, &vi->width, &vi->height, linesize_align );
            ctx->pix_fmt = input_pixel_format;
        }
        vs_vohp->background_frame = vsapi->newVideoFrame( vi->format, vi->width, vi->height, NULL, vs_vohp->core );
        if( !vs_vohp->background_frame )
            return NULL;
        vs_vohp->make_black_background( vs_vohp->background_frame, vsapi );
    }
    lw_vohp->output_width  = vi->width;
    lw_vohp->output_height = vi->height;
    /* Set up custom get_buffer() for direct rendering if available. */
    if( vs_vohp->direct_rendering )
    {
        ctx->get_buffer2 = vs_video_get_buffer;
        ctx->opaque      = lw_vohp;
        ctx->flags      |= CODEC_FLAG_EMU_EDGE;
    }
    return ctx->get_buffer2;
}

static void vs_free_video_output_handler
(
    void *private_handler
)
{
    vs_video_output_handler_t *vs_vohp = (vs_video_output_handler_t *)private_handler;
    if( !vs_vohp )
        return;
    if( vs_vohp->vsapi && vs_vohp->vsapi->freeFrame && vs_vohp->background_frame )
        vs_vohp->vsapi->freeFrame( vs_vohp->background_frame );
    free( vs_vohp );
}

vs_video_output_handler_t *vs_allocate_video_output_handler
(
    lw_video_output_handler_t *vohp
)
{
    vs_video_output_handler_t *vs_vohp = lw_malloc_zero( sizeof(vs_video_output_handler_t) );
    if( !vs_vohp )
        return NULL;
    vohp->private_handler      = vs_vohp;
    vohp->free_private_handler = vs_free_video_output_handler;
    return vs_vohp;
}
