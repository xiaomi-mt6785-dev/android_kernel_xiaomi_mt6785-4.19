/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 MediaTek Inc.
*/

/*
 *
 * Filename:
 * ---------
 *    mtk_switch_charging.c
 *
 * Project:
 * --------
 *   Android_Software
 *
 * Description:
 * ------------
 *   This Module defines functions of Battery charging
 *
 * Author:
 * -------
 * Wy Chuang
 *
 */
#include <linux/init.h>		/* For init/exit macros */
#include <linux/module.h>	/* For MODULE_ marcros  */
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/power_supply.h>
#include <linux/pm_wakeup.h>
#include <linux/time.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/proc_fs.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/scatterlist.h>
#include <linux/suspend.h>
#include <linux/of.h>

#include <mt-plat/mtk_boot.h>
#include <mt-plat/v1/mtk_charger.h>
#include "mtk_charger_intf.h"
#include "mtk_switch_charging.h"
#include "mtk_intf.h"
#include "mtk_charger_init.h"

struct tag_bootmode {
	u32 size;
	u32 tag;
	u32 bootmode;
	u32 boottype;
};

static int _uA_to_mA(int uA)
{
	if (uA == -1)
		return -1;
	else
		return uA / 1000;
}

static void _disable_all_charging(struct charger_manager *info)
{
	charger_dev_enable(info->chg1_dev, false);

	if (mtk_pe20_get_is_enable(info)) {
		mtk_pe20_set_is_enable(info, false);
		if (mtk_pe20_get_is_connect(info))
			mtk_pe20_reset_ta_vchr(info);
	}

	if (mtk_pe_get_is_enable(info)) {
		mtk_pe_set_is_enable(info, false);
		if (mtk_pe_get_is_connect(info))
			mtk_pe_reset_ta_vchr(info);
	}

	if (info->enable_pe_5)
		pe50_stop();

	if (info->enable_pe_4)
		pe40_stop();

	if (pdc_is_ready())
		pdc_stop();
}

static int get_usb_type(struct charger_manager *info)
{
	int ret;
	struct power_supply *usb_psy;
	union power_supply_propval val = {0,};

	info->usb_type = POWER_SUPPLY_TYPE_UNKNOWN;

	usb_psy = power_supply_get_by_name("usb");
	if (!usb_psy)
		return -ENODEV;

	ret = power_supply_get_property(usb_psy,
			POWER_SUPPLY_PROP_REAL_TYPE, &val);
	if (!ret)
		info->usb_type = val.intval;

	return ret;
}

