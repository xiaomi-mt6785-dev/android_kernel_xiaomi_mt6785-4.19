/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2016 MediaTek Inc.
 */

/*
 * @file    mtk_clk_buf_hw.c
 * @brief   Driver for clock buffer control of each platform
 *
 */

#include <mtk_spm.h>
#include <mtk_clkbuf_ctl.h>
#include <mtk_clkbuf_common.h>
#if defined(CONFIG_MTK_UFS_SUPPORT)
#include "ufs-mtk.h"
#endif
#include <mt-plat/mtk_boot.h>
//#include <mt-plat/upmu_common.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/board_id.h>

static void __iomem *pwrap_base;

#define PWRAP_REG(ofs)		(pwrap_base + ofs)

/* PMICWRAP Reg */
#define DCXO_ENABLE		PWRAP_REG(0x190)
#define DCXO_CONN_ADR0		PWRAP_REG(0x194)
#define DCXO_CONN_WDATA0	PWRAP_REG(0x198)
#define DCXO_CONN_ADR1		PWRAP_REG(0x19C)
#define DCXO_CONN_WDATA1	PWRAP_REG(0x1A0)
#define DCXO_NFC_ADR0		PWRAP_REG(0x1A4)
#define DCXO_NFC_WDATA0		PWRAP_REG(0x1A8)
#define DCXO_NFC_ADR1		PWRAP_REG(0x1AC)
#define DCXO_NFC_WDATA1		PWRAP_REG(0x1B0)

#define PMIC_DCXO_CW00		MT6359_DCXO_CW00
#define PMIC_DCXO_CW00_SET	MT6359_DCXO_CW00_SET
#define PMIC_DCXO_CW00_CLR	MT6359_DCXO_CW00_CLR
#define PMIC_DCXO_CW01		MT6359_DCXO_CW01
#define PMIC_DCXO_CW02		MT6359_DCXO_CW02
#define PMIC_DCXO_CW03		MT6359_DCXO_CW03
#define PMIC_DCXO_CW04		MT6359_DCXO_CW04
#define PMIC_DCXO_CW05		MT6359_DCXO_CW05
#define PMIC_DCXO_CW06		MT6359_DCXO_CW06
#define PMIC_DCXO_CW07		MT6359_DCXO_CW07
#define PMIC_DCXO_CW08		MT6359_DCXO_CW08
#define PMIC_DCXO_CW09		MT6359_DCXO_CW09
#define PMIC_DCXO_CW09_SET	MT6359_DCXO_CW09_SET
#define PMIC_DCXO_CW09_CLR	MT6359_DCXO_CW09_CLR
#define PMIC_DCXO_CW10		MT6359_DCXO_CW10
#define PMIC_DCXO_CW11		MT6359_DCXO_CW11
#define PMIC_DCXO_CW12		MT6359_DCXO_CW12
#define PMIC_DCXO_CW13		MT6359_DCXO_CW13
#define PMIC_DCXO_CW14		MT6359_DCXO_CW14
#define PMIC_DCXO_CW15		MT6359_DCXO_CW15
#define PMIC_DCXO_CW16		MT6359_DCXO_CW16
#define PMIC_DCXO_CW17		MT6359_DCXO_CW17
#define PMIC_DCXO_CW18		MT6359_DCXO_CW18
#define PMIC_DCXO_CW19		MT6359_DCXO_CW19

#define DCXO_CONN_ENABLE	(0x1 << 1)
#define DCXO_NFC_ENABLE		(0x1 << 0)

#define PMIC_REG_MASK		0xFFFF
#define PMIC_REG_SHIFT		0

/* TODO: BBLPM HW mode */
#define XO_BB_LPM_HW		(0x1 << 0)
#define XO_BUF2_BBLPM_EN_MASK	(0x1 << 2)
#define XO_BUF3_BBLPM_EN_MASK	(0x1 << 3)
#define XO_BUF4_BBLPM_EN_MASK	(0x1 << 4)
#define XO_BUF7_BBLPM_EN_MASK	(0x1 << 7)

/* TODO: marked this after driver is ready */
/* #define CLKBUF_BRINGUP */

/* #define CLKBUF_CONN_SUPPORT_CTRL_FROM_I1 */

#define BUF_MAN_M				0
#define EN_BB_M					1
#define SIG_CTRL_M				2
#define CO_BUF_M				3

#define CLKBUF_STATUS_INFO_SIZE 2048

static unsigned int xo2_mode_set[4] = {WCN_EN_M,
			WCN_EN_BB_G,
			WCN_SRCLKEN_CONN,
			WCN_BUF24_EN};
static unsigned int xo3_mode_set[4] = {NFC_EN_M,
			NFC_EN_BB_G,
			NFC_CLK_SEL_G,
			NFC_BUF234_EN};
static unsigned int xo4_mode_set[4] = {CEL_EN_M,
			CEL_EN_BB_G,
			CEL_CLK_SEL_G,
			CEL_BUF24_EN};
static unsigned int xo7_mode_set[4] = {EXT_EN_M,
			EXT_EN_BB_G,
			EXT_CLK_SEL_G,
			EXT_BUF247_EN};

static unsigned int xo_mode_init[XO_NUMBER];

/* TODO: enable BBLPM if its function is ready (set as 1) */
/* #define CLK_BUF_HW_BBLPM_EN */
static unsigned int bblpm_switch = 2;

static unsigned int bblpm_cnt;

static unsigned int pwrap_dcxo_en_init;

static unsigned int clk_buf7_ctrl = true;

static unsigned int CLK_BUF1_STATUS = CLOCK_BUFFER_HW_CONTROL,
		    CLK_BUF2_STATUS = CLOCK_BUFFER_SW_CONTROL,
		    CLK_BUF3_STATUS = CLOCK_BUFFER_SW_CONTROL,
		    CLK_BUF4_STATUS = CLOCK_BUFFER_HW_CONTROL,
		    CLK_BUF5_STATUS = CLOCK_BUFFER_DISABLE,
		    CLK_BUF6_STATUS = CLOCK_BUFFER_DISABLE,
		    CLK_BUF7_STATUS = CLOCK_BUFFER_SW_CONTROL;

static unsigned int CLK_BUF1_OUTPUT_IMPEDANCE = CLK_BUF_OUTPUT_IMPEDANCE_6,
		    CLK_BUF2_OUTPUT_IMPEDANCE = CLK_BUF_OUTPUT_IMPEDANCE_4,
		    CLK_BUF3_OUTPUT_IMPEDANCE = CLK_BUF_OUTPUT_IMPEDANCE_6,
		    CLK_BUF4_OUTPUT_IMPEDANCE = CLK_BUF_OUTPUT_IMPEDANCE_4,
		    CLK_BUF5_OUTPUT_IMPEDANCE = CLK_BUF_OUTPUT_IMPEDANCE_0,
		    CLK_BUF6_OUTPUT_IMPEDANCE = CLK_BUF_OUTPUT_IMPEDANCE_0,
		    CLK_BUF7_OUTPUT_IMPEDANCE = CLK_BUF_OUTPUT_IMPEDANCE_6;

static unsigned int CLK_BUF1_CONTROLS_DESENSE = CLK_BUF_CONTROLS_FOR_DESENSE_0,
		    CLK_BUF2_CONTROLS_DESENSE = CLK_BUF_CONTROLS_FOR_DESENSE_4,
		    CLK_BUF3_CONTROLS_DESENSE = CLK_BUF_CONTROLS_FOR_DESENSE_0,
		    CLK_BUF4_CONTROLS_DESENSE = CLK_BUF_CONTROLS_FOR_DESENSE_4,
		    CLK_BUF5_CONTROLS_DESENSE = CLK_BUF_CONTROLS_FOR_DESENSE_0,
		    CLK_BUF6_CONTROLS_DESENSE = CLK_BUF_CONTROLS_FOR_DESENSE_0,
		    CLK_BUF7_CONTROLS_DESENSE = CLK_BUF_CONTROLS_FOR_DESENSE_0;

static u8 xo_en_stat[CLKBUF_NUM];
static u8 xo_bb_lpm_en_stat;
static u8 xo_bb_lpm_en_o;

#ifndef CLKBUF_BRINGUP
static enum CLK_BUF_SWCTRL_STATUS_T  pmic_clk_buf_swctrl[CLKBUF_NUM] = {
	CLK_BUF_SW_ENABLE,
	CLK_BUF_SW_DISABLE,
	CLK_BUF_SW_DISABLE,
	CLK_BUF_SW_ENABLE,
	CLK_BUF_SW_DISABLE,
	CLK_BUF_SW_DISABLE,
	CLK_BUF_SW_ENABLE
};
#else /* For Bring-up */
static enum CLK_BUF_SWCTRL_STATUS_T  pmic_clk_buf_swctrl[CLKBUF_NUM] = {
	CLK_BUF_SW_ENABLE,
	CLK_BUF_SW_ENABLE,
	CLK_BUF_SW_ENABLE,
	CLK_BUF_SW_ENABLE,
	CLK_BUF_SW_DISABLE,
	CLK_BUF_SW_ENABLE,
	CLK_BUF_SW_ENABLE
};
#endif

unsigned int __attribute__((weak))
	pmic_read_interface(unsigned int RegNum,
		unsigned int *val, unsigned int MASK, unsigned int SHIFT)
{
	return 0;
}

