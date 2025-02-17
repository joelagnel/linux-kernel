/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Hantro VPU codec driver
 *
 * Copyright 2018 Google LLC.
 *	Tomasz Figa <tfiga@chromium.org>
 */

#ifndef HANTRO_HW_H_
#define HANTRO_HW_H_

#include <linux/interrupt.h>
#include <linux/v4l2-controls.h>
#include <media/h264-ctrls.h>
#include <media/mpeg2-ctrls.h>
#include <media/vp8-ctrls.h>
#include <media/videobuf2-core.h>

#define DEC_8190_ALIGN_MASK	0x07U

#define HANTRO_VP8_HEADER_SIZE		1280
#define HANTRO_VP8_HW_PARAMS_SIZE	5487
#define HANTRO_VP8_RET_PARAMS_SIZE	488

struct hantro_dev;
struct hantro_ctx;
struct hantro_buf;
struct hantro_variant;

/**
 * struct hantro_aux_buf - auxiliary DMA buffer for hardware data
 * @cpu:	CPU pointer to the buffer.
 * @dma:	DMA address of the buffer.
 * @size:	Size of the buffer.
 */
struct hantro_aux_buf {
	void *cpu;
	dma_addr_t dma;
	size_t size;
};

/**
 * struct hantro_jpeg_enc_hw_ctx
 * @bounce_buffer:	Bounce buffer
 */
struct hantro_jpeg_enc_hw_ctx {
	struct hantro_aux_buf bounce_buffer;
};

/**
 * struct hantro_vp8_enc_buf_data - mode-specific per-buffer data
 * @dct_offset:		Offset inside the buffer to DCT partition.
 * @hdr_size:		Size of header data in the buffer.
 * @ext_hdr_size:	Size of ext header data in the buffer.
 * @dct_size:		Size of DCT partition in the buffer.
 * @header:		Frame header to copy to destination buffer.
 */
struct hantro_vp8_enc_buf_data {
	size_t dct_offset;
	size_t hdr_size;
	size_t ext_hdr_size;
	size_t dct_size;
	u8 header[HANTRO_VP8_HEADER_SIZE];
};

/**
 * struct hantro_vp8_enc_hw_ctx - Context private data specific to codec mode.
 * @ctrl_buf:		VP8 control buffer.
 * @ext_buf:		VP8 ext data buffer.
 * @mv_buf:			VP8 motion vector buffer.
 * @ref_rec_ptr:	Bit flag for swapping ref and rec buffers every frame.
 */
struct hantro_vp8_enc_hw_ctx {
	struct hantro_aux_buf ctrl_buf;
	struct hantro_aux_buf ext_buf;
	struct hantro_aux_buf mv_buf;

	struct hantro_aux_buf priv_src;
	struct hantro_aux_buf priv_dst;

	struct hantro_vp8_enc_buf_data buf_data;

	struct v4l2_rect src_crop;

	u8 ref_rec_ptr:1;
};

/* Max. number of entries in the DPB (HW limitation). */
#define HANTRO_H264_DPB_SIZE		16

/**
 * struct hantro_h264_dec_ctrls
 * @decode:	Decode params
 * @scaling:	Scaling info
 * @slice:	Slice params
 * @sps:	SPS info
 * @pps:	PPS info
 */
struct hantro_h264_dec_ctrls {
	const struct v4l2_ctrl_h264_decode_params *decode;
	const struct v4l2_ctrl_h264_scaling_matrix *scaling;
	const struct v4l2_ctrl_h264_slice_params *slices;
	const struct v4l2_ctrl_h264_sps *sps;
	const struct v4l2_ctrl_h264_pps *pps;
};

/**
 * struct hantro_h264_dec_reflists
 * @p:		P reflist
 * @b0:		B0 reflist
 * @b1:		B1 reflist
 */
struct hantro_h264_dec_reflists {
	u8 p[HANTRO_H264_DPB_SIZE];
	u8 b0[HANTRO_H264_DPB_SIZE];
	u8 b1[HANTRO_H264_DPB_SIZE];
};

/**
 * struct hantro_h264_dec_hw_ctx
 * @priv:	Private auxiliary buffer for hardware.
 * @dpb:	DPB
 * @reflists:	P/B0/B1 reflists
 * @ctrls:	V4L2 controls attached to a run
 * @pic_size:	Size in bytes of decoded picture, this is needed
 *		to pass the location of motion vectors.
 */
struct hantro_h264_dec_hw_ctx {
	struct hantro_aux_buf priv;
	struct v4l2_h264_dpb_entry dpb[HANTRO_H264_DPB_SIZE];
	struct hantro_h264_dec_reflists reflists;
	struct hantro_h264_dec_ctrls ctrls;
	size_t pic_size;
};