static void swchg_select_charging_current_limit(struct charger_manager *info)
{
	struct charger_data *pdata = NULL;
	struct switch_charging_alg_data *swchgalg = info->algorithm_data;
	u32 ichg1_min = 0, aicr1_min = 0;
	int ret = 0 ,rc = 0;
	struct device *dev = NULL;
	struct device_node *boot_node = NULL;
	struct tag_bootmode *tag = NULL;
	int boot_mode = 11;//UNKNOWN_BOOT
	int thermal_current_limit = 6000000;
	int thermal_input_current = 3000000;
	struct power_supply	*battery_psy;
	union power_supply_propval val = {0,};
	union power_supply_propval temp_level = {0,};
#ifdef CONFIG_MTBF_SUPPORT
	struct power_supply	*usb_psy;
	bool is_powerpath_en = true;
	bool is_chg_suspend = true;
#endif

	battery_psy = power_supply_get_by_name("battery");

#ifdef CONFIG_MTBF_SUPPORT
	usb_psy = power_supply_get_by_name("usb");
#endif

	dev = &(info->pdev->dev);
	if (dev != NULL) {
		boot_node = of_parse_phandle(dev->of_node, "bootmode", 0);
		if (!boot_node) {
			chr_err("%s: failed to get boot mode phandle\n", __func__);
		} else {
			tag = (struct tag_bootmode *)of_get_property(boot_node,
								"atag,boot", NULL);
			if (!tag) {
				chr_err("%s: failed to get atag,boot\n", __func__);
			} else
				boot_mode = tag->bootmode;
		}
	}

	if (info->pe5.online) {
		chr_err("In PE5.0\n");
		return;
	}

	pdata = &info->chg1_data;
	mutex_lock(&swchgalg->ichg_aicr_access_mutex);

	/* AICL */
	if (!mtk_pe20_get_is_connect(info) && !mtk_pe_get_is_connect(info) &&
	    !mtk_is_TA_support_pd_pps(info) && !mtk_pdc_check_charger(info)) {
		charger_dev_run_aicl(info->chg1_dev,
				&pdata->input_current_limit_by_aicl);
		if (info->enable_dynamic_mivr) {
			if (pdata->input_current_limit_by_aicl >
				info->data.max_dmivr_charger_current)
				pdata->input_current_limit_by_aicl =
					info->data.max_dmivr_charger_current;
		}
	}

	if (pdata->force_charging_current > 0) {

		pdata->charging_current_limit = pdata->force_charging_current;
		if (pdata->force_charging_current <= 450000) {
			pdata->input_current_limit = 500000;
		} else {
			pdata->input_current_limit =
					info->data.ac_charger_input_current;
			pdata->charging_current_limit =
					info->data.ac_charger_current;
			chr_err("force_charging_current > 0.\n");
		}
		goto done;
	}

	if (info->usb_unlimited) {
		pdata->input_current_limit =
					info->data.ac_charger_input_current;
		pdata->charging_current_limit =
					info->data.ac_charger_current;
		chr_err("info->usb_unlimited = true.\n");
		goto done;
	}

	if (info->water_detected) {
		pdata->input_current_limit = info->data.usb_charger_current;
		pdata->charging_current_limit = info->data.usb_charger_current;
		goto done;
	}

	if ((boot_mode == META_BOOT) ||
		(boot_mode == ADVMETA_BOOT)) {
		pdata->input_current_limit = 200000; /* 200mA */
		goto done;
	}

	if (info->atm_enabled == true && (info->chr_type == STANDARD_HOST ||
	    info->chr_type == CHARGING_HOST)) {
		pdata->input_current_limit = 500000; /* 500mA */
		pdata->charging_current_limit = 500000; /* 500mA */
		goto done;
	}

	get_usb_type(info);

	if (is_typec_adapter(info) && info->chr_type != NONSTANDARD_CHARGER && info->chr_type != STANDARD_CHARGER) {
		if (adapter_dev_get_property(info->pd_adapter, TYPEC_RP_LEVEL)
			== 3000) {
			pdata->input_current_limit = 1500000;
			pdata->charging_current_limit = 2000000;
		} else if (adapter_dev_get_property(info->pd_adapter,
			TYPEC_RP_LEVEL) == 1500) {
			pdata->input_current_limit = 1500000;
			pdata->charging_current_limit = 2000000;
		} else {
			chr_err("type-C: inquire rp error\n");
			pdata->input_current_limit = 500000;
			pdata->charging_current_limit = 500000;
		}

		chr_err("type-C:%d current:%d\n",
			info->pd_type,
			adapter_dev_get_property(info->pd_adapter,
				TYPEC_RP_LEVEL));
	} else if (charger_manager_pd_is_online()) {
		if (info->stop_pdc_with_dis_hv == true) {
			charger_dev_get_input_current(info->chg1_dev, &pdata->input_current_limit);
			charger_dev_get_charging_current(info->chg1_dev, &pdata->charging_current_limit);
			pr_info("stop pdc with disable hv charging, keep AICR = %dmA, ICHG = %dmA\n",
				pdata->input_current_limit/1000, pdata->charging_current_limit/1000);
		} else {
			pdata->charging_current_limit = 3000000;
			pdata->input_current_limit = 3000000;
			pr_info("PD set ICHG = 3000mA, AICR = 3000mA\n");
		}
		rc = power_supply_get_property(battery_psy,POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT,&temp_level);
		if (rc < 0) {
			chr_err("Couldn't get POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT mode:%d\n", rc);
		}
		chr_err(" Get POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT mode:%d\n", temp_level.intval);
		charger_manager_set_prop_system_temp_level(temp_level.intval);
	} else if (info->chr_type == STANDARD_HOST) {
		if (IS_ENABLED(CONFIG_USBIF_COMPLIANCE)) {
			if (info->usb_state == USB_SUSPEND)
				pdata->input_current_limit =
					info->data.usb_charger_current_suspend;
			else if (info->usb_state == USB_UNCONFIGURED)
				pdata->input_current_limit =
				info->data.usb_charger_current_unconfigured;
			else if (info->usb_state == USB_CONFIGURED)
				pdata->input_current_limit =
				info->data.usb_charger_current_configured;
			else
				pdata->input_current_limit =
				info->data.usb_charger_current_unconfigured;

			pdata->charging_current_limit =
					pdata->input_current_limit;
		} else {
			pdata->input_current_limit =
					info->data.usb_charger_current;
			/* it can be larger */
			pdata->charging_current_limit =
					info->data.usb_charger_current;
		}
	} else if (info->chr_type == NONSTANDARD_CHARGER) {
		pdata->input_current_limit =
				info->data.non_std_ac_charger_current;
		pdata->charging_current_limit =
				info->data.non_std_ac_charger_current;
	} else if (info->chr_type == STANDARD_CHARGER) {
		if (info->dcp_confirmed) {
			pdata->input_current_limit =
					info->data.ac_charger_input_current;
			pdata->charging_current_limit =
					info->data.ac_charger_current;
		}
		if (info->usb_type == POWER_SUPPLY_TYPE_USB_HVDCP_3 ||
			info->usb_type == POWER_SUPPLY_TYPE_USB_HVDCP_3P5) {
			pdata->charging_current_limit = 3000000;
			pdata->input_current_limit = 3000000;
			pr_info("QC set ICHG = 3000mA, AICR = 3000mA\n");
		} else if (info->usb_type == POWER_SUPPLY_TYPE_USB_HVDCP
			&& (swchgalg->vbus_mv > HVDCP2P0_VOLATGE)) {
			pdata->charging_current_limit = 2400000;
			pdata->input_current_limit = 1500000;
			pr_info("QC2 set AICR = 1500mA\n");
		}
#ifdef CONFIG_BQ2597X_CHARGE_PUMP
		charger_manager_set_prop_system_temp_level(info->temp_level);
#endif
		mtk_pe20_set_charging_current(info,
					&pdata->charging_current_limit,
					&pdata->input_current_limit);
		mtk_pe_set_charging_current(info,
					&pdata->charging_current_limit,
					&pdata->input_current_limit);
	} else if (info->chr_type == CHARGING_HOST) {
		pdata->input_current_limit =
				info->data.charging_host_charger_current;
		pdata->charging_current_limit =
				info->data.charging_host_charger_current;
	} else if (info->chr_type == APPLE_1_0A_CHARGER) {
		pdata->input_current_limit =
				info->data.apple_1_0a_charger_current;
		pdata->charging_current_limit =
				info->data.apple_1_0a_charger_current;
	} else if (info->chr_type == APPLE_2_1A_CHARGER) {
		pdata->input_current_limit =
				info->data.apple_2_1a_charger_current;
		pdata->charging_current_limit =
				info->data.apple_2_1a_charger_current;
	} else if (info->chr_type == CHECK_HV) {
		pdata->input_current_limit =
				info->data.check_hv_current;
		pdata->charging_current_limit =
				info->data.check_hv_current;
	} else if (info->chr_type == HVDCP_CHARGER) {
		pdata->input_current_limit =
				info->data.qc_charger_input_current;
		pdata->charging_current_limit =
				info->data.ac_charger_current;
	}
#ifdef CONFIG_BQ2597X_CHARGE_PUMP
	if (swchgalg->state == CHR_XM_QC20) {
		pdata->input_current_limit = 1500000;
		pdata->charging_current_limit = 2500000;
	}
#endif
	if (info->enable_sw_jeita) {
		if (IS_ENABLED(CONFIG_USBIF_COMPLIANCE)
		    && info->chr_type == STANDARD_HOST)
			pr_debug("USBIF & STAND_HOST skip current check\n");
		else {
			if (info->sw_jeita.sm == TEMP_T0_TO_T1) {
				pdata->input_current_limit = 3000000;
				pdata->charging_current_limit = 980000;
			}
		}
	}

	if (pdata->thermal_charging_current_limit != -1) {
		if (pdata->thermal_charging_current_limit <
		    pdata->charging_current_limit)
			pdata->charging_current_limit =
					pdata->thermal_charging_current_limit;
	}

	if (pdata->thermal_input_current_limit != -1) {
		if (pdata->thermal_input_current_limit <
		    pdata->input_current_limit)
			pdata->input_current_limit =
					pdata->thermal_input_current_limit;
	}

	if (pdata->input_current_limit_by_aicl != -1 &&
	    !mtk_pe20_get_is_connect(info) && !mtk_pe_get_is_connect(info) &&
	    !mtk_is_TA_support_pd_pps(info)) {
		if (pdata->input_current_limit_by_aicl <
		    pdata->input_current_limit)
			pdata->input_current_limit =
					pdata->input_current_limit_by_aicl;
	}

	if (battery_psy) {
		ret = power_supply_get_property(battery_psy,
			POWER_SUPPLY_PROP_FAST_CHARGE_CURRENT, &val);
		if (ret)
			pr_err("get thermal current limit failed!!\n");
		else
			thermal_current_limit = val.intval;

		ret = power_supply_get_property(battery_psy,
			POWER_SUPPLY_PROP_THERMAL_INPUT_CURRENT, &val);
		if (ret)
			pr_err("get thermal current limit failed!!\n");
		else
			thermal_input_current = val.intval;

		if (thermal_current_limit < pdata->charging_current_limit ||
			thermal_input_current < pdata->input_current_limit) {
			if (info->usb_type == POWER_SUPPLY_TYPE_USB_HVDCP_3P5 ||
				info->usb_type == POWER_SUPPLY_TYPE_USB_HVDCP_3 ||
				info->usb_type == POWER_SUPPLY_TYPE_USB_PD) {
				pdata->charging_current_limit = thermal_current_limit;
				pr_err("thermal set FCC is %d\n", thermal_current_limit);
			} else if (thermal_input_current > 0) {
				pdata->input_current_limit = thermal_input_current;
				pr_err("thermal set ICL is %d\n", thermal_input_current);
			}
		}
	} else {
		pr_err("battery_psy not found\n");
	}

#ifdef CONFIG_MTBF_SUPPORT
	if ((info->chr_type == CHARGING_HOST) || (info->chr_type == STANDARD_CHARGER)) {
		if (usb_psy) {
			ret = power_supply_get_property(usb_psy,
				POWER_SUPPLY_PROP_MTBF_CUR, &val);
			if (ret) {
				pr_err("get mtbf current failed!!\n");
			} else {
					pr_err("mtbf current limit is %d\n", val.intval);
					//if (val.intval >= 1500) {
					//pdata->charging_current_limit = 1500000;
					pdata->input_current_limit = 1500 * 1000;
					pr_err("mtbf current  charging_current_limit = %d, input_current_limit =  %d\n", pdata->charging_current_limit, pdata->input_current_limit);
					//}

					ret = charger_dev_is_powerpath_enabled(info->chg1_dev, &is_powerpath_en);
					if (ret < 0)
					{
						chr_err("%s: mtbf get is power path enabled failed\n", __func__);
					}

					is_chg_suspend = charger_manager_is_input_suspend();

					chr_err("%s: mtbf before is_powerpath_en = %d, is_chg_suspend = %d\n", __func__, is_powerpath_en, is_chg_suspend);

					if(0 == is_chg_suspend)
					{
						charger_dev_enable_powerpath(info->chg1_dev, true); //charging status: force open power path
					}
					chr_err("%s: mtbf after is_powerpath_en = %d, is_chg_suspend = %d\n", __func__, is_powerpath_en, is_chg_suspend);
			}
		} else {
			pr_err("mtbf usb_psy not found\n");
		}
	}
#endif

done:
	ret = charger_dev_get_min_charging_current(info->chg1_dev, &ichg1_min);
	if (ret != -ENOTSUPP && pdata->charging_current_limit < ichg1_min)
		pdata->charging_current_limit = 0;

	ret = charger_dev_get_min_input_current(info->chg1_dev, &aicr1_min);
	if (ret != -ENOTSUPP && pdata->input_current_limit < aicr1_min)
		pdata->input_current_limit = 0;

#ifdef CONFIG_MTBF_SUPPORT
	ret = charger_dev_is_powerpath_enabled(info->chg1_dev, &is_powerpath_en);
	if (ret < 0)
	{
		chr_err("%s: mtbf get is power path enabled failed\n", __func__);
	}

	is_chg_suspend = charger_manager_is_input_suspend();

	chr_err("%s: mtbf is_powerpath_en = %d, is_chg_suspend = %d\n", __func__, is_powerpath_en, is_chg_suspend);
#endif

	chr_err("%s force:%d thermal:%d,%d setting:%d %d type:%d usb_unlimited:%d usbif:%d usbsm:%d aicl:%d atm:%d iclmin:%d fccmin:%d\n",
		__func__,
		_uA_to_mA(pdata->force_charging_current),
		_uA_to_mA(pdata->thermal_input_current_limit),
		_uA_to_mA(pdata->thermal_charging_current_limit),
		_uA_to_mA(pdata->input_current_limit),
		_uA_to_mA(pdata->charging_current_limit),
		info->chr_type, info->usb_unlimited,
		IS_ENABLED(CONFIG_USBIF_COMPLIANCE), info->usb_state,
		pdata->input_current_limit_by_aicl, info->atm_enabled,
		aicr1_min, ichg1_min);

	charger_dev_set_input_current(info->chg1_dev,
					pdata->input_current_limit);
	charger_dev_set_charging_current(info->chg1_dev,
					pdata->charging_current_limit);

	/* If AICR < 300mA, stop PE+/PE+20 */
	if (pdata->input_current_limit < 300000) {
		if (mtk_pe20_get_is_enable(info)) {
			mtk_pe20_set_is_enable(info, false);
			if (mtk_pe20_get_is_connect(info))
				mtk_pe20_reset_ta_vchr(info);
		}

		if (mtk_pe_get_is_enable(info)) {
			mtk_pe_set_is_enable(info, false);
			if (mtk_pe_get_is_connect(info))
				mtk_pe_reset_ta_vchr(info);
		}
	}

	/*
	 * If thermal current limit is larger than charging IC's minimum
	 * current setting, enable the charger immediately
	 */
	if (pdata->input_current_limit > aicr1_min &&
	    pdata->charging_current_limit > ichg1_min && info->can_charging)
		charger_dev_enable(info->chg1_dev, true);
	mutex_unlock(&swchgalg->ichg_aicr_access_mutex);
}