unsigned int __attribute__((weak))
	pmic_read_interface_nolock(unsigned int RegNum,
		unsigned int *val, unsigned int MASK, unsigned int SHIFT)
{
	return 0;
}

unsigned int __attribute__((weak))
	pmic_config_interface(unsigned int RegNum,
		unsigned int val, unsigned int MASK, unsigned int SHIFT)
{
	return 0;
}

unsigned int __attribute__((weak))
	pmic_config_interface_nolock(unsigned int RegNum,
		unsigned int val, unsigned int MASK, unsigned int SHIFT)
{
	return 0;
}

static void pmic_clk_buf_ctrl_ext(short on)
{
	if (on) {
		pmic_config_interface(PMIC_DCXO_CW09_SET_ADDR, 0x1,
				      PMIC_XO_EXTBUF7_EN_M_MASK,
				      PMIC_XO_EXTBUF7_EN_M_SHIFT);
		udelay(400);
	} else {
		pmic_config_interface(PMIC_DCXO_CW09_CLR_ADDR, 0x1,
				      PMIC_XO_EXTBUF7_EN_M_MASK,
				      PMIC_XO_EXTBUF7_EN_M_SHIFT);
	}
}

void clk_buf_ctrl_bblpm_hw(short on)
{
	u32 bblpm_sel = 0;

	if (!is_pmic_clkbuf)
		return;
	if (on) {
		pmic_config_interface(PMIC_XO_BB_LPM_EN_SEL_ADDR, 0x1,
				      PMIC_XO_BB_LPM_EN_SEL_MASK,
				      PMIC_XO_BB_LPM_EN_SEL_SHIFT);
	} else {
		pmic_config_interface(PMIC_XO_BB_LPM_EN_SEL_ADDR, 0x0,
				      PMIC_XO_BB_LPM_EN_SEL_MASK,
				      PMIC_XO_BB_LPM_EN_SEL_SHIFT);
	}

	pmic_read_interface_nolock(PMIC_XO_BB_LPM_EN_SEL_ADDR, &bblpm_sel,
			    PMIC_REG_MASK, PMIC_REG_SHIFT);
	clk_buf_pr_dbg("%s(%u): bblpm_sel=0x%x\n", __func__, (on ? 1 : 0),
		bblpm_sel);
}

void clk_buf_control_bblpm(bool on)
{
#ifdef CLKBUF_USE_BBLPM
#ifndef CONFIG_MTK_TINYSYS_SSPM_SUPPORT
	u32 cw00 = 0;

	if (!is_clkbuf_initiated || !is_pmic_clkbuf)
		return;

	if (on) /* FPM -> BBLPM */
		pmic_config_interface_nolock(PMIC_DCXO_CW00_SET_ADDR, 0x1,
				      PMIC_XO_BB_LPM_EN_M_MASK,
				      PMIC_XO_BB_LPM_EN_M_SHIFT);
	else /* BBLPM -> FPM */
		pmic_config_interface_nolock(PMIC_DCXO_CW00_CLR_ADDR, 0x1,
				      PMIC_XO_BB_LPM_EN_M_MASK,
				      PMIC_XO_BB_LPM_EN_M_SHIFT);

	pmic_read_interface_nolock(PMIC_DCXO_CW00, &cw00,
			    PMIC_REG_MASK, PMIC_REG_SHIFT);

	clk_buf_pr_dbg("%s(%u): CW00=0x%x\n", __func__, (on ? 1 : 0), cw00);
#endif
#endif
}

#ifdef CLKBUF_USE_BBLPM
static void clk_buf_ctrl_bblpm_mask(enum clk_buf_id id, bool onoff)
{
	if (!is_pmic_clkbuf && (bblpm_switch != 2))
		return;

	mutex_lock(&clk_buf_ctrl_lock);

	switch (id) {
	case CLK_BUF_BB_MD:
		pmic_config_interface(PMIC_XO_EXTBUF1_BBLPM_EN_MASK_ADDR,
			onoff, PMIC_XO_EXTBUF1_BBLPM_EN_MASK_MASK,
			PMIC_XO_EXTBUF1_BBLPM_EN_MASK_SHIFT);
		break;
	case CLK_BUF_CONN:
		pmic_config_interface(PMIC_XO_EXTBUF2_BBLPM_EN_MASK_ADDR,
			onoff, PMIC_XO_EXTBUF2_BBLPM_EN_MASK_MASK,
			PMIC_XO_EXTBUF2_BBLPM_EN_MASK_SHIFT);
		break;
	case CLK_BUF_NFC:
		pmic_config_interface(PMIC_XO_EXTBUF3_BBLPM_EN_MASK_ADDR,
			onoff, PMIC_XO_EXTBUF3_BBLPM_EN_MASK_MASK,
			PMIC_XO_EXTBUF3_BBLPM_EN_MASK_SHIFT);
		break;
	case CLK_BUF_RF:
		pmic_config_interface(PMIC_XO_EXTBUF4_BBLPM_EN_MASK_ADDR,
			onoff, PMIC_XO_EXTBUF4_BBLPM_EN_MASK_MASK,
			PMIC_XO_EXTBUF4_BBLPM_EN_MASK_SHIFT);
		break;
	case CLK_BUF_UFS:
		pmic_config_interface(PMIC_XO_EXTBUF7_BBLPM_EN_MASK_ADDR,
			onoff, PMIC_XO_EXTBUF7_BBLPM_EN_MASK_MASK,
			PMIC_XO_EXTBUF7_BBLPM_EN_MASK_SHIFT);
		break;
	default:
		pr_info("%s: id=%d isn't supported\n", __func__, id);
		break;
	}

	mutex_unlock(&clk_buf_ctrl_lock);
}
#endif

/*
 * Baseband Low Power Mode (BBLPM) for PMIC clkbuf
 * Condition: XO_CELL/XO_NFC/XO_WCN/XO_EXT OFF
 * Caller: deep idle, SODI2.5
 * Return: 0 if all conditions are matched & ready to enter BBLPM
 */
u32 clk_buf_bblpm_enter_cond(void)
{
	u32 bblpm_cond = 0;
#if defined(CONFIG_MTK_UFS_SUPPORT) && defined(CLKBUF_USE_BBLPM)
	int boot_type;
#endif

#ifdef CLKBUF_USE_BBLPM
	if (!is_clkbuf_initiated || !is_pmic_clkbuf || !bblpm_switch) {
		bblpm_cond |= BBLPM_COND_SKIP;
		return bblpm_cond;
	}


	/* if (pwr_sta & PWR_STATUS_MD) */
	if (!is_clk_buf_under_flightmode() &&
		(pmic_clk_buf_swctrl[XO_CEL] != CLK_BUF_SW_DISABLE))
		bblpm_cond |= BBLPM_COND_CEL;
	if (bblpm_switch != 2) {

		if ((pmic_clk_buf_swctrl[XO_WCN] == CLK_BUF_SW_ENABLE) ||
			(mtk_spm_read_register(SPM_PWRSTA) & PWR_STATUS_CONN))
			bblpm_cond |= BBLPM_COND_WCN;

		if (pmic_clk_buf_swctrl[XO_NFC] == CLK_BUF_SW_ENABLE)
			bblpm_cond |= BBLPM_COND_NFC;

#if defined(CONFIG_MTK_UFS_SUPPORT)
		boot_type = get_boot_type();
		if (boot_type == BOOTDEV_UFS) {
			if (ufs_mtk_deepidle_hibern8_check() < 0)
				bblpm_cond |= BBLPM_COND_EXT;
		}
#endif
	}
#else /* !CLKBUF_USE_BBLPM */
	bblpm_cond |= BBLPM_COND_SKIP;
#endif

	if (!bblpm_cond)
		bblpm_cnt++;

	return bblpm_cond;
}

