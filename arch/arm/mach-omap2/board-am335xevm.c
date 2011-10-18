/*
 * Code for AM335X EVM.
 *
 * Copyright (C) 2011 Texas Instruments, Inc. - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/i2c/at24.h>
#include <linux/gpio.h>
#include <linux/leds.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/err.h>

/* LCD controller is similar to DA850 */
#include <video/da8xx-fb.h>

#include <mach/hardware.h>
#include <mach/board-am335xevm.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/hardware/asp.h>

#include <plat/irqs.h>
#include <plat/board.h>
#include <plat/common.h>
#include <plat/lcdc.h>
#include <plat/usb.h>
#include <plat/mmc.h>

/* LCD controller is similar to DA850 */
#include <video/da8xx-fb.h>

#include "board-flash.h"
#include "mux.h"
#include "devices.h"
#include "hsmmc.h"

static const struct display_panel disp_panel = {
	WVGA,
	32,
	32,
	COLOR_ACTIVE,
};

static struct lcd_ctrl_config lcd_cfg = {
	&disp_panel,
	.ac_bias		= 255,
	.ac_bias_intrpt		= 0,
	.dma_burst_sz		= 16,
	.bpp			= 32,
	.fdd			= 0x80,
	.tft_alt_mode		= 0,
	.stn_565_mode		= 0,
	.mono_8bit_mode		= 0,
	.invert_line_clock	= 1,
	.invert_frm_clock	= 1,
	.sync_edge		= 0,
	.sync_ctrl		= 1,
	.raster_order		= 0,
	.fifo_th		= 6,
};

struct da8xx_lcdc_platform_data TFC_S9700RTWV35TR_01B_pdata = {
	.manu_name		= "ThreeFive",
	.controller_data	= &lcd_cfg,
	.type			= "TFC_S9700RTWV35TR_01B",
};

/* TSc controller */
#include <linux/input/ti_tscadc.h>