static void swchg_select_cv(struct charger_manager *info)
{
	u32 constant_voltage;

	if (info->enable_sw_jeita)
		if (info->sw_jeita.cv != 0) {
			charger_dev_set_constant_voltage(info->chg1_dev,
							info->sw_jeita.cv);
			return;
		}

	/* dynamic cv*/
	constant_voltage = info->data.battery_cv;
	mtk_get_dynamic_cv(info, &constant_voltage);

	charger_dev_set_constant_voltage(info->chg1_dev, constant_voltage);
}

int get_bq_charge_done(struct charger_manager *info)
{
	union power_supply_propval val = {0,};

	if (!info->bq_psy)
		info->bq_psy = power_supply_get_by_name("bq2597x-standalone");

	if ((info) && (info->bq_psy)) {
		power_supply_get_property(info->bq_psy,
				POWER_SUPPLY_PROP_BQ_CHARGE_DONE, &val);
		pr_err("get bq charge done enable=%d\n", val.intval);
	}
	return val.intval;
}

static int set_bq_charge_done(struct charger_manager *info, bool enable)
{
	int ret = 0;
	union power_supply_propval val = {0,};

	if (!info->bq_psy)
		info->bq_psy = power_supply_get_by_name("bq2597x-standalone");

	pr_info("set bq charge done enable=%d\n", enable);
	val.intval = enable;
	if ((info) && (info->bq_psy)) {
		ret = power_supply_set_property(info->bq_psy,
				POWER_SUPPLY_PROP_BQ_CHARGE_DONE, &val);
		if (ret < 0)
			pr_err("set bq charge done failed, ret=%d\n", ret);
	}

	return ret;
}