static void clk_buf_ctrl_internal(enum clk_buf_id id, enum clk_buf_onff onoff)
{
	int pwrap_dcxo_en;

	if (!is_pmic_clkbuf)
		return;

	mutex_lock(&clk_buf_ctrl_lock);

	switch (id) {
	case CLK_BUF_CONN:
		if (onoff == CLK_BUF_FORCE_ON) {
			pwrap_dcxo_en =
				clkbuf_readl(DCXO_ENABLE) & ~DCXO_CONN_ENABLE;
			clkbuf_writel(DCXO_ENABLE, pwrap_dcxo_en);
			pmic_config_interface(PMIC_DCXO_CW00_CLR,
				PMIC_XO_EXTBUF2_MODE_MASK,
				PMIC_XO_EXTBUF2_MODE_MASK,
				PMIC_XO_EXTBUF2_MODE_SHIFT);
			pmic_config_interface(PMIC_DCXO_CW00_SET,
				PMIC_XO_EXTBUF2_EN_M_MASK,
				PMIC_XO_EXTBUF2_EN_M_MASK,
				PMIC_XO_EXTBUF2_EN_M_SHIFT);
			pmic_clk_buf_swctrl[XO_WCN] = 1;
		} else if (onoff == CLK_BUF_FORCE_OFF) {
			pwrap_dcxo_en =
				clkbuf_readl(DCXO_ENABLE) & ~DCXO_CONN_ENABLE;
			clkbuf_writel(DCXO_ENABLE, pwrap_dcxo_en);
			pmic_config_interface(PMIC_DCXO_CW00_CLR,
				PMIC_XO_EXTBUF2_MODE_MASK,
				PMIC_XO_EXTBUF2_MODE_MASK,
				PMIC_XO_EXTBUF2_MODE_SHIFT);
			pmic_config_interface(PMIC_DCXO_CW00_CLR,
				PMIC_XO_EXTBUF2_EN_M_MASK,
				PMIC_XO_EXTBUF2_EN_M_MASK,
				PMIC_XO_EXTBUF2_EN_M_SHIFT);
			pmic_clk_buf_swctrl[XO_WCN] = 0;
		} else if (onoff == CLK_BUF_INIT_SETTING) {
			pmic_config_interface(PMIC_XO_EXTBUF2_MODE_ADDR,
				xo_mode_init[XO_WCN],
				PMIC_XO_EXTBUF2_MODE_MASK,
				PMIC_XO_EXTBUF2_MODE_SHIFT);
			pwrap_dcxo_en =
				clkbuf_readl(DCXO_ENABLE) |
				(pwrap_dcxo_en_init & DCXO_CONN_ENABLE);
			clkbuf_writel(DCXO_ENABLE, pwrap_dcxo_en);
		}
		pr_info("%s: id=%d, onoff=%d, DCXO_ENABLE=0x%x\n",
			__func__, id, onoff, clkbuf_readl(DCXO_ENABLE));

		break;
	case CLK_BUF_NFC:
		if (onoff == CLK_BUF_FORCE_ON) {
			pwrap_dcxo_en =
				clkbuf_readl(DCXO_ENABLE) & ~DCXO_NFC_ENABLE;
			clkbuf_writel(DCXO_ENABLE, pwrap_dcxo_en);
			pmic_config_interface(PMIC_DCXO_CW00_CLR,
				      PMIC_XO_EXTBUF3_MODE_MASK,
				      PMIC_XO_EXTBUF3_MODE_MASK,
				      PMIC_XO_EXTBUF3_MODE_SHIFT);
			pmic_config_interface(PMIC_DCXO_CW00_SET,
				PMIC_XO_EXTBUF3_EN_M_MASK,
				PMIC_XO_EXTBUF3_EN_M_MASK,
				PMIC_XO_EXTBUF3_EN_M_SHIFT);
			pmic_clk_buf_swctrl[XO_NFC] = 1;
		} else if (onoff == CLK_BUF_FORCE_OFF) {
			pwrap_dcxo_en =
				clkbuf_readl(DCXO_ENABLE) & ~DCXO_NFC_ENABLE;
			clkbuf_writel(DCXO_ENABLE, pwrap_dcxo_en);
			pmic_config_interface(PMIC_DCXO_CW00_CLR,
				PMIC_XO_EXTBUF3_MODE_MASK,
				PMIC_XO_EXTBUF3_MODE_MASK,
				PMIC_XO_EXTBUF3_MODE_SHIFT);
			pmic_config_interface(PMIC_DCXO_CW00_CLR,
				PMIC_XO_EXTBUF3_EN_M_MASK,
				PMIC_XO_EXTBUF3_EN_M_MASK,
				PMIC_XO_EXTBUF3_EN_M_SHIFT);
			pmic_clk_buf_swctrl[XO_NFC] = 0;
		} else if (onoff == CLK_BUF_INIT_SETTING) {
			pmic_config_interface(PMIC_XO_EXTBUF3_MODE_ADDR,
				xo_mode_init[XO_NFC],
				PMIC_XO_EXTBUF3_MODE_MASK,
				PMIC_XO_EXTBUF3_MODE_SHIFT);
			pwrap_dcxo_en =
				clkbuf_readl(DCXO_ENABLE) |
				(pwrap_dcxo_en_init & DCXO_NFC_ENABLE);
			clkbuf_writel(DCXO_ENABLE, pwrap_dcxo_en);
		}
		pr_info("%s: id=%d, onoff=%d, DCXO_ENABLE=0x%x\n",
			__func__, id, onoff, clkbuf_readl(DCXO_ENABLE));
		break;
	case CLK_BUF_RF:
		if (onoff == CLK_BUF_FORCE_ON) {
			pmic_config_interface(PMIC_DCXO_CW00_CLR,
				      PMIC_XO_EXTBUF4_MODE_MASK,
				      PMIC_XO_EXTBUF4_MODE_MASK,
				      PMIC_XO_EXTBUF4_MODE_SHIFT);
			pmic_config_interface(PMIC_DCXO_CW00_SET,
				PMIC_XO_EXTBUF4_EN_M_MASK,
				PMIC_XO_EXTBUF4_EN_M_MASK,
				PMIC_XO_EXTBUF4_EN_M_SHIFT);
			pmic_clk_buf_swctrl[XO_CEL] = 1;
		} else if (onoff == CLK_BUF_FORCE_OFF) {
			pmic_config_interface(PMIC_DCXO_CW00_CLR,
				PMIC_XO_EXTBUF4_MODE_MASK,
				PMIC_XO_EXTBUF4_MODE_MASK,
				PMIC_XO_EXTBUF4_MODE_SHIFT);
			pmic_config_interface(PMIC_DCXO_CW00_CLR,
				PMIC_XO_EXTBUF4_EN_M_MASK,
				PMIC_XO_EXTBUF4_EN_M_MASK,
				PMIC_XO_EXTBUF4_EN_M_SHIFT);
			pmic_clk_buf_swctrl[XO_CEL] = 0;
		} else if (onoff == CLK_BUF_INIT_SETTING) {
			pmic_config_interface(PMIC_XO_EXTBUF4_MODE_ADDR,
				xo_mode_init[XO_CEL],
				PMIC_XO_EXTBUF4_MODE_MASK,
				PMIC_XO_EXTBUF4_MODE_SHIFT);
		}
		pr_info("%s: id=%d, onoff=%d\n", __func__, id, onoff);
		break;
	case CLK_BUF_UFS:
		if (onoff == CLK_BUF_FORCE_ON) {
			clk_buf7_ctrl = false;
			pmic_config_interface(PMIC_DCXO_CW09_CLR,
				      PMIC_XO_EXTBUF7_MODE_MASK,
				      PMIC_XO_EXTBUF7_MODE_MASK,
				      PMIC_XO_EXTBUF7_MODE_SHIFT);
			pmic_config_interface(PMIC_DCXO_CW09_SET,
				PMIC_XO_EXTBUF7_EN_M_MASK,
				PMIC_XO_EXTBUF7_EN_M_MASK,
				PMIC_XO_EXTBUF7_EN_M_SHIFT);
			pmic_clk_buf_swctrl[XO_EXT] = 1;
		} else if (onoff == CLK_BUF_FORCE_OFF) {
			clk_buf7_ctrl = false;
			pmic_config_interface(PMIC_DCXO_CW09_CLR,
				PMIC_XO_EXTBUF7_MODE_MASK,
				PMIC_XO_EXTBUF7_MODE_MASK,
				PMIC_XO_EXTBUF7_MODE_SHIFT);
			pmic_config_interface(PMIC_DCXO_CW09_CLR,
				PMIC_XO_EXTBUF7_EN_M_MASK,
				PMIC_XO_EXTBUF7_EN_M_MASK,
				PMIC_XO_EXTBUF7_EN_M_SHIFT);
			pmic_clk_buf_swctrl[XO_EXT] = 0;
		} else if (onoff == CLK_BUF_INIT_SETTING) {
			pmic_config_interface(PMIC_XO_EXTBUF7_MODE_ADDR,
				xo_mode_init[XO_EXT],
				PMIC_XO_EXTBUF7_MODE_MASK,
				PMIC_XO_EXTBUF7_MODE_SHIFT);
			clk_buf7_ctrl = true;
		}
		pr_info("%s: id=%d, onoff=%d\n", __func__, id, onoff);
		break;
	default:
		pr_info("%s: id=%d isn't supported\n", __func__, id);
		break;
	}

	mutex_unlock(&clk_buf_ctrl_lock);
}

static void pmic_clk_buf_ctrl(enum CLK_BUF_SWCTRL_STATUS_T *status)
{
	u32 pmic_cw00 = 0, pmic_cw09 = 0;

	if (!is_clkbuf_initiated)
		return;

	clk_buf_ctrl_internal(CLK_BUF_CONN, status[XO_WCN] % 3);
	clk_buf_ctrl_internal(CLK_BUF_NFC, status[XO_NFC] % 3);
	clk_buf_ctrl_internal(CLK_BUF_RF, status[XO_CEL] % 3);
	clk_buf_ctrl_internal(CLK_BUF_UFS, status[XO_EXT] % 3);

	pmic_read_interface(PMIC_DCXO_CW00, &pmic_cw00,
		PMIC_REG_MASK, PMIC_REG_SHIFT);
	pmic_read_interface(PMIC_DCXO_CW09, &pmic_cw09,
		PMIC_REG_MASK, PMIC_REG_SHIFT);
	pr_info("%s DCXO_CW00=0x%x, CW09=0x%x, clk_buf_swctrl=[%u %u %u %u 0 0 %u]\n",
		__func__, pmic_cw00, pmic_cw09, status[XO_SOC], status[XO_WCN],
		status[XO_NFC], status[XO_CEL], status[XO_EXT]);
}

