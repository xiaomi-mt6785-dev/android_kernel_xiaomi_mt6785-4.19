/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 MediaTek Inc.
*/

#ifndef __MTK_INTF_H
#define __MTK_INTF_H

#include "mtk_charger_intf.h"
#include "mtk_pe50.h"
#include "mtk_pe40.h"
#include "mtk_pdc.h"

extern struct charger_manager *p_info;

enum adapter_ret {
	ADAPTER_OK = 0,
	ADAPTER_NOT_SUPPORT,
	ADAPTER_TIMEOUT,
	ADAPTER_REJECT,
	ADAPTER_ERROR,
	ADAPTER_ADJUST,
	ADAPTER_VERIFYING,
};

extern int charger_is_chip_enabled(bool *en);
extern int charger_enable_chip(bool en);
extern int charger_is_enabled(bool *en);
extern int charger_enable_chip(bool en);
extern int charger_get_mivr_state(bool *in_loop);
extern int charger_get_mivr(u32 *uV);
extern int charger_set_mivr(u32 uV);
extern int charger_get_input_current(u32 *uA);
extern int charger_set_input_current(u32 uA);
extern int charger_set_charging_current(u32 uA);
extern int charger_get_ibus(u32 *ibus);
extern int charger_get_ibat(u32 *ibat);
extern int charger_set_constant_voltage(u32 uV);
extern int charger_enable_termination(bool en);
extern int charger_enable_powerpath(bool en);
extern int charger_force_disable_powerpath(bool disable);
extern int charger_dump_registers(void);

extern int adapter_set_cap(int mV, int mA);
extern int adapter_set_cap_start(int mV, int mA);
extern int adapter_set_cap_end(int mV, int mA);
extern int adapter_get_output(int *mV, int *mA);
extern int adapter_get_pps_cap(struct pps_cap *cap);
extern int adapter_get_status(struct ta_status *sta);
extern int adapter_is_support_pd_pps(void);

extern int adapter_get_cap(struct pd_cap *cap);
extern int adapter_is_support_pd(void);

extern int set_charger_manager(struct charger_manager *info);
extern int enable_vbus_ovp(bool en);
extern int wake_up_charger(void);

#endif /* __MTK_INTF_H */