static int set_hv_charge_enable(struct charger_manager *info, bool enable)
{
	int ret = 0;
	union power_supply_propval val = {0,};

	if (!info->bq_psy)
		info->bq_psy = power_supply_get_by_name("bq2597x-standalone");

	pr_info("set hv charge enable=%d\n", enable);
	val.intval = enable;
	if ((info) && (info->bq_psy)) {
		ret = power_supply_set_property(info->bq_psy,
				POWER_SUPPLY_PROP_HV_CHARGE_ENABLED, &val);
		if (ret < 0)
			pr_err("set hv charge enable failed, ret=%d\n", ret);
	}

	return ret;
}

static void swchg_turn_on_charging(struct charger_manager *info)
{
	struct switch_charging_alg_data *swchgalg = info->algorithm_data;
	bool charging_enable = true;
 	struct device *dev = NULL;
	struct device_node *boot_node = NULL;
	struct tag_bootmode *tag = NULL;
	int boot_mode = 11;//UNKNOWN_BOOT

	dev = &(info->pdev->dev);
	if (dev != NULL) {
		boot_node = of_parse_phandle(dev->of_node, "bootmode", 0);
		if (!boot_node) {
			chr_err("%s: failed to get boot mode phandle\n", __func__);
		} else {
			tag = (struct tag_bootmode *)of_get_property(boot_node,
								"atag,boot", NULL);
			if (!tag)
				chr_err("%s: failed to get atag,boot\n", __func__);
			else
				boot_mode = tag->bootmode;
		}
	}

	if (swchgalg->state == CHR_ERROR) {
		charging_enable = false;
		chr_err("[charger]Charger Error, turn OFF charging !\n");
	} else if ((boot_mode == META_BOOT) ||
			(boot_mode == ADVMETA_BOOT)) {
		charging_enable = false;
		info->chg1_data.input_current_limit = 200000; /* 200mA */
	charger_dev_set_input_current(info->chg1_dev,
					info->chg1_data.input_current_limit);
		chr_err("In meta mode, disable charging and set input current limit to 200mA\n");
	} else {
		mtk_pe20_start_algorithm(info);
		if (mtk_pe20_get_is_connect(info) == false)
			mtk_pe_start_algorithm(info);

		swchg_select_charging_current_limit(info);
		if (info->chg1_data.input_current_limit == 0
		    || info->chg1_data.charging_current_limit == 0) {
			charging_enable = false;
			chr_err("[charger]charging current is set 0mA, turn off charging !\n");
		} else {
			swchg_select_cv(info);
		}
	}

	if (!charging_enable) {
		set_bq_charge_done(info, false);
		chr_err("[charger]turn off 6360 charging !\n");
	}

	charger_dev_enable(info->chg1_dev, charging_enable);
}

static int mtk_switch_charging_plug_in(struct charger_manager *info)
{
	struct switch_charging_alg_data *swchgalg = info->algorithm_data;

	set_bq_charge_done(info, false);
	set_hv_charge_enable(info, true);
	swchgalg->state = CHR_CC;
	info->polling_interval = CHARGING_INTERVAL;
	swchgalg->disable_charging = false;
	get_monotonic_boottime(&swchgalg->charging_begin_time);

	return 0;
}

static int mtk_switch_charging_plug_out(struct charger_manager *info)
{
	struct switch_charging_alg_data *swchgalg = info->algorithm_data;

	set_bq_charge_done(info, true);
	set_hv_charge_enable(info, true);
	swchgalg->total_charging_time = 0;

	mtk_pe20_set_is_cable_out_occur(info, true);
	mtk_pe_set_is_cable_out_occur(info, true);
	mtk_pdc_plugout(info);

	if (info->enable_pe_5)
		pe50_stop();

	if (info->enable_pe_4)
		pe40_stop();

	info->leave_pe5 = false;
	info->leave_pe4 = false;
	info->leave_pdc = false;
	info->stop_pdc_with_dis_hv = false;
	return 0;
}

static int mtk_switch_charging_do_charging(struct charger_manager *info,
						bool en)
{
	struct switch_charging_alg_data *swchgalg = info->algorithm_data;

	chr_err("%s: en:%d %s\n", __func__, en, info->algorithm_name);
	if (en) {
		swchgalg->disable_charging = false;
		swchgalg->state = CHR_CC;
		get_monotonic_boottime(&swchgalg->charging_begin_time);
		charger_manager_notifier(info, CHARGER_NOTIFY_NORMAL);
	} else {
		/* disable charging might change state, so call it first */
		_disable_all_charging(info);
		swchgalg->disable_charging = true;
		swchgalg->state = CHR_ERROR;
		charger_manager_notifier(info, CHARGER_NOTIFY_ERROR);
	}

	return 0;
}

static int mtk_switch_chr_pe50_init(struct charger_manager *info)
{
	int ret;

	ret = pe50_init();

	if (ret == 0)
		set_charger_manager(info);
	else
		chr_err("pe50 init fail\n");

	info->leave_pe5 = false;

	return ret;
}

static int mtk_switch_chr_pe50_run(struct charger_manager *info)
{
	struct switch_charging_alg_data *swchgalg = info->algorithm_data;
	/* struct charger_custom_data *pdata = &info->data; */
	/* struct pe50_data *data; */
	int ret = 0;

	if (info->enable_hv_charging == false)
		goto stop;

	ret = pe50_run();

	if (ret == 1) {
		pr_info("retry pe5\n");
		goto retry;
	}

	if (ret == 2) {
		chr_err("leave pe5\n");
		info->leave_pe5 = true;
		swchgalg->state = CHR_CC;
	}

	return 0;

stop:
	pe50_stop();
retry:
	swchgalg->state = CHR_CC;

	return 0;
}


static int mtk_switch_chr_pe40_init(struct charger_manager *info)
{
	int ret;

	ret = pe40_init();

	if (ret == 0)
		set_charger_manager(info);

	info->leave_pe4 = false;

	return 0;
}