static int clk_buf_mode_set(enum clk_buf_id id)
{
	unsigned int val = 0;
	int ret = 0;

	switch (id) {
	case CLK_BUF_BB_MD:
		break;
	case CLK_BUF_CONN:
		pmic_read_interface(PMIC_XO_EXTBUF2_MODE_ADDR,
			&val,
			PMIC_XO_EXTBUF2_MODE_MASK,
			PMIC_XO_EXTBUF2_MODE_SHIFT);
#ifdef CLKBUF_CONN_SUPPORT_CTRL_FROM_I1
		ret = val - BUF_MAN_M;
#else
		ret = val - SIG_CTRL_M;
#endif
		break;
	case CLK_BUF_NFC:
		pmic_read_interface(PMIC_XO_EXTBUF3_MODE_ADDR,
				&val,
				PMIC_XO_EXTBUF3_MODE_MASK,
				PMIC_XO_EXTBUF3_MODE_SHIFT);
		ret = val - BUF_MAN_M;
		break;
	case CLK_BUF_RF:
		pmic_read_interface(PMIC_XO_EXTBUF4_MODE_ADDR,
				&val,
				PMIC_XO_EXTBUF4_MODE_MASK,
				PMIC_XO_EXTBUF4_MODE_SHIFT);
		ret = val - SIG_CTRL_M;
		break;
	case CLK_BUF_UFS:
		pmic_read_interface(PMIC_XO_EXTBUF7_MODE_ADDR,
				&val,
				PMIC_XO_EXTBUF7_MODE_MASK,
				PMIC_XO_EXTBUF7_MODE_SHIFT);
		ret = val - BUF_MAN_M;
		break;
	default:
		ret = -5;
		pr_info("%s: id=%d isn't supported\n", __func__, id);
		break;
	}

	return ret;
}

bool clk_buf_ctrl_combine(enum clk_buf_id id, bool onoff)
{
	short ret = 0, no_lock = 0;
	int val = 0;

	if (!is_clkbuf_initiated)
		return false;

	if (!is_pmic_clkbuf)
		return false;

	clk_buf_pr_dbg("%s: id=%d, onoff=%d, clkbuf_ctrl_stat=0x%x\n",
		__func__, id, onoff, clkbuf_ctrl_stat);

	if (preempt_count() > 0 || irqs_disabled() ||
		system_state != SYSTEM_RUNNING || oops_in_progress)
		no_lock = 1;

	if (!no_lock)
		mutex_lock(&clk_buf_ctrl_lock);

	switch (id) {
	case CLK_BUF_BB_MD:
		if (CLK_BUF1_STATUS != CLOCK_BUFFER_SW_CONTROL) {
			ret = -1;
			pr_info("%s: id=%d isn't controlled by SW\n",
				__func__, id);
			break;
		}
		break;
	case CLK_BUF_CONN:
		if (onoff)
			pmic_config_interface(PMIC_DCXO_CW00_SET,
			xo2_mode_set[CO_BUF_M],
			PMIC_XO_EXTBUF2_MODE_MASK,
			PMIC_XO_EXTBUF2_MODE_SHIFT);
		else {
			val = clk_buf_mode_set(CLK_BUF_CONN);
			if (val > 0)
				pmic_config_interface(PMIC_DCXO_CW00_CLR,
					val,
					PMIC_XO_EXTBUF2_MODE_MASK,
					PMIC_XO_EXTBUF2_MODE_SHIFT);
			else if (val < 0) {
				val = 0 - val;
				pmic_config_interface(PMIC_DCXO_CW00_SET,
					val,
					PMIC_XO_EXTBUF2_MODE_MASK,
					PMIC_XO_EXTBUF2_MODE_SHIFT);
			}
		}
		break;
	case CLK_BUF_NFC:
		if (onoff)
			pmic_config_interface(PMIC_DCXO_CW00_SET,
				xo3_mode_set[CO_BUF_M],
				PMIC_XO_EXTBUF3_MODE_MASK,
				PMIC_XO_EXTBUF3_MODE_SHIFT);
		else {
			val = clk_buf_mode_set(CLK_BUF_NFC);
			if (val > 0)
				pmic_config_interface(PMIC_DCXO_CW00_CLR,
					val,
					PMIC_XO_EXTBUF3_MODE_MASK,
					PMIC_XO_EXTBUF3_MODE_SHIFT);
			else if (val < 0) {
				val = 0 - val;
				pmic_config_interface(PMIC_DCXO_CW00_SET,
					val,
					PMIC_XO_EXTBUF3_MODE_MASK,
					PMIC_XO_EXTBUF3_MODE_SHIFT);
			}
		}
		break;
	case CLK_BUF_RF:
		if (onoff)
			pmic_config_interface(PMIC_DCXO_CW00_SET,
				xo4_mode_set[CO_BUF_M],
				PMIC_XO_EXTBUF4_MODE_MASK,
				PMIC_XO_EXTBUF4_MODE_SHIFT);
		else {
			val = clk_buf_mode_set(CLK_BUF_RF);
			if (val > 0)
				pmic_config_interface(PMIC_DCXO_CW00_CLR,
					val,
					PMIC_XO_EXTBUF4_MODE_MASK,
					PMIC_XO_EXTBUF4_MODE_SHIFT);
			else if (val < 0) {
				val = 0 - val;
				pr_info("%s val = %d\n", __func__, val);
				pmic_config_interface(PMIC_DCXO_CW00_SET,
					val,
					PMIC_XO_EXTBUF4_MODE_MASK,
					PMIC_XO_EXTBUF4_MODE_SHIFT);
			}
		}
		break;
	case CLK_BUF_UFS:
		if (onoff)
			pmic_config_interface(PMIC_DCXO_CW09_SET,
					xo7_mode_set[CO_BUF_M],
					PMIC_XO_EXTBUF7_MODE_MASK,
					PMIC_XO_EXTBUF7_MODE_SHIFT);
		else {
			val = clk_buf_mode_set(CLK_BUF_UFS);
			if (val > 0)
				pmic_config_interface(PMIC_DCXO_CW09_CLR,
					val,
					PMIC_XO_EXTBUF7_MODE_MASK,
					PMIC_XO_EXTBUF7_MODE_SHIFT);
			else if (val < 0) {
				val = 0 - val;
				pmic_config_interface(PMIC_DCXO_CW09_SET,
					val,
					PMIC_XO_EXTBUF7_MODE_MASK,
					PMIC_XO_EXTBUF7_MODE_SHIFT);
			}
		}
		break;
	default:
		ret = -1;
		pr_info("%s: id=%d isn't supported\n", __func__, id);
		break;
	}

	if (!no_lock)
		mutex_unlock(&clk_buf_ctrl_lock);

	if (ret)
		return false;
	else
		return true;
}
EXPORT_SYMBOL(clk_buf_ctrl_combine);

bool clk_buf_ctrl(enum clk_buf_id id, bool onoff)
{
	short ret = 0, no_lock = 0;

	if (!is_clkbuf_initiated)
		return false;

	if (!is_pmic_clkbuf)
		return false;

	clk_buf_pr_dbg("%s: id=%d, onoff=%d, clkbuf_ctrl_stat=0x%x\n",
		__func__, id, onoff, clkbuf_ctrl_stat);

	if (preempt_count() > 0 || irqs_disabled()
		|| system_state != SYSTEM_RUNNING || oops_in_progress)
		no_lock = 1;

	if (!no_lock)
		mutex_lock(&clk_buf_ctrl_lock);

	switch (id) {
	case CLK_BUF_BB_MD:
		if (CLK_BUF1_STATUS != CLOCK_BUFFER_SW_CONTROL) {
			ret = -1;
			pr_info("%s: id=%d isn't controlled by SW\n",
				__func__, id);
			break;
		}
		break;
	case CLK_BUF_CONN:
		if (CLK_BUF2_STATUS != CLOCK_BUFFER_SW_CONTROL) {
			ret = -1;
			pr_info("%s: id=%d isn't controlled by SW\n",
				__func__, id);
			break;
		}
		/* record the status of CONN from caller for checking BBLPM */
		pmic_clk_buf_swctrl[XO_WCN] = onoff;
		break;
	case CLK_BUF_NFC:
		if (CLK_BUF3_STATUS != CLOCK_BUFFER_SW_CONTROL) {
			ret = -1;
			pr_info("%s: id=%d isn't controlled by SW\n",
				__func__, id);
			break;
		}
		/* record the status of NFC from caller for checking BBLPM */
		pmic_clk_buf_swctrl[XO_NFC] = onoff;
		break;
	case CLK_BUF_RF:
		if (CLK_BUF4_STATUS != CLOCK_BUFFER_SW_CONTROL) {
			ret = -1;
			pr_info("%s: id=%d isn't controlled by SW\n",
				__func__, id);
			break;
		}
		break;
	case CLK_BUF_UFS:
		if ((CLK_BUF7_STATUS != CLOCK_BUFFER_SW_CONTROL) ||
			(clk_buf7_ctrl != true)) {
			ret = -1;
			pr_info("%s: id=%d isn't controlled by SW\n",
				__func__, id);
			break;
		}
		pmic_clk_buf_ctrl_ext(onoff);
		pmic_clk_buf_swctrl[XO_EXT] = onoff;
		break;
	default:
		ret = -1;
		pr_info("%s: id=%d isn't supported\n", __func__, id);
		break;
	}

	if (!no_lock)
		mutex_unlock(&clk_buf_ctrl_lock);

	if (ret)
		return false;
	else
		return true;
}
EXPORT_SYMBOL(clk_buf_ctrl);