static struct resource tsc_resources[]  = {
	[0] = {
		.start  = AM335X_TSC_BASE,
		.end    = AM335X_TSC_BASE + SZ_8K - 1,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = AM335X_IRQ_ADC_GEN,
		.end    = AM335X_IRQ_ADC_GEN,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct tsc_data am335x_touchscreen_data  = {
	.wires  = 4,
};

static struct platform_device tsc_device = {
	.name   = "tsc",
	.id     = -1,
	.dev    = {
			.platform_data  = &am335x_touchscreen_data,
	},
	.num_resources  = ARRAY_SIZE(tsc_resources),
	.resource       = tsc_resources,
};

static u8 am335x_iis_serializer_direction1[] = {
	INACTIVE_MODE,	INACTIVE_MODE,	TX_MODE,	RX_MODE,
	INACTIVE_MODE,	INACTIVE_MODE,	INACTIVE_MODE,	INACTIVE_MODE,
	INACTIVE_MODE,	INACTIVE_MODE,	INACTIVE_MODE,	INACTIVE_MODE,
	INACTIVE_MODE,	INACTIVE_MODE,	INACTIVE_MODE,	INACTIVE_MODE,
};

static struct snd_platform_data am335x_evm_snd_data1 = {
	.tx_dma_offset	= 0x46400000,	/* McASP1 */
	.rx_dma_offset	= 0x46400000,
	.op_mode	= DAVINCI_MCASP_IIS_MODE,
	.num_serializer	= ARRAY_SIZE(am335x_iis_serializer_direction1),
	.tdm_slots	= 2,
	.serial_dir	= am335x_iis_serializer_direction1,
	.asp_chan_q	= EVENTQ_2,
	.version	= MCASP_VERSION_3,
	.txnumevt	= 1,
	.rxnumevt	= 1,
};

static struct omap2_hsmmc_info am335x_mmc[] __initdata = {
	{
		.mmc            = 1,
		.caps           = MMC_CAP_4_BIT_DATA,
		.gpio_cd        = -EINVAL,/* Dedicated pins for CD and WP */
		.gpio_wp        = -EINVAL,
		.ocr_mask       = MMC_VDD_32_33 | MMC_VDD_33_34, /* 3V3 */
	},
	{
		.mmc            = 0,	/* will be set at runtime */
	},
	{
		.mmc            = 0,	/* will be set at runtime */
	},
	{}      /* Terminator */
};


#ifdef CONFIG_OMAP_MUX
static struct omap_board_mux board_mux[] __initdata = {
	AM335X_MUX(I2C0_SDA, OMAP_MUX_MODE0 | AM335X_SLEWCTRL_SLOW |
			AM335X_INPUT_EN | AM335X_PIN_OUTPUT),
	AM335X_MUX(I2C0_SCL, OMAP_MUX_MODE0 | AM335X_SLEWCTRL_SLOW |
			AM335X_INPUT_EN | AM335X_PIN_OUTPUT),
	{ .reg_offset = OMAP_MUX_TERMINATOR },
};
#else
#define	board_mux	NULL
#endif

/* module pin mux structure */
struct pinmux_config {
	const char *string_name; /* signal name format */
	int val; /* Options for the mux register value */
};

struct evm_dev_cfg {
	void (*device_init)(int evm_id, int profile);

/*
* If the device is required on both baseboard & daughter board (ex i2c),
* specify DEV_ON_BASEBOARD
*/
#define DEV_ON_BASEBOARD	0
#define DEV_ON_DGHTR_BRD	1
	u32 device_on;

	u32 profile;	/* Profiles (0-7) in which the module is present */
};

/* AM335X - CPLD Register Offsets */
#define	CPLD_DEVICE_HDR	0x00 /* CPLD Header */
#define	CPLD_DEVICE_ID	0x04 /* CPLD identification */
#define	CPLD_DEVICE_REV	0x0C /* Revision of the CPLD code */
#define	CPLD_CFG_REG	0x10 /* Configuration Register */

static struct i2c_client *cpld_client;
static struct i2c_client *pmic_client;

static u32 am335x_evm_id;

static struct omap_board_config_kernel am335x_evm_config[] __initdata = {
};

static void __init am335x_init_early(void)
{
	omap2_init_common_infrastructure();
}

/*
* EVM Config held in On-Board eeprom device.
*
* Header Format
*
*  Name			Size	Contents
*			(Bytes)
*-------------------------------------------------------------
*  Header		4	0xAA, 0x55, 0x33, 0xEE
*
*  Board Name		8	Name for board in ASCII.
*				example "A33515BB" = "AM335X
				Low Cost EVM board"
*
*  Version		4	Hardware version code for board in
*				in ASCII. "1.0A" = rev.01.0A
*
*  Serial Number	12	Serial number of the board. This is a 12
*				character string which is WWYY4P16nnnn, where
*				WW = 2 digit week of the year of production
*				YY = 2 digit year of production
*				nnnn = incrementing board number
*
*  Configuration option	32	Codes(TBD) to show the configuration
*				setup on this board.
*
*  Available		32720	Available space for other non-volatile
*				data.
*/
struct am335x_evm_eeprom_config {
	u32	header;
	u8	name[8];
	u32	version;
	u8	serial[12];
	u8	opt[32];
};

static struct am335x_evm_eeprom_config config;
static bool daughter_brd_detected;

#define AM335X_EEPROM_HEADER		0xEE3355AA

/* current profile if exists else PROFILE_0 on error */
static u32 am335x_get_profile_selection(void)
{
	int val = 0;

	if (!cpld_client)
		/* error checking is not done in func's calling this routine.
		so return profile 0 on error */
		return 0;

	val = i2c_smbus_read_word_data(cpld_client, CPLD_CFG_REG);
	if (val < 0)
		return 0;	/* default to Profile 0 on Error */
	else
		return val & 0x7;
}

/* Module pin mux for LCDC */
static struct pinmux_config lcdc_pin_mux[] = {
	{"lcd_data0.lcd_data0",		OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT
						       | AM335X_PULL_DISA},
	{"lcd_data1.lcd_data1",		OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT
						       | AM335X_PULL_DISA},
	{"lcd_data2.lcd_data2",		OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT
						       | AM335X_PULL_DISA},
	{"lcd_data3.lcd_data3",		OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT
						       | AM335X_PULL_DISA},
	{"lcd_data4.lcd_data4",		OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT
						       | AM335X_PULL_DISA},
	{"lcd_data5.lcd_data5",		OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT
						       | AM335X_PULL_DISA},
	{"lcd_data6.lcd_data6",		OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT
						       | AM335X_PULL_DISA},
	{"lcd_data7.lcd_data7",		OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT
						       | AM335X_PULL_DISA},
	{"lcd_data8.lcd_data8",		OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT
						       | AM335X_PULL_DISA},
	{"lcd_data9.lcd_data9",		OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT
						       | AM335X_PULL_DISA},
	{"lcd_data10.lcd_data10",	OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT
						       | AM335X_PULL_DISA},
	{"lcd_data11.lcd_data11",	OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT
						       | AM335X_PULL_DISA},
	{"lcd_data12.lcd_data12",	OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT
						       | AM335X_PULL_DISA},
	{"lcd_data13.lcd_data13",	OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT
						       | AM335X_PULL_DISA},
	{"lcd_data14.lcd_data14",	OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT
						       | AM335X_PULL_DISA},
	{"lcd_data15.lcd_data15",	OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT
						       | AM335X_PULL_DISA},
	{"gpmc_ad8.lcd_data16",		OMAP_MUX_MODE1 | AM335X_PIN_OUTPUT},
	{"gpmc_ad9.lcd_data17",		OMAP_MUX_MODE1 | AM335X_PIN_OUTPUT},
	{"gpmc_ad10.lcd_data18",	OMAP_MUX_MODE1 | AM335X_PIN_OUTPUT},
	{"gpmc_ad11.lcd_data19",	OMAP_MUX_MODE1 | AM335X_PIN_OUTPUT},
	{"gpmc_ad12.lcd_data20",	OMAP_MUX_MODE1 | AM335X_PIN_OUTPUT},
	{"gpmc_ad13.lcd_data21",	OMAP_MUX_MODE1 | AM335X_PIN_OUTPUT},
	{"gpmc_ad14.lcd_data22",	OMAP_MUX_MODE1 | AM335X_PIN_OUTPUT},
	{"gpmc_ad15.lcd_data23",	OMAP_MUX_MODE1 | AM335X_PIN_OUTPUT},
	{"lcd_vsync.lcd_vsync",		OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"lcd_hsync.lcd_hsync",		OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"lcd_pclk.lcd_pclk",		OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"lcd_ac_bias_en.lcd_ac_bias_en", OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{NULL, 0},
};

static struct pinmux_config tsc_pin_mux[] = {
	{"ain0.ain0",           OMAP_MUX_MODE0 | AM335X_INPUT_EN},
	{"ain1.ain1",           OMAP_MUX_MODE0 | AM335X_INPUT_EN},
	{"ain2.ain2",           OMAP_MUX_MODE0 | AM335X_INPUT_EN},
	{"ain3.ain3",           OMAP_MUX_MODE0 | AM335X_INPUT_EN},
	{"vrefp.vrefp",         OMAP_MUX_MODE0 | AM335X_INPUT_EN},
	{"vrefn.vrefn",         OMAP_MUX_MODE0 | AM335X_INPUT_EN},
	{NULL, 0},
};

/* Pin mux for nand flash module */
static struct pinmux_config nand_pin_mux[] = {
	{"gpmc_ad0.gpmc_ad0",	  OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"gpmc_ad1.gpmc_ad1",	  OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"gpmc_ad2.gpmc_ad2",	  OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"gpmc_ad3.gpmc_ad3",	  OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"gpmc_ad4.gpmc_ad4",	  OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"gpmc_ad5.gpmc_ad5",	  OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"gpmc_ad6.gpmc_ad6",	  OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"gpmc_ad7.gpmc_ad7",	  OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"gpmc_wait0.gpmc_wait0", OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"gpmc_wpn.gpmc_wpn",	  OMAP_MUX_MODE7 | AM335X_PIN_INPUT_PULLUP},
	{"gpmc_csn0.gpmc_csn0",	  OMAP_MUX_MODE0 | AM335X_PULL_DISA},
	{"gpmc_advn_ale.gpmc_advn_ale",  OMAP_MUX_MODE0 | AM335X_PULL_DISA},
	{"gpmc_oen_ren.gpmc_oen_ren",	 OMAP_MUX_MODE0 | AM335X_PULL_DISA},
	{"gpmc_wen.gpmc_wen",     OMAP_MUX_MODE0 | AM335X_PULL_DISA},
	{"gpmc_ben0_cle.gpmc_ben0_cle",	 OMAP_MUX_MODE0 | AM335X_PULL_DISA},
	{NULL, 0},
};


/* Module pin mux for rgmii1 */
static struct pinmux_config rgmii1_pin_mux[] = {
	{"mii1_txen.rgmii1_tctl", OMAP_MUX_MODE2 | AM335X_PIN_OUTPUT},
	{"mii1_rxdv.rgmii1_rctl", OMAP_MUX_MODE2 | AM335X_PIN_INPUT_PULLDOWN},
	{"mii1_txd3.rgmii1_td3", OMAP_MUX_MODE2 | AM335X_PIN_OUTPUT},
	{"mii1_txd2.rgmii1_td2", OMAP_MUX_MODE2 | AM335X_PIN_OUTPUT},
	{"mii1_txd1.rgmii1_td1", OMAP_MUX_MODE2 | AM335X_PIN_OUTPUT},
	{"mii1_txd0.rgmii1_td0", OMAP_MUX_MODE2 | AM335X_PIN_OUTPUT},
	{"mii1_txclk.rgmii1_tclk", OMAP_MUX_MODE2 | AM335X_PIN_OUTPUT},
	{"mii1_rxclk.rgmii1_rclk", OMAP_MUX_MODE2 | AM335X_PIN_INPUT_PULLDOWN},
	{"mii1_rxd3.rgmii1_rd3", OMAP_MUX_MODE2 | AM335X_PIN_INPUT_PULLDOWN},
	{"mii1_rxd2.rgmii1_rd2", OMAP_MUX_MODE2 | AM335X_PIN_INPUT_PULLDOWN},
	{"mii1_rxd1.rgmii1_rd1", OMAP_MUX_MODE2 | AM335X_PIN_INPUT_PULLDOWN},
	{"mii1_rxd0.rgmii1_rd0", OMAP_MUX_MODE2 | AM335X_PIN_INPUT_PULLDOWN},
	{"mdio_data.mdio_data", OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"mdio_clk.mdio_clk", OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT_PULLUP},
	{NULL, 0},
};

/* Module pin mux for rgmii2 */
static struct pinmux_config rgmii2_pin_mux[] = {
	{"gpmc_a0.rgmii2_tctl", OMAP_MUX_MODE2 | AM335X_PIN_OUTPUT},
	{"gpmc_a1.rgmii2_rctl", OMAP_MUX_MODE2 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_a2.rgmii2_td3", OMAP_MUX_MODE2 | AM335X_PIN_OUTPUT},
	{"gpmc_a3.rgmii2_td2", OMAP_MUX_MODE2 | AM335X_PIN_OUTPUT},
	{"gpmc_a4.rgmii2_td1", OMAP_MUX_MODE2 | AM335X_PIN_OUTPUT},
	{"gpmc_a5.rgmii2_td0", OMAP_MUX_MODE2 | AM335X_PIN_OUTPUT},
	{"gpmc_a6.rgmii2_tclk", OMAP_MUX_MODE2 | AM335X_PIN_OUTPUT},
	{"gpmc_a7.rgmii2_rclk", OMAP_MUX_MODE2 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_a8.rgmii2_rd3", OMAP_MUX_MODE2 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_a9.rgmii2_rd2", OMAP_MUX_MODE2 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_a10.rgmii2_rd1", OMAP_MUX_MODE2 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_a11.rgmii2_rd0", OMAP_MUX_MODE2 | AM335X_PIN_INPUT_PULLDOWN},
	{"mdio_data.mdio_data", OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"mdio_clk.mdio_clk", OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT_PULLUP},
	{NULL, 0},
};

/* Module pin mux for mii1 */
static struct pinmux_config mii1_pin_mux[] = {
	{"mii1_rxerr.mii1_rxerr", OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"mii1_txen.mii1_txen", OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"mii1_rxdv.mii1_rxdv", OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"mii1_txd3.mii1_txd3", OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"mii1_txd2.mii1_txd2", OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"mii1_txd1.mii1_txd1", OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"mii1_txd0.mii1_txd0", OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"mii1_txclk.mii1_txclk", OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"mii1_rxclk.mii1_rxclk", OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"mii1_rxd3.mii1_rxd3", OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"mii1_rxd2.mii1_rxd2", OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"mii1_rxd1.mii1_rxd1", OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"mii1_rxd0.mii1_rxd0", OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"mdio_data.mdio_data", OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"mdio_clk.mdio_clk", OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT_PULLUP},
	{NULL, 0},
};

/* Module pin mux for rmii1 */
static struct pinmux_config rmii1_pin_mux[] = {
	{"mii1_crs.rmii1_crs_dv", OMAP_MUX_MODE1 | AM335X_PIN_INPUT_PULLDOWN},
	{"mii1_rxerr.mii1_rxerr", OMAP_MUX_MODE1 | AM335X_PIN_INPUT_PULLDOWN},
	{"mii1_txen.mii1_txen", OMAP_MUX_MODE1 | AM335X_PIN_OUTPUT},
	{"mii1_txd1.mii1_txd1", OMAP_MUX_MODE1 | AM335X_PIN_OUTPUT},
	{"mii1_txd0.mii1_txd0", OMAP_MUX_MODE1 | AM335X_PIN_OUTPUT},
	{"mii1_rxd1.mii1_rxd1", OMAP_MUX_MODE1 | AM335X_PIN_INPUT_PULLDOWN},
	{"mii1_rxd0.mii1_rxd0", OMAP_MUX_MODE1 | AM335X_PIN_INPUT_PULLDOWN},
	{"rmii1_refclk.rmii1_refclk", OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"mdio_data.mdio_data", OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"mdio_clk.mdio_clk", OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT_PULLUP},
	{NULL, 0},
};

static struct pinmux_config i2c1_pin_mux[] = {
	{"spi0_d1.i2c1_sda",    OMAP_MUX_MODE2 | AM335X_SLEWCTRL_SLOW |
					AM335X_PULL_ENBL | AM335X_INPUT_EN},
	{"spi0_cs0.i2c1_scl",   OMAP_MUX_MODE2 | AM335X_SLEWCTRL_SLOW |
					AM335X_PULL_ENBL | AM335X_INPUT_EN},
	{NULL, 0},
};

/* Module pin mux for mcasp1 */
static struct pinmux_config mcasp1_pin_mux[] = {
	{"mii1_crs.mcasp1_aclkx", OMAP_MUX_MODE4 | AM335X_PIN_INPUT_PULLDOWN},
	{"mii1_rxerr.mcasp1_fsx", OMAP_MUX_MODE4 | AM335X_PIN_INPUT_PULLDOWN},
	{"mii1_col.mcasp1_axr2", OMAP_MUX_MODE4 | AM335X_PIN_INPUT_PULLDOWN},
	{"rmii1_refclk.mcasp1_axr3", OMAP_MUX_MODE4 |
						AM335X_PIN_INPUT_PULLDOWN},
	{NULL, 0},
};


/* Module pin mux for mmc0 */
static struct pinmux_config mmc0_pin_mux[] = {
	{"mmc0_dat3.mmc0_dat3",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"mmc0_dat2.mmc0_dat2",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"mmc0_dat1.mmc0_dat1",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"mmc0_dat0.mmc0_dat0",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"mmc0_clk.mmc0_clk",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"mmc0_cmd.mmc0_cmd",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"mcasp0_aclkr.mmc0_sdwp", OMAP_MUX_MODE4 | AM335X_PIN_INPUT_PULLDOWN},
	{"spi0_cs1.mmc0_sdcd",  OMAP_MUX_MODE5 | AM335X_PIN_INPUT_PULLUP},
	{NULL, 0},
};

static struct pinmux_config mmc0_no_cd_pin_mux[] = {
	{"mmc0_dat3.mmc0_dat3",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"mmc0_dat2.mmc0_dat2",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"mmc0_dat1.mmc0_dat1",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"mmc0_dat0.mmc0_dat0",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"mmc0_clk.mmc0_clk",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"mmc0_cmd.mmc0_cmd",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"mcasp0_aclkr.mmc0_sdwp", OMAP_MUX_MODE4 | AM335X_PIN_INPUT_PULLDOWN},
	{NULL, 0},
};

/* Module pin mux for mmc1 */
static struct pinmux_config mmc1_pin_mux[] = {
	{"gpmc_ad7.mmc1_dat7",	OMAP_MUX_MODE1 | AM335X_PIN_INPUT_PULLUP},
	{"gpmc_ad6.mmc1_dat6",	OMAP_MUX_MODE1 | AM335X_PIN_INPUT_PULLUP},
	{"gpmc_ad5.mmc1_dat5",	OMAP_MUX_MODE1 | AM335X_PIN_INPUT_PULLUP},
	{"gpmc_ad4.mmc1_dat4",	OMAP_MUX_MODE1 | AM335X_PIN_INPUT_PULLUP},
	{"gpmc_ad3.mmc1_dat3",	OMAP_MUX_MODE1 | AM335X_PIN_INPUT_PULLUP},
	{"gpmc_ad2.mmc1_dat2",	OMAP_MUX_MODE1 | AM335X_PIN_INPUT_PULLUP},
	{"gpmc_ad1.mmc1_dat1",	OMAP_MUX_MODE1 | AM335X_PIN_INPUT_PULLUP},
	{"gpmc_ad0.mmc1_dat0",	OMAP_MUX_MODE1 | AM335X_PIN_INPUT_PULLUP},
	{"gpmc_csn1.mmc1_clk",	OMAP_MUX_MODE2 | AM335X_PIN_INPUT_PULLUP},
	{"gpmc_csn2.mmc1_cmd",	OMAP_MUX_MODE2 | AM335X_PIN_INPUT_PULLUP},
	{"uart1_rxd.mmc1_sdwp",	OMAP_MUX_MODE1 | AM335X_PIN_INPUT_PULLUP},
	{"mcasp0_fsx.mmc1_sdcd", OMAP_MUX_MODE4 | AM335X_PIN_INPUT_PULLDOWN},
	{NULL, 0},
};

/* Module pin mux for mmc2 */
static struct pinmux_config mmc2_pin_mux[] = {
	{"gpmc_ad11.mmc2_dat7",	OMAP_MUX_MODE3 | AM335X_PIN_INPUT_PULLUP},
	{"gpmc_ad10.mmc2_dat6",	OMAP_MUX_MODE3 | AM335X_PIN_INPUT_PULLUP},
	{"gpmc_ad9.mmc2_dat5",	OMAP_MUX_MODE3 | AM335X_PIN_INPUT_PULLUP},
	{"gpmc_ad8.mmc2_dat4",	OMAP_MUX_MODE3 | AM335X_PIN_INPUT_PULLUP},
	{"gpmc_ad15.mmc2_dat3",	OMAP_MUX_MODE3 | AM335X_PIN_INPUT_PULLUP},
	{"gpmc_ad14.mmc2_dat2",	OMAP_MUX_MODE3 | AM335X_PIN_INPUT_PULLUP},
	{"gpmc_ad13.mmc2_dat1",	OMAP_MUX_MODE3 | AM335X_PIN_INPUT_PULLUP},
	{"gpmc_ad12.mmc2_dat0",	OMAP_MUX_MODE3 | AM335X_PIN_INPUT_PULLUP},
	{"gpmc_clk.mmc2_clk",	OMAP_MUX_MODE3 | AM335X_PIN_INPUT_PULLUP},
	{"gpmc_csn3.mmc2_cmd",	OMAP_MUX_MODE3 | AM335X_PIN_INPUT_PULLUP},
	{"spi0_cs0.mmc2_sdwp",	OMAP_MUX_MODE1 | AM335X_PIN_INPUT_PULLDOWN},
	{"mcasp0_axr0.mmc2_sdcd", OMAP_MUX_MODE4 | AM335X_PIN_INPUT_PULLUP},
	{NULL, 0},
};

/*
* @pin_mux - single module pin-mux structure which defines pin-mux
*			details for all its pins.
*/
static void setup_pin_mux(struct pinmux_config *pin_mux)
{
	int i;

	for (i = 0; pin_mux->string_name != NULL; pin_mux++)
		omap_mux_init_signal(pin_mux->string_name, pin_mux->val);

}

/*
* @evm_id - evm id which needs to be configured
* @dev_cfg - single evm structure which includes
*				all module inits, pin-mux defines
* @profile - if present, else PROFILE_NONE
* @dghtr_brd_flg - Whether Daughter board is present or not
*/
static void _configure_device(int evm_id, struct evm_dev_cfg *dev_cfg,
	int profile)
{
	int i;

	/*
	* Only General Purpose & Industrial Auto Motro Control
	* EVM has profiles. So check if this evm has profile.
	* If not, ignore the profile comparison
	*/

	/*
	* If the device is on baseboard, directly configure it. Else (device on
	* Daughter board), check if the daughter card is detected.
	*/
	if (profile == PROFILE_NONE) {
		for (i = 0; dev_cfg->device_init != NULL; dev_cfg++) {
			if (dev_cfg->device_on == DEV_ON_BASEBOARD)
				dev_cfg->device_init(evm_id, profile);
			else if (daughter_brd_detected == true)
				dev_cfg->device_init(evm_id, profile);
		}
	} else {
		for (i = 0; dev_cfg->device_init != NULL; dev_cfg++) {
			if (dev_cfg->profile & profile) {
				if (dev_cfg->device_on == DEV_ON_BASEBOARD)
					dev_cfg->device_init(evm_id, profile);
				else if (daughter_brd_detected == true)
					dev_cfg->device_init(evm_id, profile);
			}
		}
	}
}

/* Convert GPIO signal to GPIO pin number */
#define GPIO_TO_PIN(bank, gpio) (32 * (bank) + (gpio))

#define AM335X_LCD_BL_PIN	GPIO_TO_PIN(0, 7)

/* pinmux for usb0 drvvbus */
static struct pinmux_config usb0_pin_mux[] = {
	{"usb0_drvvbus.usb0_drvvbus",    OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{NULL, 0},
};

/* pinmux for usb1 drvvbus */
static struct pinmux_config usb1_pin_mux[] = {
	{"usb1_drvvbus.usb1_drvvbus",    OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{NULL, 0},
};

/* LEDS - gpio1_21 -> gpio1_24 */

#define BEAGLEBONE_USR1_LED  GPIO_TO_PIN(1, 21)
#define BEAGLEBONE_USR2_LED  GPIO_TO_PIN(1, 22)
#define BEAGLEBONE_USR3_LED  GPIO_TO_PIN(1, 23)
#define BEAGLEBONE_USR4_LED  GPIO_TO_PIN(1, 24)

static struct gpio_led gpio_leds[] = {
	{
		.name			= "beaglebone::usr0",
		.default_trigger	= "heartbeat",
		.gpio			= BEAGLEBONE_USR1_LED,
	},
	{
		.name			= "beaglebone::usr1",
		.default_trigger	= "mmc0",
		.gpio			= BEAGLEBONE_USR2_LED,
	},
	{
		.name			= "beaglebone::usr2",
		.gpio			= BEAGLEBONE_USR3_LED,
	},
	{
		.name           = "beaglebone::usr3",
		.gpio           = BEAGLEBONE_USR4_LED,
	},
};

static struct gpio_led_platform_data gpio_led_info = {
	.leds		= gpio_leds,
	.num_leds	= ARRAY_SIZE(gpio_leds),
};

static struct platform_device leds_gpio = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data	= &gpio_led_info,
	},
};

static struct platform_device *bone_devices[] __initdata = {
		    &leds_gpio,
};

static struct pinmux_config boneled_pin_mux[] = {
    {"gpmc_a5.rgmii2_td0", OMAP_MUX_MODE7 | AM335X_PIN_OUTPUT}, // gpio 21
    {"gpmc_a6.rgmii2_tclk", OMAP_MUX_MODE7 | AM335X_PIN_OUTPUT}, // gpio22
    {"gpmc_a7.rgmii2_rclk", OMAP_MUX_MODE7 | AM335X_PIN_OUTPUT}, // gpio23
    {"gpmc_a8.rgmii2_rd3", OMAP_MUX_MODE7 | AM335X_PIN_OUTPUT}, // gpio 24
};

/* Module pin mux for eCAP0 */
static struct pinmux_config ecap0_pin_mux[] = {
	{"ecap0_in_pwm0_out.gpio0_7", AM335X_PIN_OUTPUT},
	{NULL, 0},
};

static int backlight_enable = false;

static void enable_ecap0(int evm_id, int profile)
{
	backlight_enable = true;
}

static int __init ecap0_init(void)
{
	int status = 0;

	if (backlight_enable) {
		setup_pin_mux(ecap0_pin_mux);

		status = gpio_request(AM335X_LCD_BL_PIN, "lcd bl\n");
		if (status < 0)
			pr_warn("Failed to request gpio for LCD backlight\n");

		gpio_direction_output(AM335X_LCD_BL_PIN, 1);
	}
	return status;
}
late_initcall(ecap0_init);

static int __init conf_disp_pll(int rate)
{
	struct clk *disp_pll;
	int ret = -EINVAL;

	disp_pll = clk_get(NULL, "dpll_disp_ck");
	if (IS_ERR(disp_pll)) {
		pr_err("Cannot clk_get disp_pll\n");
		goto out;
	}

	ret = clk_set_rate(disp_pll, rate);
	clk_put(disp_pll);
out:
	return ret;
}

static void lcdc_init(int evm_id, int profile)
{

	setup_pin_mux(lcdc_pin_mux);

	if (conf_disp_pll(300000000)) {
		pr_info("Failed configure display PLL, not attempting to"
				"register LCDC\n");
		return;
	}

	if (am335x_register_lcdc(&TFC_S9700RTWV35TR_01B_pdata))
		pr_info("Failed to register LCDC device\n");
	return;
}

static void tsc_init(int evm_id, int profile)
{
	int err;
	setup_pin_mux(tsc_pin_mux);
	err = platform_device_register(&tsc_device);
	if (err)
		pr_err("failed to register touchscreen device\n");
}

static void bone_leds_init(int evm_id, int profil )
{
	int err;
	setup_pin_mux(boneled_pin_mux);
	err = platform_add_devices(bone_devices, ARRAY_SIZE(bone_devices));
	if (err)
		pr_err("failed to register LEDS\n");
}

static void rgmii1_init(int evm_id, int profile)
{
	setup_pin_mux(rgmii1_pin_mux);
	return;
}

static void rgmii2_init(int evm_id, int profile)
{
	setup_pin_mux(rgmii2_pin_mux);
	return;
}

static void mii1_init(int evm_id, int profile)
{
	setup_pin_mux(mii1_pin_mux);
	return;
}

static void rmii1_init(int evm_id, int profile)
{
	setup_pin_mux(rmii1_pin_mux);
	return;
}

static void usb0_init(int evm_id, int profile)
{
	setup_pin_mux(usb0_pin_mux);
	return;
}

static void usb1_init(int evm_id, int profile)
{
	setup_pin_mux(usb1_pin_mux);
	return;
}

/* NAND partition information */
static struct mtd_partition am335x_nand_partitions[] = {
/* All the partition sizes are listed in terms of NAND block size */
	{
		.name           = "U-Boot-min",
		.offset         = 0,                    /* Offset = 0x0 */
		.size           = 4*SZ_128K,
		.mask_flags     = MTD_WRITEABLE,        /* force read-only */
	},
	{
		.name           = "U-Boot",
		.offset         = MTDPART_OFS_APPEND,   /* Offset = 0x8000 */
		.size           = 18 * SZ_128K,
		.mask_flags     = MTD_WRITEABLE,        /* force read-only */
	},
	{
		.name           = "U-Boot Env",
		.offset         = MTDPART_OFS_APPEND,   /* Offset = 0x2c0000 */
		.size           = 1 * SZ_128K,
	},
	{
		.name           = "Kernel",
		.offset         = MTDPART_OFS_APPEND,   /* Offset = 0x2E0000 */
		.size           = 34 * SZ_128K,
	},
	{
		.name           = "File System",
		.offset         = MTDPART_OFS_APPEND,   /* Offset = 0x720000 */
		.size           = 1601 * SZ_128K,
	},
	{
		.name           = "Reserved",
		.offset         = MTDPART_OFS_APPEND,   /* Offset = 0xCf40000 */
		.size           = MTDPART_SIZ_FULL,
	},
};

static void evm_nand_init(int evm_id, int profile)
{
	setup_pin_mux(nand_pin_mux);
	board_nand_init(am335x_nand_partitions,
		ARRAY_SIZE(am335x_nand_partitions), 0, 0);
}

static struct i2c_board_info __initdata am335x_i2c_boardinfo1[] = {
	{
		I2C_BOARD_INFO("tlv320aic3x", 0x1b),
	}
};

static void i2c1_init(int evm_id, int profile)
{
	setup_pin_mux(i2c1_pin_mux);
	omap_register_i2c_bus(2, 100, am335x_i2c_boardinfo1,
			ARRAY_SIZE(am335x_i2c_boardinfo1));
	return;
}

/* Setup McASP 1 */
static void mcasp1_init(int evm_id, int profile)
{
	/* Configure McASP */
	setup_pin_mux(mcasp1_pin_mux);
	am335x_register_mcasp1(&am335x_evm_snd_data1);
	return;
}

static void mmc1_init(int evm_id, int profile)
{
	setup_pin_mux(mmc1_pin_mux);

	am335x_mmc[1].mmc = 2;
	am335x_mmc[1].caps = MMC_CAP_4_BIT_DATA;
	am335x_mmc[1].gpio_cd = -EINVAL;
	am335x_mmc[1].gpio_wp = -EINVAL;
	am335x_mmc[1].ocr_mask = MMC_VDD_32_33 | MMC_VDD_33_34; /* 3V3 */

	/* mmc will be initialized when mmc0_init is called */
	return;
}

static void mmc2_init(int evm_id, int profile)
{
	setup_pin_mux(mmc2_pin_mux);

	am335x_mmc[1].mmc = 3;
	am335x_mmc[1].caps = MMC_CAP_4_BIT_DATA;
	am335x_mmc[1].gpio_cd = -EINVAL;
	am335x_mmc[1].gpio_wp = -EINVAL;
	am335x_mmc[1].ocr_mask = MMC_VDD_32_33 | MMC_VDD_33_34; /* 3V3 */

	/* mmc will be initialized when mmc0_init is called */
	return;
}

static void mmc0_init(int evm_id, int profile)
{
	setup_pin_mux(mmc0_pin_mux);

	omap2_hsmmc_init(am335x_mmc);
	return;
}

static void mmc0_no_cd_init(int evm_id, int profile)
{
	setup_pin_mux(mmc0_no_cd_pin_mux);

	omap2_hsmmc_init(am335x_mmc);
	return;
}


/* Low-Cost EVM */
static struct evm_dev_cfg low_cost_evm_dev_cfg[] = {
	{rgmii1_init,	DEV_ON_BASEBOARD, PROFILE_NONE},
	{usb0_init,	DEV_ON_BASEBOARD, PROFILE_NONE},
	{usb1_init,	DEV_ON_BASEBOARD, PROFILE_NONE},
	{evm_nand_init, DEV_ON_BASEBOARD, PROFILE_NONE},
	{NULL, 0, 0},
};

/* General Purpose EVM */
static struct evm_dev_cfg gen_purp_evm_dev_cfg[] = {
	{enable_ecap0,	DEV_ON_DGHTR_BRD, (PROFILE_0 | PROFILE_1 |
						PROFILE_2 | PROFILE_7) },
	{lcdc_init,	DEV_ON_DGHTR_BRD, (PROFILE_0 | PROFILE_1 |
						PROFILE_2 | PROFILE_7) },
	{tsc_init,	DEV_ON_DGHTR_BRD, (PROFILE_0 | PROFILE_1 |
						PROFILE_2 | PROFILE_7) },
	{rgmii1_init,	DEV_ON_BASEBOARD, PROFILE_ALL},
	{rgmii2_init,	DEV_ON_DGHTR_BRD, (PROFILE_1 | PROFILE_2 |
						PROFILE_4 | PROFILE_6) },
	{usb0_init,	DEV_ON_BASEBOARD, PROFILE_ALL},
	{usb1_init,	DEV_ON_BASEBOARD, PROFILE_ALL},
	{evm_nand_init, DEV_ON_DGHTR_BRD,
		(PROFILE_ALL & ~PROFILE_2 & ~PROFILE_3)},
	{i2c1_init,	DEV_ON_DGHTR_BRD, (PROFILE_0 | PROFILE_3 | PROFILE_7)},
	{mcasp1_init,	DEV_ON_DGHTR_BRD, (PROFILE_0 | PROFILE_3) },
	{mmc1_init,	DEV_ON_DGHTR_BRD, PROFILE_2},
	{mmc2_init,	DEV_ON_DGHTR_BRD, PROFILE_4},
	{mmc0_init,	DEV_ON_BASEBOARD, (PROFILE_ALL & ~PROFILE_5)},
	{mmc0_no_cd_init,	DEV_ON_BASEBOARD, PROFILE_5},
	{NULL, 0, 0},
};

/* Industrial Auto Motor Control EVM */
static struct evm_dev_cfg ind_auto_mtrl_evm_dev_cfg[] = {
	{mii1_init,	DEV_ON_DGHTR_BRD, PROFILE_ALL},
	{usb0_init,	DEV_ON_BASEBOARD, PROFILE_ALL},
	{usb1_init,	DEV_ON_BASEBOARD, PROFILE_ALL},
	{evm_nand_init, DEV_ON_DGHTR_BRD, PROFILE_ALL},
	{NULL, 0, 0},
};

/* IP-Phone EVM */
static struct evm_dev_cfg ip_phn_evm_dev_cfg[] = {
	{enable_ecap0,	DEV_ON_DGHTR_BRD, PROFILE_NONE},
	{lcdc_init,	DEV_ON_DGHTR_BRD, PROFILE_NONE},
	{tsc_init,	DEV_ON_DGHTR_BRD, PROFILE_NONE},
	{rgmii1_init,	DEV_ON_BASEBOARD, PROFILE_NONE},
	{rgmii2_init,	DEV_ON_DGHTR_BRD, PROFILE_NONE},
	{usb0_init,	DEV_ON_BASEBOARD, PROFILE_NONE},
	{usb1_init,	DEV_ON_BASEBOARD, PROFILE_NONE},
	{evm_nand_init, DEV_ON_DGHTR_BRD, PROFILE_NONE},
	{i2c1_init,	DEV_ON_BASEBOARD, PROFILE_NONE},
	{mcasp1_init,	DEV_ON_DGHTR_BRD, PROFILE_NONE},
	{mmc0_init,	DEV_ON_BASEBOARD, PROFILE_NONE},
	{NULL, 0, 0},
};

/* Beaglebone */
static struct evm_dev_cfg beaglebone_dev_cfg[] = {
	{rmii1_init,	DEV_ON_BASEBOARD, PROFILE_NONE},
	{usb0_init,	DEV_ON_BASEBOARD, PROFILE_NONE},
	{usb1_init,	DEV_ON_BASEBOARD, PROFILE_NONE},
	{mmc0_init,	DEV_ON_BASEBOARD, PROFILE_NONE},
	{i2c1_init,	DEV_ON_BASEBOARD, PROFILE_NONE},
	{bone_leds_init,  DEV_ON_BASEBOARD, PROFILE_ALL},
	{NULL, 0, 0},
};

static void setup_low_cost_evm(void)
{
	pr_info("The board is a AM335x Low Cost EVM.\n");

	_configure_device(LOW_COST_EVM, low_cost_evm_dev_cfg, PROFILE_NONE);
}

static void setup_general_purpose_evm(void)
{
	u32 prof_sel = am335x_get_profile_selection();

	pr_info("The board is general purpose EVM in profile %d\n", prof_sel);

	_configure_device(GEN_PURP_EVM, gen_purp_evm_dev_cfg, (1L << prof_sel));
}

static void setup_ind_auto_motor_ctrl_evm(void)
{
	u32 prof_sel = am335x_get_profile_selection();

	pr_info("The board is an industrial automation EVM in profile %d\n",
		prof_sel);

	/* Only Profile 0 is supported */
	if ((1L << prof_sel) != PROFILE_0) {
		pr_err("AM335X: Only Profile 0 is supported\n");
		pr_err("Assuming profile 0 & continuing\n");
		prof_sel = PROFILE_0;
	}

	_configure_device(IND_AUT_MTR_EVM, ind_auto_mtrl_evm_dev_cfg,
		PROFILE_0);

}

static void setup_ip_phone_evm(void)
{
	pr_info("The board is an IP phone EVM\n");

	_configure_device(IP_PHN_EVM, ip_phn_evm_dev_cfg, PROFILE_NONE);
}

static void pmic_read() {
	int val;
	val = i2c_smbus_read_byte_data(pmic_client, 0x0);
	printk("PMIC CHIP ID: %x\n", val);
}

static void setup_beaglebone(void)
{
	pr_info("The board is a AM335x Beaglebone.\n");

	pmic_read();

	_configure_device(LOW_COST_EVM, beaglebone_dev_cfg, PROFILE_NONE);
}

static void am335x_setup_daughter_board(struct memory_accessor *m, void *c)
{
	u8 tmp;
	int ret;

	/*
	 * try reading a byte from the EEPROM to see if it is
	 * present. We could read a lot more, but that would
	 * just slow the boot process and we have all the information
	 * we need from the EEPROM on the base board anyway.
	 */
	ret = m->read(m, &tmp, 0, sizeof(u8));
	if (ret == sizeof(u8)) {
		pr_info("Detected a daughter card on AM335x EVM..");
		daughter_brd_detected = true;
	} else {
		pr_info("No daughter card found\n");
		daughter_brd_detected = false;
	}
}

static void am335x_evm_setup(struct memory_accessor *mem_acc, void *context)
{
	int ret;
	char tmp[10];

	/* get board specific data */
	ret = mem_acc->read(mem_acc, (char *)&config, 0, sizeof(config));
	if (ret != sizeof(config)) {
		pr_warning("AM335X EVM config read fail, read %d bytes\n", ret);
		return;
	}

	if (config.header != AM335X_EEPROM_HEADER) {
		pr_warning("AM335X: wrong header 0x%x, expected 0x%x\n",
			config.header, AM335X_EEPROM_HEADER);
		goto out;
	}

	if (strncmp("A335", config.name, 4)) {
		pr_err("Board %s doesn't look like an AM335x board\n",
			config.name);
		goto out;
	}

	snprintf(tmp, sizeof(config.name), "%s", config.name);
	pr_info("Board name: %s\n", tmp);
	/* only 6 characters of options string used for now */
	snprintf(tmp, 7, "%s", config.opt);
	pr_info("SKU: %s\n", tmp);

	if (!strncmp("SKU#00", config.opt, 6))
		setup_low_cost_evm();
	else if (!strncmp("SKU#01", config.opt, 6))
		setup_general_purpose_evm();
	else if (!strncmp("SKU#02", config.opt, 6))
		setup_ind_auto_motor_ctrl_evm();
	else if (!strncmp("SKU#03", config.opt, 6))
		setup_ip_phone_evm();
	else
		goto out;

	/* Initialize cpsw after board detection is completed as board
	 * information is required for configuring phy address and hence
	 * should be call only after board detection
	 */
	am335x_cpsw_init();

	return;
out:
	/*
	 * for bring-up assume a full configuration, this should
	 * eventually be changed to assume a minimal configuration
	 */
	pr_err("Could not detect any board, falling back to: "
		"Beaglebone in profile 0 with no daughter card connected\n");
	daughter_brd_detected = false;
	setup_beaglebone();

	/* Initialize cpsw after board detection is completed as board
	 * information is required for configuring phy address and hence
	 * should be call only after board detection
	 */
	am335x_cpsw_init();

}

static struct at24_platform_data am335x_daughter_board_eeprom_info = {
	.byte_len       = (256*1024) / 8,
	.page_size      = 64,
	.flags          = AT24_FLAG_ADDR16,
	.setup          = am335x_setup_daughter_board,
	.context        = (void *)NULL,
};

static struct at24_platform_data am335x_baseboard_eeprom_info = {
	.byte_len       = (256*1024) / 8,
	.page_size      = 64,
	.flags          = AT24_FLAG_ADDR16,
	.setup          = am335x_evm_setup,
	.context        = (void *)NULL,
};

/*
* Daughter board Detection.
* Every board has a ID memory (EEPROM) on board. We probe these devices at
* machine init, starting from daughter board and ending with baseboard.
* Assumptions :
*	1. probe for i2c devices are called in the order they are included in
*	   the below struct. Daughter boards eeprom are probed 1st. Baseboard
*	   eeprom probe is called last.
*/
static struct i2c_board_info __initdata am335x_i2c_boardinfo[] = {
	{
		/* Daughter Board EEPROM */
		I2C_BOARD_INFO("24c256", DAUG_BOARD_I2C_ADDR),
		.platform_data  = &am335x_daughter_board_eeprom_info,
	},
	{
		/* Baseboard board EEPROM */
		I2C_BOARD_INFO("24c256", BASEBOARD_I2C_ADDR),
		.platform_data  = &am335x_baseboard_eeprom_info,
	},
	{
		I2C_BOARD_INFO("cpld_reg", 0x35),
	},
	{
		I2C_BOARD_INFO("tlc59108", 0x40),
	},
	{
		I2C_BOARD_INFO("tps65217", 0x24),
	},
};

static struct omap_musb_board_data musb_board_data = {
	.interface_type	= MUSB_INTERFACE_ULPI,
	.mode           = MUSB_OTG,
	.power		= 500,
	.instances	= 1,
};

static int cpld_reg_probe(struct i2c_client *client,
	    const struct i2c_device_id *id)
{
	cpld_client = client;
	return 0;
}

static int pmic_probe(struct i2c_client *client,
	    const struct i2c_device_id *id)
{
	pmic_client = client;
	return 0;
}

static int __devexit pmic_remove(struct i2c_client *client)
{
	pmic_client = NULL;
	return 0;
}

static int __devexit cpld_reg_remove(struct i2c_client *client)
{
	cpld_client = NULL;
	return 0;
}

static const struct i2c_device_id cpld_reg_id[] = {
	{ "cpld_reg", 0 },
	{ }
};

static struct i2c_driver cpld_reg_driver = {
	.driver = {
		.name	= "cpld_reg",
	},
	.probe		= cpld_reg_probe,
	.remove		= cpld_reg_remove,
	.id_table	= cpld_reg_id,
};

static const struct i2c_device_id tps65217_id[] = {
	{ "tps65217", 0 },
	{ }
};

static struct i2c_driver tps65217_pmic = {
	.driver = {
		.name	= "tps65217_pmic",
	},
	.probe		= pmic_probe,
	.remove		= pmic_remove,
	.id_table	= tps65217_id,
};


static void evm_init_cpld(void)
{
	i2c_add_driver(&cpld_reg_driver);
	i2c_add_driver(&tps65217_pmic);
}

static void __init am335x_evm_i2c_init(void)
{
	/* Initially assume Low Cost EVM Config */
	am335x_evm_id = LOW_COST_EVM;

	evm_init_cpld();

	omap_register_i2c_bus(1, 100, am335x_i2c_boardinfo,
				ARRAY_SIZE(am335x_i2c_boardinfo));
}

static void __init am335x_evm_init(void)
{
	am335x_mux_init(board_mux);
	omap_serial_init();
	am335x_evm_i2c_init();
	omap_sdrc_init(NULL, NULL);
	usb_musb_init(&musb_board_data);
	omap_board_config = am335x_evm_config;
	omap_board_config_size = ARRAY_SIZE(am335x_evm_config);
}

static void __init am335x_evm_map_io(void)
{
	omap2_set_globals_am33xx();
	omapam33xx_map_common_io();
}

MACHINE_START(AM335XEVM, "am335xevm")
	/* Maintainer: Texas Instruments */
	.boot_params	= 0x80000100,
	.map_io		= am335x_evm_map_io,
	.init_early	= am335x_init_early,
	.init_irq	= ti81xx_init_irq,
	.timer		= &omap3_am33xx_timer,
	.init_machine	= am335x_evm_init,
MACHINE_END