static int select_pe40_charging_current_limit(struct charger_manager *info)
{
	struct charger_data *pdata;
	u32 ichg1_min = 0, aicr1_min = 0;
	int ret = 0;

	pdata = &info->chg1_data;

	pdata->input_current_limit =
		info->data.pe40_single_charger_input_current;
	pdata->charging_current_limit =
		info->data.pe40_single_charger_current;

	if (pdata->thermal_charging_current_limit != -1) {
		if (pdata->thermal_charging_current_limit <
		    pdata->charging_current_limit)
			pdata->charging_current_limit =
					pdata->thermal_charging_current_limit;
	}

	if (pdata->thermal_input_current_limit != -1) {
		if (pdata->thermal_input_current_limit <
		    pdata->input_current_limit)
			pdata->input_current_limit =
					pdata->thermal_input_current_limit;
	}

	ret = charger_dev_get_min_charging_current(info->chg1_dev, &ichg1_min);
	if (ret != -ENOTSUPP && pdata->charging_current_limit < ichg1_min)
		pdata->charging_current_limit = 0;

	ret = charger_dev_get_min_input_current(info->chg1_dev, &aicr1_min);
	if (ret != -ENOTSUPP && pdata->input_current_limit < aicr1_min)
		pdata->input_current_limit = 0;

	chr_err("force:%d thermal:%d,%d setting:%d %d type:%d usb_unlimited:%d usbif:%d usbsm:%d aicl:%d atm:%d\n",
		_uA_to_mA(pdata->force_charging_current),
		_uA_to_mA(pdata->thermal_input_current_limit),
		_uA_to_mA(pdata->thermal_charging_current_limit),
		_uA_to_mA(pdata->input_current_limit),
		_uA_to_mA(pdata->charging_current_limit),
		info->chr_type, info->usb_unlimited,
		IS_ENABLED(CONFIG_USBIF_COMPLIANCE), info->usb_state,
		pdata->input_current_limit_by_aicl, info->atm_enabled);

	return 0;
}

static int mtk_switch_chr_pe40_run(struct charger_manager *info)
{
	struct charger_custom_data *pdata = &info->data;
	struct switch_charging_alg_data *swchgalg = info->algorithm_data;
	struct pe40_data *data = NULL;
	int ret = 0;

	charger_dev_enable(info->chg1_dev, true);
	select_pe40_charging_current_limit(info);

	data = pe40_get_data();
	if (!data) {
		chr_err("%s: data is NULL\n", __func__);
		goto stop;
	}

	data->input_current_limit = info->chg1_data.input_current_limit;
	data->charging_current_limit = info->chg1_data.input_current_limit;
	data->pe40_max_vbus = pdata->pe40_max_vbus;
	data->high_temp_to_leave_pe40 = pdata->high_temp_to_leave_pe40;
	data->high_temp_to_enter_pe40 = pdata->high_temp_to_enter_pe40;
	data->low_temp_to_leave_pe40 = pdata->low_temp_to_leave_pe40;
	data->low_temp_to_enter_pe40 = pdata->low_temp_to_enter_pe40;
	data->pe40_r_cable_1a_lower = pdata->pe40_r_cable_1a_lower;
	data->pe40_r_cable_2a_lower = pdata->pe40_r_cable_2a_lower;
	data->pe40_r_cable_3a_lower = pdata->pe40_r_cable_3a_lower;

	data->battery_cv = pdata->battery_cv;
	if (info->enable_sw_jeita) {
		if (info->sw_jeita.cv != 0)
			data->battery_cv = info->sw_jeita.cv;
	}

	if (info->enable_hv_charging == false)
		goto stop;
	if (info->pd_reset == true) {
		chr_err("encounter hard reset, stop pe4.0\n");
		info->pd_reset = false;
		goto stop;
	}

	ret = pe40_run();

	if (ret == 1) {
		chr_err("retry pe4\n");
		goto retry;
	}

	if (ret == 2 &&
		info->chg1_data.thermal_charging_current_limit == -1 &&
		info->chg1_data.thermal_input_current_limit == -1) {
		chr_err("leave pe4\n");
		info->leave_pe4 = true;
		swchgalg->state = CHR_CC;
	}

	return 0;

stop:
	pe40_stop();
retry:
	swchgalg->state = CHR_CC;

	return 0;
}


static int mtk_switch_chr_pdc_init(struct charger_manager *info)
{
	int ret;

	ret = pdc_init();

	if (ret == 0)
		set_charger_manager(info);

	info->leave_pdc = false;
	info->stop_pdc_with_dis_hv = false;

	return 0;
}

static int select_pdc_charging_current_limit(struct charger_manager *info)
{
	struct charger_data *pdata;
	u32 ichg1_min = 0, aicr1_min = 0;
	int ret = 0;

	pdata = &info->chg1_data;

	pdata->input_current_limit =
		info->data.pd_charger_current;
	pdata->charging_current_limit =
		info->data.pd_charger_current;

	if (pdata->thermal_charging_current_limit != -1) {
		if (pdata->thermal_charging_current_limit <
		    pdata->charging_current_limit)
			pdata->charging_current_limit =
					pdata->thermal_charging_current_limit;
	}

	if (pdata->thermal_input_current_limit != -1) {
		if (pdata->thermal_input_current_limit <
		    pdata->input_current_limit)
			pdata->input_current_limit =
					pdata->thermal_input_current_limit;
	}

	ret = charger_dev_get_min_charging_current(info->chg1_dev, &ichg1_min);
	if (ret != -ENOTSUPP && pdata->charging_current_limit < ichg1_min)
		pdata->charging_current_limit = 0;

	ret = charger_dev_get_min_input_current(info->chg1_dev, &aicr1_min);
	if (ret != -ENOTSUPP && pdata->input_current_limit < aicr1_min)
		pdata->input_current_limit = 0;

	chr_err("force:%d thermal:%d,%d setting:%d %d type:%d usb_unlimited:%d usbif:%d usbsm:%d aicl:%d atm:%d\n",
		_uA_to_mA(pdata->force_charging_current),
		_uA_to_mA(pdata->thermal_input_current_limit),
		_uA_to_mA(pdata->thermal_charging_current_limit),
		_uA_to_mA(pdata->input_current_limit),
		_uA_to_mA(pdata->charging_current_limit),
		info->chr_type, info->usb_unlimited,
		IS_ENABLED(CONFIG_USBIF_COMPLIANCE), info->usb_state,
		pdata->input_current_limit_by_aicl, info->atm_enabled);

	return 0;
}