void clk_buf_disp_ctrl(bool onoff)
{
	if (onoff) {
		pmic_config_interface(PMIC_DCXO_CW00_CLR,
			      PMIC_XO_EXTBUF3_MODE_MASK,
			      PMIC_XO_EXTBUF3_MODE_MASK,
			      PMIC_XO_EXTBUF3_MODE_SHIFT);
		pmic_config_interface(PMIC_DCXO_CW00_SET,
			PMIC_XO_EXTBUF3_EN_M_MASK,
			PMIC_XO_EXTBUF3_EN_M_MASK,
			PMIC_XO_EXTBUF3_EN_M_SHIFT);
		pmic_clk_buf_swctrl[XO_NFC] = 1;
	} else {
		pmic_config_interface(PMIC_DCXO_CW00_CLR,
			PMIC_XO_EXTBUF3_MODE_MASK,
			PMIC_XO_EXTBUF3_MODE_MASK,
			PMIC_XO_EXTBUF3_MODE_SHIFT);
		pmic_config_interface(PMIC_DCXO_CW00_CLR,
			PMIC_XO_EXTBUF3_EN_M_MASK,
			PMIC_XO_EXTBUF3_EN_M_MASK,
			PMIC_XO_EXTBUF3_EN_M_SHIFT);
		pmic_clk_buf_swctrl[XO_NFC] = 0;
	}
}
EXPORT_SYMBOL(clk_buf_disp_ctrl);

void clk_buf_dump_dts_log(void)
{
	pr_info("%s: CLK_BUF?_STATUS=%d %d %d %d %d %d %d\n", __func__,
		     CLK_BUF1_STATUS, CLK_BUF2_STATUS,
		     CLK_BUF3_STATUS, CLK_BUF4_STATUS,
		     CLK_BUF5_STATUS, CLK_BUF6_STATUS,
		     CLK_BUF7_STATUS);
	pr_info("%s: CLK_BUF?_OUTPUT_IMPEDANCE=%d %d %d %d %d %d %d\n",
		     __func__,
		     CLK_BUF1_OUTPUT_IMPEDANCE,
		     CLK_BUF2_OUTPUT_IMPEDANCE,
		     CLK_BUF3_OUTPUT_IMPEDANCE,
		     CLK_BUF4_OUTPUT_IMPEDANCE,
		     CLK_BUF5_OUTPUT_IMPEDANCE,
		     CLK_BUF6_OUTPUT_IMPEDANCE,
		     CLK_BUF7_OUTPUT_IMPEDANCE);
	pr_info("%s: CLK_BUF?_CONTROLS_DESENSE=%d %d %d %d %d %d %d\n",
		     __func__,
		     CLK_BUF1_CONTROLS_DESENSE,
		     CLK_BUF2_CONTROLS_DESENSE,
		     CLK_BUF3_CONTROLS_DESENSE,
		     CLK_BUF4_CONTROLS_DESENSE,
		     CLK_BUF5_CONTROLS_DESENSE,
		     CLK_BUF6_CONTROLS_DESENSE,
		     CLK_BUF7_CONTROLS_DESENSE);
}

void clk_buf_dump_clkbuf_log(void)
{
	u32 pmic_cw00 = 0, pmic_cw09 = 0, pmic_cw12 = 0, pmic_cw13 = 0,
	    pmic_cw15 = 0, pmic_cw19 = 0, top_spi_con1 = 0,
	    ldo_vrfck_op = 0, ldo_vbbck_op = 0, ldo_vrfck_en = 0,
	    ldo_vbbck_en = 0;

	pmic_read_interface(PMIC_XO_EXTBUF1_MODE_ADDR, &pmic_cw00,
		PMIC_REG_MASK, PMIC_REG_SHIFT);
	pmic_read_interface(PMIC_XO_EXTBUF7_MODE_ADDR, &pmic_cw09,
		PMIC_REG_MASK, PMIC_REG_SHIFT);
	pmic_read_interface(PMIC_XO_EXTBUF2_CLKSEL_MAN_ADDR, &pmic_cw12,
		PMIC_REG_MASK, PMIC_REG_SHIFT);
	pmic_read_interface(PMIC_RG_XO_EXTBUF2_SRSEL_ADDR, &pmic_cw13,
		PMIC_REG_MASK, PMIC_REG_SHIFT);
	pmic_read_interface(PMIC_RG_XO_RESERVED1_ADDR, &pmic_cw15,
		PMIC_REG_MASK, PMIC_REG_SHIFT);
	pmic_read_interface(PMIC_RG_XO_EXTBUF2_RSEL_ADDR, &pmic_cw19,
		PMIC_REG_MASK, PMIC_REG_SHIFT);
	pmic_read_interface(PMIC_RG_SRCLKEN_IN3_EN_ADDR, &top_spi_con1,
		PMIC_RG_SRCLKEN_IN3_EN_MASK, PMIC_RG_SRCLKEN_IN3_EN_SHIFT);
	pmic_read_interface(PMIC_RG_LDO_VRFCK_HW14_OP_EN_ADDR, &ldo_vrfck_op,
		PMIC_RG_LDO_VRFCK_HW14_OP_EN_MASK,
		PMIC_RG_LDO_VRFCK_HW14_OP_EN_SHIFT);
	pmic_read_interface(PMIC_RG_LDO_VBBCK_HW14_OP_EN_ADDR, &ldo_vbbck_op,
		PMIC_RG_LDO_VBBCK_HW14_OP_EN_MASK,
		PMIC_RG_LDO_VBBCK_HW14_OP_EN_SHIFT);
	pmic_read_interface(PMIC_RG_LDO_VRFCK_EN_ADDR, &ldo_vrfck_en,
		PMIC_RG_LDO_VRFCK_EN_MASK, PMIC_RG_LDO_VRFCK_EN_SHIFT);
	pmic_read_interface(PMIC_RG_LDO_VBBCK_EN_ADDR, &ldo_vbbck_en,
		 PMIC_RG_LDO_VBBCK_EN_MASK, PMIC_RG_LDO_VBBCK_EN_SHIFT);
	pr_info("%s DCXO_CW00/09/12/13/15/19=0x%x %x %x %x %x %x\n",
		     __func__, pmic_cw00, pmic_cw09, pmic_cw12, pmic_cw13,
		     pmic_cw15, pmic_cw19);
	pr_info("%s in3_en/rf_op/bb_op/rf_en/bb_en=0x%x %x %x %x %x\n",
		     __func__, top_spi_con1, ldo_vrfck_op, ldo_vbbck_op,
		     ldo_vrfck_en, ldo_vbbck_en);
}

static u32 dcxo_dbg_read_auxout(u16 sel)
{
	u32 rg_auxout = 0;

	pmic_config_interface(PMIC_XO_STATIC_AUXOUT_SEL_ADDR, sel,
			      PMIC_XO_STATIC_AUXOUT_SEL_MASK,
			      PMIC_XO_STATIC_AUXOUT_SEL_SHIFT);
	pmic_read_interface(PMIC_XO_STATIC_AUXOUT_ADDR, &rg_auxout,
			    PMIC_XO_STATIC_AUXOUT_MASK,
			    PMIC_XO_STATIC_AUXOUT_SHIFT);
	clk_buf_pr_dbg("%s: sel=%d, rg_auxout=0x%x\n",
		__func__, sel, rg_auxout);

	return rg_auxout;
}

static void clk_buf_get_xo_en(void)
{
	u32 rg_auxout = 0;

	rg_auxout = dcxo_dbg_read_auxout(6);
	clk_buf_pr_dbg("%s: sel io_dbg5: rg_auxout=0x%x\n",
		__func__, rg_auxout);
	xo_en_stat[XO_SOC] = (rg_auxout & (0x1 << 13)) >> 13;
	xo_en_stat[XO_WCN] = (rg_auxout & (0x1 << 11)) >> 11;
	xo_en_stat[XO_NFC] = (rg_auxout & (0x1 << 9)) >> 9;
	xo_en_stat[XO_CEL] = (rg_auxout & (0x1 << 7)) >> 7;
	xo_en_stat[XO_PD] = (rg_auxout & (0x1 << 5)) >> 5;
	xo_en_stat[XO_EXT] = (rg_auxout & (0x1 << 3)) >> 3;

	pr_info("%s: EN_STAT=%d %d %d %d %d %d\n",
		__func__,
		xo_en_stat[XO_SOC],
		xo_en_stat[XO_WCN],
		xo_en_stat[XO_NFC],
		xo_en_stat[XO_CEL],
		xo_en_stat[XO_PD],
		xo_en_stat[XO_EXT]);
}

static void clk_buf_get_bblpm_en(void)
{
	u32 rg_auxout = 0;

	rg_auxout = dcxo_dbg_read_auxout(27);
	clk_buf_pr_dbg("%s: sel ctrl_dbg7: rg_auxout=0x%x\n",
		__func__, rg_auxout);

	xo_bb_lpm_en_stat = (rg_auxout & (0x1 << 0)) >> 0;
	rg_auxout = dcxo_dbg_read_auxout(39);
	clk_buf_pr_dbg("%s: sel ctrl_dbg719: rg_auxout=0x%x\n",
		__func__, rg_auxout);
	xo_bb_lpm_en_o = (rg_auxout & (0x1 << 15)) >> 15;

	pr_info("%s: bblpm %d %d\n",
		__func__,
		xo_bb_lpm_en_stat,
		xo_bb_lpm_en_o);
}

void clk_buf_get_aux_out(void)
{
	clk_buf_get_xo_en();
	clk_buf_get_bblpm_en();
}