/**
 * struct hantro_mpeg2_dec_hw_ctx
 * @qtable:		Quantization table
 */
struct hantro_mpeg2_dec_hw_ctx {
	struct hantro_aux_buf qtable;
};

/**
 * struct hantro_vp8d_hw_ctx
 * @segment_map:	Segment map buffer.
 * @prob_tbl:		Probability table buffer.
 */
struct hantro_vp8_dec_hw_ctx {
	struct hantro_aux_buf segment_map;
	struct hantro_aux_buf prob_tbl;
};

/**
 * struct hantro_codec_ops - codec mode specific operations
 *
 * @init:	If needed, can be used for initialization.
 *		Optional and called from process context.
 * @exit:	If needed, can be used to undo the .init phase.
 *		Optional and called from process context.
 * @run:	Start single {en,de)coding job. Called from atomic context
 *		to indicate that a pair of buffers is ready and the hardware
 *		should be programmed and started.
 * @done:	Read back processing results and additional data from hardware.
 * @reset:	Reset the hardware in case of a timeout.
 */
struct hantro_codec_ops {
	int (*init)(struct hantro_ctx *ctx);
	void (*exit)(struct hantro_ctx *ctx);
	void (*run)(struct hantro_ctx *ctx);
	void (*done)(struct hantro_ctx *ctx, enum vb2_buffer_state);
	void (*reset)(struct hantro_ctx *ctx);
};

/**
 * enum hantro_enc_fmt - source format ID for hardware registers.
 */
enum hantro_enc_fmt {
	RK3288_VPU_ENC_FMT_YUV420P = 0,
	RK3288_VPU_ENC_FMT_YUV420SP = 1,
	RK3288_VPU_ENC_FMT_YUYV422 = 2,
	RK3288_VPU_ENC_FMT_UYVY422 = 3,
};

extern const struct hantro_variant rk3399_vpu_variant;
extern const struct hantro_variant rk3328_vpu_variant;
extern const struct hantro_variant rk3288_vpu_variant;

extern const u32 hantro_vp8_dec_mc_filter[8][6];

void hantro_watchdog(struct work_struct *work);
void hantro_run(struct hantro_ctx *ctx);
void hantro_irq_done(struct hantro_dev *vpu, unsigned int bytesused,
		     enum vb2_buffer_state result);
void hantro_prepare_run(struct hantro_ctx *ctx);
void hantro_finish_run(struct hantro_ctx *ctx);

void hantro_h1_jpeg_enc_run(struct hantro_ctx *ctx);
void rk3399_vpu_jpeg_enc_run(struct hantro_ctx *ctx);
int hantro_jpeg_enc_init(struct hantro_ctx *ctx);
void hantro_jpeg_enc_exit(struct hantro_ctx *ctx);

void hantro_h1_vp8_enc_run(struct hantro_ctx *ctx);
int hantro_vp8_enc_init(struct hantro_ctx *ctx);
void hantro_vp8_enc_done(struct hantro_ctx *ctx, enum vb2_buffer_state result);
void hantro_vp8_enc_assemble_bitstream(struct hantro_ctx *ctx,
				       struct vb2_buffer *vb);
void hantro_vp8_enc_exit(struct hantro_ctx *ctx);

struct vb2_buffer *hantro_h264_get_ref_buf(struct hantro_ctx *ctx,
					   unsigned int dpb_idx);
int hantro_h264_dec_prepare_run(struct hantro_ctx *ctx);
void hantro_g1_h264_dec_run(struct hantro_ctx *ctx);
int hantro_h264_dec_init(struct hantro_ctx *ctx);
void hantro_h264_dec_exit(struct hantro_ctx *ctx);

void hantro_g1_mpeg2_dec_run(struct hantro_ctx *ctx);
void rk3399_vpu_mpeg2_dec_run(struct hantro_ctx *ctx);
void hantro_mpeg2_dec_copy_qtable(u8 *qtable,
	const struct v4l2_ctrl_mpeg2_quantization *ctrl);
int hantro_mpeg2_dec_init(struct hantro_ctx *ctx);
void hantro_mpeg2_dec_exit(struct hantro_ctx *ctx);

void hantro_g1_vp8_dec_run(struct hantro_ctx *ctx);
void rk3399_vpu_vp8_dec_run(struct hantro_ctx *ctx);
int hantro_vp8_dec_init(struct hantro_ctx *ctx);
void hantro_vp8_dec_exit(struct hantro_ctx *ctx);
void hantro_vp8_prob_update(struct hantro_ctx *ctx,
			    const struct v4l2_ctrl_vp8_frame_header *hdr);

int hantro_dummy_enc_init(struct hantro_dev *dev);
void hantro_dummy_enc_release(struct hantro_dev *vpu);

#endif /* HANTRO_HW_H_ */