static int mtk_switch_chr_pdc_run(struct charger_manager *info)
{
	struct charger_custom_data *pdata = &info->data;
	struct switch_charging_alg_data *swchgalg = info->algorithm_data;
	struct pdc_data *data = NULL;
	int ret = 0;

	charger_dev_enable(info->chg1_dev, true);
	select_pdc_charging_current_limit(info);

	data = pdc_get_data();

	data->input_current_limit = info->chg1_data.input_current_limit;
	data->charging_current_limit = info->chg1_data.input_current_limit;
	data->pd_vbus_low_bound = pdata->pd_vbus_low_bound;
	data->pd_vbus_upper_bound = pdata->pd_vbus_upper_bound;

	data->battery_cv = pdata->battery_cv;
	if (info->enable_sw_jeita) {
		if (info->sw_jeita.cv != 0)
			data->battery_cv = info->sw_jeita.cv;
	}

	if (info->enable_hv_charging == false) {
		info->stop_pdc_with_dis_hv = true;
		goto stop;
	} else {
		info->stop_pdc_with_dis_hv = false;
	}

	if (adapter_is_support_pd_pps()) {
		chr_err("PD2.0 is missing. leave pdc\n");
		goto stop;
	}

	info->is_pdc_run = true;
	ret = pdc_run();

	if (ret == 2 &&
		info->chg1_data.thermal_charging_current_limit == -1 &&
		info->chg1_data.thermal_input_current_limit == -1) {
		chr_err("leave pdc\n");
		info->leave_pdc = true;
		info->is_pdc_run = false;
		swchgalg->state = CHR_CC;
	}

	return 0;

stop:
	pdc_stop();
	swchgalg->state = CHR_CC;
	info->is_pdc_run = false;
	return 0;
}


/* return false if total charging time exceeds max_charging_time */
static bool mtk_switch_check_charging_time(struct charger_manager *info)
{
	struct switch_charging_alg_data *swchgalg = info->algorithm_data;
	struct timespec time_now;

	if (info->enable_sw_safety_timer) {
		get_monotonic_boottime(&time_now);
		chr_debug("%s: begin: %ld, now: %ld\n", __func__,
			swchgalg->charging_begin_time.tv_sec, time_now.tv_sec);

		if (swchgalg->total_charging_time >=
		    info->data.max_charging_time) {
			chr_err("%s: SW safety timeout: %d sec > %d sec\n",
				__func__, swchgalg->total_charging_time,
				info->data.max_charging_time);
			charger_dev_notify(info->chg1_dev,
					CHARGER_DEV_NOTIFY_SAFETY_TIMEOUT);
			return false;
		}
	}

	return true;
}

int get_battery_capacity(void)
{
	struct power_supply	*battery_psy;
	union power_supply_propval val = {0,};

	battery_psy = power_supply_get_by_name("battery");
	if (battery_psy)
		power_supply_get_property(battery_psy,
				POWER_SUPPLY_PROP_CAPACITY, &val);

	return val.intval;
}

static int mtk_switch_chr_cc(struct charger_manager *info)
{
	bool chg_done = false;
	struct switch_charging_alg_data *swchgalg = info->algorithm_data;
	struct timespec time_now, charging_time;
	int tmp = battery_get_bat_temperature();
#ifdef CONFIG_BQ2597X_CHARGE_PUMP
	int battery_capacity = 0;
	static int qc2_recheck = 0;
	bool bq_charge_done = false;
	struct power_supply *usb_psy;
	struct power_supply *charger_identify_psy;
	struct mt_charger *mt_chg = NULL;
	if(info->usb_psy){
		pr_err("ww_test usb_psy is NULL!/n");
		mt_chg = power_supply_get_drvdata(info->usb_psy);
	}
	usb_psy = power_supply_get_by_name("usb");
	charger_identify_psy = power_supply_get_by_name("Charger_Identify");
	bq_charge_done = get_bq_charge_done(info);
	battery_capacity = get_battery_capacity();
	info->temp_level = charger_manager_get_prop_system_temp_level();
#endif

	if (info->enable_hv_charging == true)
		set_hv_charge_enable(info, true);

	/* check bif */
	if (IS_ENABLED(CONFIG_MTK_BIF_SUPPORT)) {
		if (pmic_is_bif_exist() != 1) {
			chr_err("CONFIG_MTK_BIF_SUPPORT but no bif , stop charging\n");
			swchgalg->state = CHR_ERROR;
			charger_manager_notifier(info, CHARGER_NOTIFY_ERROR);
		}
	}

	get_monotonic_boottime(&time_now);
	charging_time = timespec_sub(time_now, swchgalg->charging_begin_time);

	swchgalg->total_charging_time = charging_time.tv_sec;

	chr_err("pe40_ready:%d pps:%d hv:%d thermal:%d,%d tmp:%d,%d,%d,xm_pps:%d,pd20:%d,leave_pdc:%d\n",
		info->enable_pe_4,
		pe40_is_ready(),
		info->enable_hv_charging,
		info->chg1_data.thermal_charging_current_limit,
		info->chg1_data.thermal_input_current_limit,
		tmp,
		info->data.high_temp_to_enter_pe40,
		info->data.low_temp_to_enter_pe40,
		adapter_is_support_pd_pps(),
		pdc_is_ready(),
		info->leave_pdc);

/*
	if (info->enable_pe_5 && pe50_is_ready() && !info->leave_pe5) {
		if (info->enable_hv_charging == true) {
			chr_err("enter PE5.0\n");
			swchgalg->state = CHR_PE50;
			info->pe5.online = true;
			if (mtk_pe20_get_is_enable(info)) {
				mtk_pe20_set_is_enable(info, false);
				if (mtk_pe20_get_is_connect(info))
					mtk_pe20_reset_ta_vchr(info);
			}

			if (mtk_pe_get_is_enable(info)) {
				mtk_pe_set_is_enable(info, false);
				if (mtk_pe_get_is_connect(info))
					mtk_pe_reset_ta_vchr(info);
			}
			return 1;
		}
	}

	if (info->enable_pe_4 &&
		pe40_is_ready() &&
		!info->leave_pe4) {
		if (info->enable_hv_charging == true &&
			info->chg1_data.thermal_charging_current_limit == -1 &&
			info->chg1_data.thermal_input_current_limit == -1) {
			chr_err("enter PE4.0!\n");
			swchgalg->state = CHR_PE40;
			if (mtk_pe20_get_is_enable(info)) {
				mtk_pe20_set_is_enable(info, false);
				if (mtk_pe20_get_is_connect(info))
					mtk_pe20_reset_ta_vchr(info);
			}

			if (mtk_pe_get_is_enable(info)) {
				mtk_pe_set_is_enable(info, false);
				if (mtk_pe_get_is_connect(info))
					mtk_pe_reset_ta_vchr(info);
			}
			return 1;
		}
	}
*/

	if (mt_chg->usb_desc.type != POWER_SUPPLY_TYPE_USB_HVDCP)
		qc2_recheck = 0;

#ifdef CONFIG_BQ2597X_CHARGE_PUMP
	if (adapter_is_support_pd_pps() &&
		battery_capacity < 90 && !bq_charge_done &&
		(info->sw_jeita.sm == TEMP_T2_TO_T3 ||
		info->sw_jeita.sm == TEMP_T1P5_TO_T2)) {
		chr_err("%s: enter xiaomi_PD-PM!\n", __func__);
		mt_chg->usb_desc.type = POWER_SUPPLY_TYPE_USB_PD;
		swchgalg->state = CHR_XM_PD_PM;
		charger_enable_vbus_ovp(info, false);
		charger_manager_set_prop_system_temp_level(info->temp_level);
		charger_dev_enable(info->chg1_dev, true);
		charger_dev_set_input_current(info->chg1_dev, 100000);
		charger_dev_set_charging_current(info->chg1_dev, 3000000);
		power_supply_changed(usb_psy);
		return 1;
	}
#endif

	if (pdc_is_ready() &&
		!adapter_is_support_pd_pps() &&
		!info->leave_pdc) {
		if (info->enable_hv_charging == true) {
			chr_err("%s: enter PDC!\n", __func__);
			info->stop_pdc_with_dis_hv = false;
			swchgalg->state = CHR_PDC;
			mt_chg->usb_desc.type = POWER_SUPPLY_TYPE_USB_PD;
			charger_enable_vbus_ovp(info, false);
			if (mtk_pe20_get_is_enable(info)) {
				mtk_pe20_set_is_enable(info, false);
				if (mtk_pe20_get_is_connect(info))
					mtk_pe20_reset_ta_vchr(info);
			}

			if (mtk_pe_get_is_enable(info)) {
				mtk_pe_set_is_enable(info, false);
				if (mtk_pe_get_is_connect(info))
					mtk_pe_reset_ta_vchr(info);
			}
			return 1;
		} else {
			info->stop_pdc_with_dis_hv = true;
		}
	}

#ifdef CONFIG_BQ2597X_CHARGE_PUMP
	if (!mtk_pdc_check_charger(info) && battery_capacity < 90 && !bq_charge_done &&
		(info->sw_jeita.sm == TEMP_T2_TO_T3 || info->sw_jeita.sm == TEMP_T1P5_TO_T2)) {
		if (mt_chg->usb_desc.type == POWER_SUPPLY_TYPE_USB_HVDCP_3 ||
			mt_chg->usb_desc.type == POWER_SUPPLY_TYPE_USB_HVDCP_3P5) {
			chr_err("%s: enter qc3.0 and qc35\n", __func__);
			charger_manager_set_prop_system_temp_level(info->temp_level);
			swchgalg->state = CHR_XM_QC3;
			charger_dev_enable(info->chg1_dev, true);
			charger_dev_set_input_current(info->chg1_dev, 100000);
			charger_dev_set_charging_current(info->chg1_dev, 3000000);
			power_supply_changed(usb_psy);
			return 1;
		}
	}

	if (mt_chg->usb_desc.type == POWER_SUPPLY_TYPE_USB_HVDCP) {
		charger_manager_set_prop_system_temp_level(info->temp_level);
		if (!charger_identify_psy)
			return 0;
		chr_err("%s: enter qc2.0\n", __func__);
		swchgalg->state = CHR_XM_QC20;
	}
#endif

	swchg_turn_on_charging(info);

	charger_dev_is_charging_done(info->chg1_dev, &chg_done);
	if (chg_done) {
		swchgalg->state = CHR_BATFULL;
		charger_dev_do_event(info->chg1_dev, EVENT_EOC, 0);
		chr_err("%s: battery full!\n", __func__);
	}

	return 0;
}