int clk_buf_ctrl_bblpm_sw(bool enable)
{
	clk_buf_ctrl_bblpm_hw(false);
	if (enable)
		pmic_config_interface(PMIC_DCXO_CW00_SET,
				0x1 << 12,
				PMIC_REG_MASK,
				PMIC_REG_SHIFT);
	else
		pmic_config_interface(PMIC_DCXO_CW00_CLR,
				0x1 << 12,
				PMIC_REG_MASK,
				PMIC_REG_SHIFT);

	clk_buf_get_bblpm_en();

	return 0;
}

static ssize_t clk_buf_show_status_info_internal(char *buf)
{
	int len = 0;
	u32 pmic_cw00 = 0, pmic_cw09 = 0, pmic_cw12 = 0, pmic_cw13 = 0,
	    pmic_cw15 = 0, pmic_cw19 = 0;
	u32 top_spi_con1 = 0, ldo_vrfck_op_en = 0, ldo_vrfck_en = 0,
		ldo_vbbck_op_en = 0, ldo_vbbck_en = 0;
	u32 buf2_mode, buf3_mode, buf4_mode, buf6_mode, buf7_mode;
	u32 buf2_en_m, buf3_en_m, buf4_en_m, buf6_en_m, buf7_en_m;

	clk_buf_get_xo_en();

	len += snprintf(buf+len, CLKBUF_STATUS_INFO_SIZE-len,
			"********** PMIC clock buffer state (%s) **********\n",
			(is_pmic_clkbuf ? "on" : "off"));
	len += snprintf(buf+len, CLKBUF_STATUS_INFO_SIZE-len,
			"XO_SOC   SW(1)/HW(2) CTL: %d, Dis(0)/En(1): %d, RS: %u\n",
			CLK_BUF1_STATUS, pmic_clk_buf_swctrl[XO_SOC],
			xo_en_stat[XO_SOC]);
	len += snprintf(buf+len, CLKBUF_STATUS_INFO_SIZE-len,
			"XO_WCN   SW(1)/HW(2) CTL: %d, Dis(0)/En(1): %d, RS: %u\n",
			CLK_BUF2_STATUS, pmic_clk_buf_swctrl[XO_WCN],
			xo_en_stat[XO_WCN]);
	len += snprintf(buf+len, CLKBUF_STATUS_INFO_SIZE-len,
			"XO_NFC   SW(1)/HW(2) CTL: %d, Dis(0)/En(1): %d, RS: %u\n",
			CLK_BUF3_STATUS, pmic_clk_buf_swctrl[XO_NFC],
			xo_en_stat[XO_NFC]);
	len += snprintf(buf+len, CLKBUF_STATUS_INFO_SIZE-len,
			"XO_CEL   SW(1)/HW(2) CTL: %d, Dis(0)/En(1): %d, RS: %u\n",
			CLK_BUF4_STATUS, pmic_clk_buf_swctrl[XO_CEL],
			xo_en_stat[XO_CEL]);
	len += snprintf(buf+len, CLKBUF_STATUS_INFO_SIZE-len,
			"XO_AUD   SW(1)/HW(2) CTL: %d, Dis(0)/En(1): %d, RS: %u\n",
			CLK_BUF5_STATUS, pmic_clk_buf_swctrl[XO_AUD],
			xo_en_stat[XO_AUD]);
	len += snprintf(buf+len, CLKBUF_STATUS_INFO_SIZE-len,
			"XO_PD    SW(1)/HW(2) CTL: %d, Dis(0)/En(1): %d, RS: %u\n",
			CLK_BUF6_STATUS, pmic_clk_buf_swctrl[XO_PD],
			xo_en_stat[XO_PD]);
	len += snprintf(buf+len, CLKBUF_STATUS_INFO_SIZE-len,
			"XO_EXT   SW(1)/HW(2) CTL: %d, Dis(0)/En(1): %d, RS: %u\n",
			CLK_BUF7_STATUS, pmic_clk_buf_swctrl[XO_EXT],
			xo_en_stat[XO_EXT]);
	len += snprintf(buf+len, CLKBUF_STATUS_INFO_SIZE-len,
			".********** clock buffer debug info **********\n");
	len += snprintf(buf+len, CLKBUF_STATUS_INFO_SIZE-len,
		"clkbuf_ctrl_stat=0x%x\n",
		clkbuf_ctrl_stat);

	pmic_read_interface_nolock(PMIC_DCXO_CW00, &pmic_cw00,
			    PMIC_REG_MASK, PMIC_REG_SHIFT);
	pmic_read_interface_nolock(PMIC_DCXO_CW09, &pmic_cw09,
			    PMIC_REG_MASK, PMIC_REG_SHIFT);
	pmic_read_interface_nolock(PMIC_DCXO_CW12, &pmic_cw12,
			    PMIC_REG_MASK, PMIC_REG_SHIFT);
	pmic_read_interface_nolock(PMIC_DCXO_CW13, &pmic_cw13,
			    PMIC_REG_MASK, PMIC_REG_SHIFT);
	pmic_read_interface_nolock(PMIC_DCXO_CW15, &pmic_cw15,
			    PMIC_REG_MASK, PMIC_REG_SHIFT);
	pmic_read_interface_nolock(PMIC_DCXO_CW19, &pmic_cw19,
			    PMIC_REG_MASK, PMIC_REG_SHIFT);
	pmic_read_interface_nolock(PMIC_RG_LDO_VRFCK_HW14_OP_EN_ADDR,
			    &ldo_vrfck_op_en,
			    PMIC_RG_LDO_VRFCK_HW14_OP_EN_MASK,
			    PMIC_RG_LDO_VRFCK_HW14_OP_EN_SHIFT);
	pmic_read_interface_nolock(PMIC_RG_LDO_VBBCK_HW14_OP_EN_ADDR,
			    &ldo_vbbck_op_en,
			    PMIC_RG_LDO_VBBCK_HW14_OP_EN_MASK,
			    PMIC_RG_LDO_VBBCK_HW14_OP_EN_SHIFT);
	pmic_read_interface_nolock(PMIC_RG_LDO_VRFCK_EN_ADDR, &ldo_vrfck_en,
			    PMIC_RG_LDO_VRFCK_EN_MASK,
			    PMIC_RG_LDO_VRFCK_EN_SHIFT);
	pmic_read_interface_nolock(PMIC_RG_LDO_VBBCK_EN_ADDR, &ldo_vbbck_en,
			    PMIC_RG_LDO_VBBCK_EN_MASK,
			    PMIC_RG_LDO_VBBCK_EN_SHIFT);
	len += snprintf(buf+len, CLKBUF_STATUS_INFO_SIZE-len,
		"DCXO_CW00/09/12/13/15/19=0x%x %x %x %x %x %x\n",
		pmic_cw00, pmic_cw09, pmic_cw12, pmic_cw13, pmic_cw15,
		pmic_cw19);
	len += snprintf(buf+len, CLKBUF_STATUS_INFO_SIZE-len,
		"LDO vrfck_op/en=%x %x vbb_en/ldo_bb_en=%x %x\n",
		ldo_vrfck_op_en, ldo_vrfck_en, ldo_vbbck_op_en, ldo_vbbck_en);

	buf2_mode = (pmic_cw00 >> PMIC_XO_EXTBUF2_MODE_SHIFT)
		& PMIC_XO_EXTBUF2_MODE_MASK;
	buf3_mode = (pmic_cw00 >> PMIC_XO_EXTBUF3_MODE_SHIFT)
		& PMIC_XO_EXTBUF3_MODE_MASK;
	buf4_mode = (pmic_cw00 >> PMIC_XO_EXTBUF4_MODE_SHIFT)
		& PMIC_XO_EXTBUF4_MODE_MASK;
	buf6_mode = (pmic_cw09 >> PMIC_XO_EXTBUF6_MODE_SHIFT)
		& PMIC_XO_EXTBUF6_MODE_MASK;
	buf7_mode = (pmic_cw09 >> PMIC_XO_EXTBUF7_MODE_SHIFT)
		& PMIC_XO_EXTBUF7_MODE_MASK;
	buf2_en_m = (pmic_cw00 >> PMIC_XO_EXTBUF2_EN_M_SHIFT)
		& PMIC_XO_EXTBUF2_EN_M_MASK;
	buf3_en_m = (pmic_cw00 >> PMIC_XO_EXTBUF3_EN_M_SHIFT)
		& PMIC_XO_EXTBUF3_EN_M_MASK;
	buf4_en_m = (pmic_cw00 >> PMIC_XO_EXTBUF4_EN_M_SHIFT)
		& PMIC_XO_EXTBUF4_EN_M_MASK;
	buf6_en_m = (pmic_cw09 >> PMIC_XO_EXTBUF6_EN_M_SHIFT)
		& PMIC_XO_EXTBUF6_EN_M_MASK;
	buf7_en_m = (pmic_cw09 >> PMIC_XO_EXTBUF7_EN_M_SHIFT)
		& PMIC_XO_EXTBUF7_EN_M_MASK;
	len += snprintf(buf+len, CLKBUF_STATUS_INFO_SIZE-len,
		"buf2/3/4/6/7 mode=%d/%d/%d/%d/%d, buf2/3/4/6/7 en_m=%d/%d/%d/%d/%d\n",
		buf2_mode, buf3_mode, buf4_mode, buf6_mode, buf7_mode,
		buf2_en_m, buf3_en_m, buf4_en_m, buf6_en_m, buf7_en_m);
	pmic_read_interface_nolock(PMIC_RG_SRCLKEN_IN3_EN_ADDR, &top_spi_con1,
			    PMIC_REG_MASK, PMIC_REG_SHIFT);
	len += snprintf(buf+len, CLKBUF_STATUS_INFO_SIZE-len,
		"SRCLKEN_IN3_EN(srclken_conn)=0x%x\n", top_spi_con1);

	len += snprintf(buf+len, CLKBUF_STATUS_INFO_SIZE-len,
		"DCXO_CONN_ADR0/WDATA0/ADR1/WDATA1=0x%x %x %x %x\n",
		clkbuf_readl(DCXO_CONN_ADR0),
		clkbuf_readl(DCXO_CONN_WDATA0),
		clkbuf_readl(DCXO_CONN_ADR1),
		clkbuf_readl(DCXO_CONN_WDATA1));
	len += snprintf(buf+len, CLKBUF_STATUS_INFO_SIZE-len,
		"DCXO_NFC_ADR0/WDATA0/ADR1/WDATA1/EN=0x%x %x %x %x %x\n",
		clkbuf_readl(DCXO_NFC_ADR0),
		clkbuf_readl(DCXO_NFC_WDATA0),
		clkbuf_readl(DCXO_NFC_ADR1),
		clkbuf_readl(DCXO_NFC_WDATA1),
		clkbuf_readl(DCXO_ENABLE));

	len += snprintf(buf+len, CLKBUF_STATUS_INFO_SIZE-len,
		"bblpm_switch=%u, bblpm_cnt=%u, bblpm_cond=0x%x\n",
		bblpm_switch, bblpm_cnt, clk_buf_bblpm_enter_cond());

	len += snprintf(buf+len, CLKBUF_STATUS_INFO_SIZE-len,
			"MD1_PWR_CON=0x%x, PWR_STATUS=0x%x, PCM_REG13_DATA=0x%x,",
			mtk_spm_read_register(SPM_MD1_PWR_CON),
			mtk_spm_read_register(SPM_PWRSTA),
			mtk_spm_read_register(SPM_REG13));
	len += snprintf(buf+len, CLKBUF_STATUS_INFO_SIZE-len,
			"SPARE_ACK_MASK=0x%x, flight mode = %d\n",
			mtk_spm_read_register(SPM_SPARE_ACK_MASK),
			is_clk_buf_under_flightmode());

	len += snprintf(buf+len, CLKBUF_STATUS_INFO_SIZE-len,
			".********** clock buffer command help **********\n");
	len += snprintf(buf+len, CLKBUF_STATUS_INFO_SIZE-len,
			"PMIC switch on/off: echo pmic en1 en2 en3 en4 en5 en6 en7 > /sys/power/clk_buf/clk_buf_ctrl\n");

	return len;
}

