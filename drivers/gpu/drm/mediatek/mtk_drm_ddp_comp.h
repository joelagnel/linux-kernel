/*
 * Copyright (c) 2015 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef MTK_DRM_DDP_COMP_H
#define MTK_DRM_DDP_COMP_H

#include <linux/io.h>

struct device;
struct device_node;
struct drm_crtc;
struct drm_device;
struct mtk_plane_state;
struct drm_crtc_state;

enum mtk_ddp_comp_type {
	MTK_DISP_OVL,
	MTK_DISP_OVL_2L,
	MTK_DISP_RDMA,
	MTK_DISP_WDMA,
	MTK_DISP_COLOR,
	MTK_DISP_CCORR,
	MTK_DISP_DITHER,
	MTK_DISP_AAL,
	MTK_DISP_GAMMA,
	MTK_DISP_UFOE,
	MTK_DSI,
	MTK_DPI,
	MTK_DISP_PWM,
	MTK_DISP_MUTEX,
	MTK_DISP_OD,
	MTK_DISP_BLS,
	MTK_DDP_COMP_TYPE_MAX,
};

enum mtk_ddp_comp_id {
	DDP_COMPONENT_AAL0,
	DDP_COMPONENT_AAL1,
	DDP_COMPONENT_BLS,
	DDP_COMPONENT_CCORR,
	DDP_COMPONENT_COLOR0,
	DDP_COMPONENT_COLOR1,
	DDP_COMPONENT_DITHER,
	DDP_COMPONENT_DPI0,
	DDP_COMPONENT_DPI1,
	DDP_COMPONENT_DSI0,
	DDP_COMPONENT_DSI1,
	DDP_COMPONENT_DSI2,
	DDP_COMPONENT_DSI3,
	DDP_COMPONENT_GAMMA,
	DDP_COMPONENT_OD0,
	DDP_COMPONENT_OD1,
	DDP_COMPONENT_OVL0,
	DDP_COMPONENT_OVL_2L0,
	DDP_COMPONENT_OVL_2L1,
	DDP_COMPONENT_OVL1,
	DDP_COMPONENT_PWM0,
	DDP_COMPONENT_PWM1,
	DDP_COMPONENT_PWM2,
	DDP_COMPONENT_RDMA0,
	DDP_COMPONENT_RDMA1,
	DDP_COMPONENT_RDMA2,
	DDP_COMPONENT_UFOE,
	DDP_COMPONENT_WDMA0,
	DDP_COMPONENT_WDMA1,
	DDP_COMPONENT_ID_MAX,
};

struct mtk_ddp_comp;
struct cmdq_pkt;
struct mtk_ddp_comp_funcs {
	void (*config)(struct mtk_ddp_comp *comp, unsigned int w,
		       unsigned int h, unsigned int vrefresh,
		       unsigned int bpc, struct cmdq_pkt *cmdq_pkt);
	void (*start)(struct mtk_ddp_comp *comp);
	void (*stop)(struct mtk_ddp_comp *comp);
	void (*enable_vblank)(struct mtk_ddp_comp *comp, struct drm_crtc *crtc);
	void (*disable_vblank)(struct mtk_ddp_comp *comp);
	void (*prepare)(struct mtk_ddp_comp *comp);
	void (*unprepare)(struct mtk_ddp_comp *comp);
	unsigned int (*supported_rotations)(struct mtk_ddp_comp *comp);
	unsigned int (*layer_nr)(struct mtk_ddp_comp *comp);
	int (*layer_check)(struct mtk_ddp_comp *comp,
			   unsigned int idx,
			   struct mtk_plane_state *state);
	void (*layer_config)(struct mtk_ddp_comp *comp, unsigned int idx,
			     struct mtk_plane_state *state,
			     struct cmdq_pkt *cmdq_pkt);
	void (*gamma_set)(struct mtk_ddp_comp *comp,
			  struct drm_crtc_state *state);
	void (*bgclr_in_on)(struct mtk_ddp_comp *comp);
	void (*bgclr_in_off)(struct mtk_ddp_comp *comp);
	void (*ctm_set)(struct mtk_ddp_comp *comp,
			struct drm_crtc_state *state);
};

struct mtk_ddp_comp {
	struct clk *clk;
	void __iomem *regs;
	int irq;
	struct device *dev;
	enum mtk_ddp_comp_id id;
	const struct mtk_ddp_comp_funcs *funcs;
	resource_size_t regs_pa;
	u8 subsys;
};

static inline void mtk_ddp_comp_config(struct mtk_ddp_comp *comp,
				       unsigned int w, unsigned int h,
				       unsigned int vrefresh, unsigned int bpc,
				       struct cmdq_pkt *cmdq_pkt)
{
	if (comp->funcs && comp->funcs->config)
		comp->funcs->config(comp, w, h, vrefresh, bpc, cmdq_pkt);
}

static inline void mtk_ddp_comp_prepare(struct mtk_ddp_comp *comp)
{
	if (comp->funcs && comp->funcs->prepare)
		comp->funcs->prepare(comp);
}

static inline void mtk_ddp_comp_unprepare(struct mtk_ddp_comp *comp)
{
	if (comp->funcs && comp->funcs->unprepare)
		comp->funcs->unprepare(comp);
}

static inline void mtk_ddp_comp_start(struct mtk_ddp_comp *comp)
{
	if (comp->funcs && comp->funcs->start)
		comp->funcs->start(comp);
}

static inline void mtk_ddp_comp_stop(struct mtk_ddp_comp *comp)
{
	if (comp->funcs && comp->funcs->stop)
		comp->funcs->stop(comp);
}

static inline void mtk_ddp_comp_enable_vblank(struct mtk_ddp_comp *comp,
					      struct drm_crtc *crtc)
{
	if (comp->funcs && comp->funcs->enable_vblank)
		comp->funcs->enable_vblank(comp, crtc);
}

static inline void mtk_ddp_comp_disable_vblank(struct mtk_ddp_comp *comp)
{
	if (comp->funcs && comp->funcs->disable_vblank)
		comp->funcs->disable_vblank(comp);
}

static inline
unsigned int mtk_ddp_comp_supported_rotations(struct mtk_ddp_comp *comp)
{
	if (comp->funcs && comp->funcs->supported_rotations)
		return comp->funcs->supported_rotations(comp);

	return 0;
}

static inline unsigned int mtk_ddp_comp_layer_nr(struct mtk_ddp_comp *comp)
{
	if (comp->funcs && comp->funcs->layer_nr)
		return comp->funcs->layer_nr(comp);

	return 0;
}

static inline int mtk_ddp_comp_layer_check(struct mtk_ddp_comp *comp,
					   unsigned int idx,
					   struct mtk_plane_state *state)
{
	if (comp->funcs && comp->funcs->layer_check)
		return comp->funcs->layer_check(comp, idx, state);
	return 0;
}

static inline void mtk_ddp_comp_layer_config(struct mtk_ddp_comp *comp,
					     unsigned int idx,
					     struct mtk_plane_state *state,
					     struct cmdq_pkt *cmdq_pkt)
{
	if (comp->funcs && comp->funcs->layer_config)
		comp->funcs->layer_config(comp, idx, state, cmdq_pkt);
}

static inline void mtk_ddp_gamma_set(struct mtk_ddp_comp *comp,
				     struct drm_crtc_state *state)
{
	if (comp->funcs && comp->funcs->gamma_set)
		comp->funcs->gamma_set(comp, state);
}

static inline void mtk_ddp_comp_bgclr_in_on(struct mtk_ddp_comp *comp)
{
	if (comp->funcs && comp->funcs->bgclr_in_on)
		comp->funcs->bgclr_in_on(comp);
}

static inline void mtk_ddp_comp_bgclr_in_off(struct mtk_ddp_comp *comp)
{
	if (comp->funcs && comp->funcs->bgclr_in_off)
		comp->funcs->bgclr_in_off(comp);
}

static inline void mtk_ddp_ctm_set(struct mtk_ddp_comp *comp,
				   struct drm_crtc_state *state)
{
	if (comp->funcs && comp->funcs->ctm_set)
		comp->funcs->ctm_set(comp, state);
}

int mtk_ddp_comp_get_id(struct device_node *node,
			enum mtk_ddp_comp_type comp_type);
int mtk_ddp_comp_init(struct device *dev, struct device_node *comp_node,
		      struct mtk_ddp_comp *comp, enum mtk_ddp_comp_id comp_id,
		      const struct mtk_ddp_comp_funcs *funcs);
int mtk_ddp_comp_register(struct drm_device *drm, struct mtk_ddp_comp *comp);
void mtk_ddp_comp_unregister(struct drm_device *drm, struct mtk_ddp_comp *comp);
void mtk_dither_set(struct mtk_ddp_comp *comp, unsigned int bpc,
		    unsigned int CFG, struct cmdq_pkt *cmdq_pkt);
enum mtk_ddp_comp_type mtk_ddp_comp_get_type(enum mtk_ddp_comp_id comp_id);
void mtk_ddp_write(struct cmdq_pkt *cmdq_pkt, unsigned int value,
		   struct mtk_ddp_comp *comp, unsigned int offset);
void mtk_ddp_write_relaxed(struct cmdq_pkt *cmdq_pkt, unsigned int value,
			   struct mtk_ddp_comp *comp, unsigned int offset);
void mtk_ddp_write_mask(struct cmdq_pkt *cmdq_pkt, unsigned int value,
			struct mtk_ddp_comp *comp, unsigned int offset,
			unsigned int mask);
#endif /* MTK_DRM_DDP_COMP_H */