static int mtk_switch_chr_err(struct charger_manager *info)
{
	struct switch_charging_alg_data *swchgalg = info->algorithm_data;

	if (info->enable_sw_jeita) {
		if ((info->sw_jeita.sm == TEMP_BELOW_T0) ||
			(info->sw_jeita.sm == TEMP_ABOVE_T4))
			info->sw_jeita.error_recovery_flag = false;

		if ((info->sw_jeita.error_recovery_flag == false) &&
			(info->sw_jeita.sm != TEMP_BELOW_T0) &&
			(info->sw_jeita.sm != TEMP_ABOVE_T4)) {
			info->sw_jeita.error_recovery_flag = true;
			swchgalg->state = CHR_CC;
			get_monotonic_boottime(&swchgalg->charging_begin_time);
		}
	}

	swchgalg->total_charging_time = 0;

	_disable_all_charging(info);
	return 0;
}

static int mtk_switch_chr_full(struct charger_manager *info)
{
	bool chg_done = false;
	struct switch_charging_alg_data *swchgalg = info->algorithm_data;

	swchgalg->total_charging_time = 0;

	/* turn off LED */

	/*
	 * If CV is set to lower value by JEITA,
	 * Reset CV to normal value if temperture is in normal zone
	 */
	swchg_select_cv(info);
	info->polling_interval = CHARGING_FULL_INTERVAL;
	charger_dev_is_charging_done(info->chg1_dev, &chg_done);
	if (!chg_done) {
		swchgalg->state = CHR_CC;
		charger_dev_do_event(info->chg1_dev, EVENT_RECHARGE, 0);
		mtk_pe20_set_to_check_chr_type(info, true);
		mtk_pe_set_to_check_chr_type(info, true);
		info->enable_dynamic_cv = true;
		get_monotonic_boottime(&swchgalg->charging_begin_time);
		chr_err("battery recharging!\n");
		info->polling_interval = CHARGING_INTERVAL;
	}

	return 0;
}

#ifdef CONFIG_BQ2597X_CHARGE_PUMP
static int mtk_switch_chr_xm_pd_pm_run(struct charger_manager *info)
{
	struct switch_charging_alg_data *swchgalg = info->algorithm_data;
	static bool bq_charge_done;

	if (info->enable_hv_charging == false)
		set_hv_charge_enable(info, false);

	bq_charge_done = get_bq_charge_done(info);
	if (adapter_is_support_pd_pps() == false || bq_charge_done) {
		swchgalg->state = CHR_CC;
	}

	return 0;
}
static int mtk_switch_chr_xm_qc3_pm_run(struct charger_manager *info)
{
	static bool bq_charge_done;
	struct switch_charging_alg_data *swchgalg = info->algorithm_data;

	if (info->enable_hv_charging == false)
		set_hv_charge_enable(info, false);

	bq_charge_done = get_bq_charge_done(info);
	get_usb_type(info);
	if ((info->usb_type == POWER_SUPPLY_TYPE_USB_HVDCP_3 ||
		info->usb_type == POWER_SUPPLY_TYPE_USB_HVDCP_3P5) &&
		!bq_charge_done) {
	} else {
		swchgalg->state = CHR_CC;
	}

	return 0;
}
#endif

static int mtk_switch_charging_current(struct charger_manager *info)
{
	swchg_select_charging_current_limit(info);
	return 0;
}