u8 clk_buf_get_xo_en_sta(enum xo_id id)
{
	if (id < 0 || id >= XO_NUMBER)
		return 0;

	clk_buf_get_xo_en();

	return xo_en_stat[id];
}

void clk_buf_show_status_info(void)
{
	int len;
	char *buf, *str, *str_sep;

	buf = vmalloc(CLKBUF_STATUS_INFO_SIZE);
	if (buf) {
		len = clk_buf_show_status_info_internal(buf);
		str = buf;
		while ((str_sep = strsep(&str, ".")) != NULL)
			pr_info("%s\n", str_sep);

		vfree(buf);
	} else
		pr_info("%s: allocate memory fail\n", __func__);
}

#ifdef CONFIG_PM
static ssize_t clk_buf_ctrl_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	u32 clk_buf_en[CLKBUF_NUM], i, pwrap_dcxo_en = 0;
	char cmd[32];

	if (sscanf(buf, "%31s %x %x %x %x %x %x %x", cmd, &clk_buf_en[XO_SOC],
		&clk_buf_en[XO_WCN], &clk_buf_en[XO_NFC], &clk_buf_en[XO_CEL],
		&clk_buf_en[XO_AUD], &clk_buf_en[XO_PD], &clk_buf_en[XO_EXT])
		!= (CLKBUF_NUM + 1))
		return -EPERM;

	if (!strcmp(cmd, "pmic")) {
		if (!is_pmic_clkbuf)
			return -EINVAL;

		for (i = 0; i < CLKBUF_NUM; i++)
			pmic_clk_buf_swctrl[i] = clk_buf_en[i];

		pmic_clk_buf_ctrl(pmic_clk_buf_swctrl);

		return count;
	} else if (!strcmp(cmd, "pwrap")) {
		if (!is_pmic_clkbuf)
			return -EINVAL;

		mutex_lock(&clk_buf_ctrl_lock);

		for (i = 0; i < CLKBUF_NUM; i++) {
			if (i == XO_WCN) {
				if (clk_buf_en[i])
					pwrap_dcxo_en |= DCXO_CONN_ENABLE;
				else
					pwrap_dcxo_en &= ~DCXO_CONN_ENABLE;
			} else if (i == XO_NFC) {
				if (clk_buf_en[i])
					pwrap_dcxo_en |= DCXO_NFC_ENABLE;
				else
					pwrap_dcxo_en &= ~DCXO_NFC_ENABLE;
			}
		}

		clkbuf_writel(DCXO_ENABLE, pwrap_dcxo_en);
		pr_info("%s: DCXO_ENABLE=0x%x, pwrap_dcxo_en=0x%x\n",
			__func__, clkbuf_readl(DCXO_ENABLE),
			pwrap_dcxo_en);

		mutex_unlock(&clk_buf_ctrl_lock);

		return count;
	} else {
		return -EINVAL;
	}
}

static ssize_t clk_buf_ctrl_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int len;

	len = clk_buf_show_status_info_internal(buf);

	return len;
}

static ssize_t clk_buf_debug_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	u32 onoff;
	char cmd[32] =  {'\0'}, xo_user[11] = {'\0'};

	if ((sscanf(buf, "%31s %10s %x", cmd, xo_user, &onoff) != 3))
		return -EPERM;
	if (!strcmp(cmd, "debug")) {
		if (onoff == 0)
			clkbuf_debug = false;
		else if (onoff == 1)
			clkbuf_debug = true;
		else
			goto ERROR_CMD;
	}  else if (!strcmp(cmd, "bblpm")) {
		if (onoff == 0)
			bblpm_switch = 0;
		else if (onoff == 1)
			bblpm_switch = 1;
		else if (onoff == 2)
			bblpm_switch = 2;
		else
			goto ERROR_CMD;
	} else {
		if (!strcmp(xo_user, "XO_WCN")) {
			if (!strcmp(cmd, "CO_BUFFER"))
				clk_buf_ctrl_combine(CLK_BUF_CONN, onoff);
			else if (!strcmp(cmd, "FORCE_ON"))
				clk_buf_ctrl_internal(CLK_BUF_CONN, onoff);
			else if (!strcmp(cmd, "TEST"))
				clk_buf_ctrl(CLK_BUF_CONN, onoff);
			else
				goto ERROR_CMD;
		} else if (!strcmp(xo_user, "XO_NFC")) {
			if (!strcmp(cmd, "CO_BUFFER"))
				clk_buf_ctrl_combine(CLK_BUF_NFC, onoff);
			else if (!strcmp(cmd, "FORCE_ON"))
				clk_buf_ctrl_internal(CLK_BUF_NFC, onoff);
			else if (!strcmp(cmd, "TEST"))
				clk_buf_ctrl(CLK_BUF_NFC, onoff);
			else
				goto ERROR_CMD;
		} else if (!strcmp(xo_user, "XO_CEL")) {
			if (!strcmp(cmd, "CO_BUFFER"))
				clk_buf_ctrl_combine(CLK_BUF_RF, onoff);
			else if (!strcmp(cmd, "FORCE_ON"))
				clk_buf_ctrl_internal(CLK_BUF_RF, onoff);
			else if (!strcmp(cmd, "TEST"))
				clk_buf_ctrl(CLK_BUF_RF, onoff);
			else
				goto ERROR_CMD;
		} else if (!strcmp(xo_user, "XO_EXT")) {
			if (!strcmp(cmd, "CO_BUFFER"))
				clk_buf_ctrl_combine(CLK_BUF_UFS, onoff);
			else if (!strcmp(cmd, "FORCE_ON"))
				clk_buf_ctrl_internal(CLK_BUF_UFS, onoff);
			else if (!strcmp(cmd, "TEST"))
				clk_buf_ctrl(CLK_BUF_UFS, onoff);
			else
				goto ERROR_CMD;
		} else
			if (strcmp(xo_user, "0"))
				goto ERROR_CMD;
	}

	return count;
ERROR_CMD:
	pr_info("bad argument!! please follow correct format\n");
	return -EPERM;
}

static ssize_t clk_buf_debug_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int len = 0;

	len += snprintf(buf+len, PAGE_SIZE-len, "clkbuf_debug=%d\n",
		clkbuf_debug);

	return len;
}

static ssize_t clk_buf_bblpm_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	u32 onoff = 0;
	int ret = 0;

	if ((kstrtouint(buf, 10, &onoff))) {
		pr_info("bblpm input error\n");
		return -EPERM;
	}
	pr_info("bblpm input = %d\n", onoff);
	if (onoff == 1)
		ret = clk_buf_ctrl_bblpm_sw(true);
	else if (onoff == 0)
		ret = clk_buf_ctrl_bblpm_sw(false);

	if (ret)
		return ret;

	return count;
}

