/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: Ming Hsiu Tsai <minghsiu.tsai@mediatek.com>
 */

#ifndef __MTK_MDP_COMP_H__
#define __MTK_MDP_COMP_H__

/**
 * enum mtk_mdp_comp_type - the MDP component
 * @MTK_MDP_RDMA:		Read DMA
 * @MTK_MDP_RSZ:		Reszer
 * @MTK_MDP_WDMA:		Write DMA
 * @MTK_MDP_WROT:		Write DMA with rotation
 * @MTK_MDP_COMP_TYPE_MAX:	Placeholder for num elems in this enum
 */
enum mtk_mdp_comp_type {
	MTK_MDP_RDMA,
	MTK_MDP_RSZ,
	MTK_MDP_WDMA,
	MTK_MDP_WROT,
	MTK_MDP_COMP_TYPE_MAX,
};

/**
 * struct mtk_mdp_comp - the MDP's function component data
 * @node:	list node to track sibing MDP components
 * @clk:	clocks required for component
 * @dev:	component's device
 * @type:	component type
 */
struct mtk_mdp_comp {
	struct list_head	node;
	struct clk		*clk[2];
	struct device		*dev;
	enum mtk_mdp_comp_type	type;
};

int mtk_mdp_comp_init(struct mtk_mdp_comp *comp, struct device *dev);

int mtk_mdp_comp_clock_on(struct mtk_mdp_comp *comp);
int mtk_mdp_comp_power_on(struct mtk_mdp_comp *comp);
int mtk_mdp_comp_power_off(struct mtk_mdp_comp *comp);

void mtk_mdp_comp_clock_off(struct mtk_mdp_comp *comp);

extern struct platform_driver mtk_mdp_component_driver;

#endif /* __MTK_MDP_COMP_H__ */