static int mtk_switch_charging_run(struct charger_manager *info)
{
	struct switch_charging_alg_data *swchgalg = info->algorithm_data;
	int ret = 0;
	union power_supply_propval val = {0,};

	chr_err("%s [%d %d], timer=%d\n", __func__, swchgalg->state,
		info->pd_type,
		swchgalg->total_charging_time);

	if (info->usb_psy) {
		power_supply_get_property(info->usb_psy, POWER_SUPPLY_PROP_VOLTAGE_NOW, &val);
		swchgalg->vbus_mv = val.intval;
		if (swchgalg->vbus_mv < INVALID_VBUS_THRE) {
			chr_err("%s vbus invalid: %d.\n", __func__, swchgalg->vbus_mv);
			return 0;
		}
	}

	if (mtk_pdc_check_charger(info) == false &&
	    mtk_is_TA_support_pd_pps(info) == false) {
		mtk_pe20_check_charger(info);
		if (mtk_pe20_get_is_connect(info) == false)
			mtk_pe_check_charger(info);
	}

	do {
		switch (swchgalg->state) {
			chr_err("%s_2 [%d] %d\n", __func__, swchgalg->state,
				info->pd_type);
		case CHR_CC:
#ifdef CONFIG_BQ2597X_CHARGE_PUMP
		case CHR_XM_QC20:
#endif
			ret = mtk_switch_chr_cc(info);
			break;

		case CHR_PE50:
			ret = mtk_switch_chr_pe50_run(info);
			break;

		case CHR_PE40:
			ret = mtk_switch_chr_pe40_run(info);
			break;

		case CHR_PDC:
			ret = mtk_switch_chr_pdc_run(info);
			break;

		case CHR_BATFULL:
			ret = mtk_switch_chr_full(info);
			break;

		case CHR_ERROR:
			ret = mtk_switch_chr_err(info);
			break;
#ifdef CONFIG_BQ2597X_CHARGE_PUMP
		case CHR_XM_PD_PM:
			ret = mtk_switch_chr_xm_pd_pm_run(info);
			break;

		case CHR_XM_QC3:
			ret = mtk_switch_chr_xm_qc3_pm_run(info);
			break;
#endif
		}
	} while (ret != 0);
	mtk_switch_check_charging_time(info);

	charger_dev_dump_registers(info->chg1_dev);
	charger_dev_dump_registers(info->chg2_dev);
	return 0;
}

static int charger_dev_event(struct notifier_block *nb,
	unsigned long event, void *v)
{
	struct charger_manager *info =
			container_of(nb, struct charger_manager, chg1_nb);
	struct chgdev_notify *data = v;

	chr_info("%s %ld", __func__, event);

	switch (event) {
	case CHARGER_DEV_NOTIFY_EOC:
		charger_manager_notifier(info, CHARGER_NOTIFY_EOC);
		pr_info("%s: end of charge\n", __func__);
		break;
	case CHARGER_DEV_NOTIFY_RECHG:
		charger_manager_notifier(info, CHARGER_NOTIFY_START_CHARGING);
		pr_info("%s: recharge\n", __func__);
		break;
	case CHARGER_DEV_NOTIFY_SAFETY_TIMEOUT:
		info->safety_timeout = true;
		chr_err("%s: safety timer timeout\n", __func__);

		/* If sw safety timer timeout, do not wake up charger thread */
		if (info->enable_sw_safety_timer)
			return NOTIFY_DONE;
		break;
	case CHARGER_DEV_NOTIFY_VBUS_OVP:
		info->vbusov_stat = data->vbusov_stat;
		chr_err("%s: vbus ovp = %d\n", __func__, info->vbusov_stat);
		break;
	default:
		return NOTIFY_DONE;
	}

	if (info->chg1_dev->is_polling_mode == false)
		_wake_up_charger(info);

	return NOTIFY_DONE;
}

static int dvchg1_dev_event(struct notifier_block *nb, unsigned long event,
			    void *data)
{
	struct charger_manager *info =
			container_of(nb, struct charger_manager, dvchg1_nb);

	chr_info("%s %ld", __func__, event);

	return mtk_pe50_notifier_call(info, MTK_PE50_NOTISRC_CHG, event, data);
}

static int dvchg2_dev_event(struct notifier_block *nb, unsigned long event,
			    void *data)
{
	struct charger_manager *info =
			container_of(nb, struct charger_manager, dvchg2_nb);

	chr_info("%s %ld", __func__, event);

	return mtk_pe50_notifier_call(info, MTK_PE50_NOTISRC_CHG, event, data);
}

int mtk_switch_charging_init2(struct charger_manager *info)
{
	struct switch_charging_alg_data *swch_alg;

	swch_alg = devm_kzalloc(&info->pdev->dev,
				sizeof(*swch_alg), GFP_KERNEL);
	if (!swch_alg)
		return -ENOMEM;

	info->chg1_dev = get_charger_by_name("primary_chg");
	if (info->chg1_dev)
		chr_err("Found primary charger [%s]\n",
			info->chg1_dev->props.alias_name);
	else
		chr_err("*** Error : can't find primary charger ***\n");

	info->chg2_dev = get_charger_by_name("secondary_chg");
	if (info->chg2_dev)
		chr_info("Found secondary charger [%s]\n",
			info->chg2_dev->props.alias_name);
	else
		chr_err("*** Error: can't find secondary charger\n");
	info->dvchg1_dev = get_charger_by_name("primary_divider_chg");
	if (info->dvchg1_dev) {
		chr_err("Found primary divider charger [%s]\n",
			info->dvchg1_dev->props.alias_name);
		info->dvchg1_nb.notifier_call = dvchg1_dev_event;
		register_charger_device_notifier(info->dvchg1_dev,
						 &info->dvchg1_nb);
	} else
		chr_err("Can't find primary divider charger\n");
	info->dvchg2_dev = get_charger_by_name("secondary_divider_chg");
	if (info->dvchg2_dev) {
		chr_err("Found secondary divider charger [%s]\n",
			info->dvchg2_dev->props.alias_name);
		info->dvchg2_nb.notifier_call = dvchg2_dev_event;
		register_charger_device_notifier(info->dvchg2_dev,
						 &info->dvchg2_nb);
	} else
		chr_err("Can't find secondary divider charger\n");

	mutex_init(&swch_alg->ichg_aicr_access_mutex);

	info->algorithm_data = swch_alg;
	info->do_algorithm = mtk_switch_charging_run;
	info->plug_in = mtk_switch_charging_plug_in;
	info->plug_out = mtk_switch_charging_plug_out;
	info->do_charging = mtk_switch_charging_do_charging;
	info->do_event = charger_dev_event;
	info->change_current_setting = mtk_switch_charging_current;

	mtk_switch_chr_pe50_init(info);
	mtk_switch_chr_pe40_init(info);
	mtk_switch_chr_pdc_init(info);

	return 0;
}