static ssize_t clk_buf_bblpm_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int len = 0;

	clk_buf_get_aux_out();
	len += snprintf(buf+len, PAGE_SIZE-len,
		"EN_STAT=%d %d %d %d %d %d\n",
		xo_en_stat[XO_SOC],
		xo_en_stat[XO_WCN],
		xo_en_stat[XO_NFC],
		xo_en_stat[XO_CEL],
		xo_en_stat[XO_PD],
		xo_en_stat[XO_EXT]);

	len += snprintf(buf+len, PAGE_SIZE-len,
		"bblpm %d %d\n",
		xo_bb_lpm_en_stat,
		xo_bb_lpm_en_o);

	return len;
}

DEFINE_ATTR_RW(clk_buf_ctrl);
DEFINE_ATTR_RW(clk_buf_debug);
DEFINE_ATTR_RW(clk_buf_bblpm);

static struct attribute *clk_buf_attrs[] = {
	/* for clock buffer control */
	__ATTR_OF(clk_buf_ctrl),
	__ATTR_OF(clk_buf_debug),
	__ATTR_OF(clk_buf_bblpm),

	/* must */
	NULL,
};

static struct attribute_group clk_buf_attr_group = {
	.name	= "clk_buf",
	.attrs	= clk_buf_attrs,
};

int clk_buf_fs_init(void)
{
	int r = 0;

	/* create /sys/power/clk_buf/xxx */
	r = sysfs_create_group(power_kobj, &clk_buf_attr_group);
	if (r)
		pr_notice("FAILED TO CREATE /sys/power/clk_buf (%d)\n", r);

	return r;
}
#else /* !CONFIG_PM */
int clk_buf_fs_init(void)
{
	return 0;
}
#endif /* CONFIG_PM */

#if defined(CONFIG_OF)
int clk_buf_dts_map(void)
{
	struct device_node *node;
	u32 vals[CLKBUF_NUM] = {0, 0, 0, 0, 0, 0, 0};
	int ret = -1;

	node = of_find_compatible_node(NULL, NULL,
		"mediatek,pmic_clock_buffer");
	if (node) {
		ret = of_property_read_u32_array(node,
			"mediatek,clkbuf-config", vals, CLKBUF_NUM);
		if (!ret) {
			CLK_BUF1_STATUS = vals[0];
			CLK_BUF2_STATUS = vals[1];
			CLK_BUF3_STATUS = vals[2];
			CLK_BUF4_STATUS = vals[3];
			CLK_BUF5_STATUS = vals[4];
			CLK_BUF6_STATUS = vals[5];
			CLK_BUF7_STATUS = vals[6];
		}
		ret = of_property_read_u32_array(node,
			"mediatek,clkbuf-output-impedance", vals, CLKBUF_NUM);
		if (!ret) {
			CLK_BUF1_OUTPUT_IMPEDANCE = vals[0];
			CLK_BUF2_OUTPUT_IMPEDANCE = vals[1];
			CLK_BUF3_OUTPUT_IMPEDANCE = vals[2];
			CLK_BUF4_OUTPUT_IMPEDANCE = vals[3];
			CLK_BUF5_OUTPUT_IMPEDANCE = vals[4];
			CLK_BUF6_OUTPUT_IMPEDANCE = vals[5];
			CLK_BUF7_OUTPUT_IMPEDANCE = vals[6];
		}
		ret = of_property_read_u32_array(node,
			"mediatek,clkbuf-controls-for-desense", vals,
			CLKBUF_NUM);
		if (!ret) {
			CLK_BUF1_CONTROLS_DESENSE = vals[0];
			CLK_BUF2_CONTROLS_DESENSE = vals[1];
			CLK_BUF3_CONTROLS_DESENSE = vals[2];
			CLK_BUF4_CONTROLS_DESENSE = vals[3];
			CLK_BUF5_CONTROLS_DESENSE = vals[4];
			CLK_BUF6_CONTROLS_DESENSE = vals[5];
			CLK_BUF7_CONTROLS_DESENSE = vals[6];
		}
	} else {
		pr_notice("%s can't find compatible node for pmic_clock_buffer\n",
			__func__);
		return -1;
	}
	node = of_find_compatible_node(NULL, NULL, "mediatek,mt6785-pwrap");
	if (node)
		pwrap_base = of_iomap(node, 0);
	else {
		pr_notice("%s can't find compatible node for pwrap\n",
			__func__);
		return -1;
	}

	return 0;
}
#else /* !CONFIG_OF */
int clk_buf_dts_map(void)
{
	return 0;
}
#endif

void clk_buf_init_pmic_clkbuf_legacy(void)
{
	clk_buf_dump_clkbuf_log();
}

void clk_buf_init_pmic_wrap(void)
{
}

void clk_buf_init_pmic_swctrl(void)
{
	if (CLK_BUF1_STATUS == CLOCK_BUFFER_DISABLE)
		pmic_clk_buf_swctrl[XO_SOC] = CLK_BUF_SW_DISABLE;

	if (CLK_BUF2_STATUS == CLOCK_BUFFER_DISABLE) {
		clk_buf_ctrl_internal(CLK_BUF_CONN, CLK_BUF_FORCE_OFF);
		pmic_clk_buf_swctrl[XO_WCN] = CLK_BUF_SW_DISABLE;
	}

	if (CLK_BUF3_STATUS == CLOCK_BUFFER_DISABLE) {
		clk_buf_ctrl_internal(CLK_BUF_NFC, CLK_BUF_FORCE_OFF);
		pmic_clk_buf_swctrl[XO_NFC] = CLK_BUF_SW_DISABLE;
	}

	if (CLK_BUF4_STATUS == CLOCK_BUFFER_DISABLE) {
		clk_buf_ctrl_internal(CLK_BUF_RF, CLK_BUF_FORCE_OFF);
		pmic_clk_buf_swctrl[XO_CEL] = CLK_BUF_SW_DISABLE;
	}

	if (CLK_BUF5_STATUS == CLOCK_BUFFER_DISABLE)
		pmic_clk_buf_swctrl[XO_AUD] = CLK_BUF_SW_DISABLE;

	if (CLK_BUF6_STATUS == CLOCK_BUFFER_DISABLE)
		pmic_clk_buf_swctrl[XO_PD] = CLK_BUF_SW_DISABLE;

	if (CLK_BUF7_STATUS == CLOCK_BUFFER_DISABLE) {
		clk_buf_ctrl_internal(CLK_BUF_UFS, CLK_BUF_FORCE_OFF);
		pmic_clk_buf_swctrl[XO_EXT] = CLK_BUF_SW_DISABLE;
	}
}

short is_clkbuf_bringup(void)
{
#ifdef CLKBUF_BRINGUP
	pr_info("%s: skipped for bring up\n", __func__);
	return 1;
#else
	return 0;
#endif
}

void clk_buf_post_init(void)
{
	int project_number;
	project_number = board_id_get_hwversion_product_num();

#ifndef CONFIG_NFC_CHIP_SUPPORT
	/* no need to use XO_NFC if no NFC */
	clk_buf_ctrl_internal(CLK_BUF_NFC, CLK_BUF_FORCE_OFF);
	CLK_BUF3_STATUS = CLOCK_BUFFER_DISABLE;
#else
	if (project_number == 1 || project_number == 3) {
		/* no need to use XO_NFC if no NFC */
		clk_buf_ctrl_internal(CLK_BUF_NFC, CLK_BUF_FORCE_OFF);
		CLK_BUF3_STATUS = CLOCK_BUFFER_DISABLE;
	}
#endif
#ifdef CLKBUF_USE_BBLPM
	if (bblpm_switch == 2) {
		clk_buf_ctrl_bblpm_mask(CLK_BUF_BB_MD, true);
		clk_buf_ctrl_bblpm_mask(CLK_BUF_UFS, false);
		if (CLK_BUF4_STATUS == CLOCK_BUFFER_DISABLE) {
			clk_buf_ctrl_bblpm_mask(CLK_BUF_RF, true);
			clk_buf_ctrl_bblpm_hw(true);
		} else
			clk_buf_ctrl_bblpm_hw(false);
	}
#endif

	/* save setting after init done */
	pmic_read_interface(PMIC_XO_EXTBUF2_MODE_ADDR,
		&xo_mode_init[XO_WCN],
		PMIC_XO_EXTBUF2_MODE_MASK,
		PMIC_XO_EXTBUF2_MODE_SHIFT);
	pmic_read_interface(PMIC_XO_EXTBUF3_MODE_ADDR,
		&xo_mode_init[XO_NFC],
		PMIC_XO_EXTBUF3_MODE_MASK,
		PMIC_XO_EXTBUF3_MODE_SHIFT);
	pmic_read_interface(PMIC_XO_EXTBUF4_MODE_ADDR,
		&xo_mode_init[XO_CEL],
		PMIC_XO_EXTBUF4_MODE_MASK,
		PMIC_XO_EXTBUF4_MODE_SHIFT);
	pmic_read_interface(PMIC_XO_EXTBUF7_MODE_ADDR,
		&xo_mode_init[XO_EXT],
		PMIC_XO_EXTBUF7_MODE_MASK,
		PMIC_XO_EXTBUF7_MODE_SHIFT);
	pwrap_dcxo_en_init = clkbuf_readl(DCXO_ENABLE);
}

