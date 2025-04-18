/*
 * State machine for qc3 when it works on cp
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *    Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 *    Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the
 *    distribution.
 *
 *    Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#define pr_fmt(fmt)	"[FC2-PM]: %s: " fmt, __func__
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/device.h>
#include <linux/power_supply.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/wait.h>
#include <linux/types.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
//#include <linux/pmic-voter.h>

#include <mt-plat/v1/charger_class.h>
#include <mt-plat/v1/mtk_charger.h>

#include "cp_qc30.h"

#define BATT_MAX_CHG_VOLT		4450
#define BATT_FAST_CHG_CURR		5400
#define	BUS_OVP_THRESHOLD		12000
#define	BUS_OVP_ALARM_THRESHOLD_QC30		9500
#define	BUS_OVP_ALARM_THRESHOLD_QC35		9500

#define BUS_VOLT_INIT_UP		300

#define BAT_VOLT_LOOP_LMT		BATT_MAX_CHG_VOLT
#define BAT_CURR_LOOP_LMT		BATT_FAST_CHG_CURR
#define BUS_VOLT_LOOP_LMT		BUS_OVP_THRESHOLD

#define VOLT_UP		true
#define VOLT_DOWN	false

#define ADC_ERR			1
#define CP_ENABLE_FAIL			2

/* registers parameter */
// mode
#define XMUSB350_MODE_QC20_V5			0x01
#define XMUSB350_MODE_QC20_V9			0x02
#define XMUSB350_MODE_QC20_V12			0x03
#define XMUSB350_MODE_QC30_V5			0x04
#define XMUSB350_MODE_QC3_PLUS_V5		0x05

enum {
	CP_VBUS_ERROR_NONE,
	CP_VBUS_ERROR_LOW,
	CP_VBUS_ERROR_HIGHT,
};

static struct sys_config sys_config = {
	.bat_volt_lp_lmt		= BAT_VOLT_LOOP_LMT,
	.bat_curr_lp_lmt		= BAT_CURR_LOOP_LMT/* + 1000*/,
	.bus_volt_lp_lmt		= BUS_VOLT_LOOP_LMT,
	.bus_curr_lp_lmt		= BAT_CURR_LOOP_LMT >> 1,
	.bus_volt_alarm_threshold = BUS_OVP_ALARM_THRESHOLD_QC30,

	.fc2_taper_current		= 1500,
	.flash2_policy.down_steps	= -1,
	.flash2_policy.volt_hysteresis	= 50,

	.min_vbat_start_flash2		= 3500,
	.cp_sec_enable			= false,
};

struct cp_qc30_data {
	struct device *dev;
	int			bat_volt_max;
	int			bat_curr_max;
	int			bus_volt_max;
	int			bus_curr_max;
	int			non_ffc_bat_volt_max;

	int			qc3_27w_bat_curr_max;
	int			qc3_27w_bus_curr_max;
	int			qc3p_bat_curr_max;
	int			qc3p_bus_curr_max;
	bool			cp_sec_enable;

	/* notifiers */
	struct notifier_block	nb;

	struct delayed_work	qc3_pm_work;
};

static pm_t pm_state;

static int fc2_taper_timer;
static int ibus_lmt_change_timer;

struct charger_device *ch_dev = NULL;
static struct charger_consumer *chg_consumer = NULL;
static struct cp_qc30_data *p_chip = NULL;

static struct power_supply *cp_get_sw_psy(void)
{

	if (!pm_state.sw_psy)
		pm_state.sw_psy = power_supply_get_by_name("battery");

	return pm_state.sw_psy;
}

static struct power_supply *cp_get_usb_psy(void)
{

	if (!pm_state.usb_psy)
		pm_state.usb_psy = power_supply_get_by_name("usb");

	return pm_state.usb_psy;
}

static struct power_supply *cp_get_bms_psy(void)
{
	if (!pm_state.bms_psy)
		pm_state.bms_psy = power_supply_get_by_name("bms");

	return pm_state.bms_psy;
}

static int cp_get_effective_fcc_val(pm_t pm_state)
{
	int effective_fcc_val = 0;
	struct power_supply *psy;
	union power_supply_propval val = {0,};
	int ret;

	psy = cp_get_sw_psy();
	if (!psy)
		return 0;

	ret = power_supply_get_property(psy,
			POWER_SUPPLY_PROP_FAST_CHARGE_CURRENT, &val);
	if (!ret)
		effective_fcc_val = val.intval / 1000;

	pr_info("%s effective_fcc_val = %d.\n", __func__, effective_fcc_val);
	return effective_fcc_val;
}

static bool qc3_get_fastcharge_mode_enabled(pm_t pm_state)
{
	union power_supply_propval pval = {0,};
	int rc;

	if (!pm_state.bms_psy)
		return false;

	rc = power_supply_get_property(pm_state.bms_psy,
				POWER_SUPPLY_PROP_FASTCHARGE_MODE, &pval);
	if (rc < 0) {
		pr_info("Couldn't get fastcharge mode:%d\n", rc);
		return false;
	}

	pr_info("fastcharge mode: %d\n", pval.intval);

	if (pval.intval == 1)
		return true;
	else
		return false;
}

static struct power_supply *cp_get_fc_psy(void)
{
	if (!pm_state.fc_psy) {
		if (sys_config.cp_sec_enable)
			pm_state.fc_psy = power_supply_get_by_name("bq2597x-master");
		else
			pm_state.fc_psy = power_supply_get_by_name("bq2597x-standalone");
	}

	return pm_state.fc_psy;
}

static struct power_supply *cp_get_charger_identify_psy(void)
{

	if (!pm_state.charger_identify_psy)
		pm_state.charger_identify_psy = power_supply_get_by_name("Charger_Identify");

	return pm_state.charger_identify_psy;
}

static void cp_update_bms_ibat(void)
{
	int ret;
	struct power_supply *psy;
	union power_supply_propval val = {0,};

	psy = cp_get_bms_psy();
	if (!psy)
		return;

	ret = power_supply_get_property(psy,
			POWER_SUPPLY_PROP_CURRENT_NOW, &val);
	if (!ret)
		pm_state.ibat_now = -(val.intval / 1000);

}

/* set fastcharge mode to enable or disabled */
static int qc3_set_bms_fastcharge_mode(bool enable)
{
	union power_supply_propval pval = {0,};
	int rc;

	/* do not enable ffc for qc3 class-b now */
	return 0;

	cp_get_usb_psy();

	if (!pm_state.usb_psy)
		return 0;

	pval.intval = enable;

	rc = power_supply_set_property(pm_state.usb_psy,
				POWER_SUPPLY_PROP_FASTCHARGE_MODE, &pval);
	if (rc < 0) {
		pr_err("Couldn't write fastcharge mode:%d\n", rc);
		return rc;
	}

	pm_state.bms_fastcharge_mode = enable;

	return 0;
}

/* get thermal level from battery power supply property */
static int qc3_get_batt_current_thermal_level(int *level)
{
	int rc = 0;
	struct power_supply *psy;
	union power_supply_propval val = {0,};

	psy = cp_get_sw_psy();
	if (!psy)
		return 0;

	rc = power_supply_get_property(psy,
			POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT, &val);

	if (rc < 0) {
		pr_err("Couldn't get thermal level:%d\n", rc);
		return rc;
	}

	pr_info("val.intval: %d\n", val.intval);

	*level = val.intval;
	return rc;
}

/* determine whether to disable cp according to jeita status */
static bool qc3_disable_cp_by_jeita_status(void)
{
	int batt_temp = 0, bq_input_suspend = 0;
	int ret, bat_constant_voltage;
	bool is_fastcharge_mode = false;
	struct power_supply *psy;
	union power_supply_propval val = {0,};

	psy = cp_get_sw_psy();
	if (!psy)
		return false;

	ret = power_supply_get_property(psy,
			POWER_SUPPLY_PROP_INPUT_SUSPEND, &val);
	if (!ret)
		bq_input_suspend = !!val.intval;

	psy = cp_get_bms_psy();
	if (!psy)
		return false;

	ret = power_supply_get_property(psy,
			POWER_SUPPLY_PROP_TEMP, &val);

	if (ret < 0) {
		pr_info("Couldn't get batt temp prop:%d\n", ret);
		return false;
	}

	batt_temp = val.intval;
	pr_err("batt_temp: %d\n", batt_temp);

	bat_constant_voltage = sys_config.bat_volt_lp_lmt;
	is_fastcharge_mode = qc3_get_fastcharge_mode_enabled(pm_state);
	if (is_fastcharge_mode)
		bat_constant_voltage += 10;

	if (!ch_dev)
		ch_dev = get_charger_by_name("primary_chg");

	if (bq_input_suspend) {
		return true;
	} else {
		if (batt_temp >= JEITA_WARM_THR && !pm_state.jeita_triggered) {
			pm_state.jeita_triggered = true;
			return true;
		} else if (batt_temp <= JEITA_COOL_NOT_ALLOW_CP_THR
				&& !pm_state.jeita_triggered) {
			pm_state.jeita_triggered = true;
			return true;
		} else if ((batt_temp <= (JEITA_WARM_THR - JEITA_HYSTERESIS))
					&& (batt_temp >= (JEITA_COOL_NOT_ALLOW_CP_THR + JEITA_HYSTERESIS))
				&& pm_state.jeita_triggered) {

			if (ch_dev)
				charger_dev_set_constant_voltage(ch_dev, (bat_constant_voltage * 1000));

			pm_state.jeita_triggered = false;
			return false;
		} else {
			return pm_state.jeita_triggered;
		}
	}
}

static void cp_update_sw_status(void)
{
	//int ret;
	struct power_supply *psy;
	//union power_supply_propval val = {0,};

	psy = cp_get_sw_psy();
	if (!psy)
		return;
#if 0
	ret = power_supply_get_property(psy,
			POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED, &val);
	if (!ret)
		pm_state.sw_chager.charge_enabled = val.intval;
#endif
	pm_state.sw_chager.charge_enabled = 1;
}

static void cp_update_fc_status(void)
{
	int ret;
	struct power_supply *psy;
	union power_supply_propval val = {0,};

	pr_debug("%s enter.\n", __func__);
	psy = cp_get_fc_psy();
	if (!psy)
		return;

	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_TI_BATTERY_VOLTAGE, &val);
	if (!ret)
		pm_state.bq2597x.vbat_volt = val.intval;

	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_TI_BATTERY_CURRENT, &val);
	if (!ret)
		pm_state.bq2597x.ibat_curr = val.intval;
	pr_debug("%s pm_state.bq2597x.ibat_curr form bq is %d.\n", __func__, pm_state.bq2597x.ibat_curr);

	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_TI_BUS_VOLTAGE, &val);
	if (!ret)
		pm_state.bq2597x.vbus_volt = val.intval;

	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_TI_BUS_CURRENT, &val);
	if (!ret)
		pm_state.bq2597x.ibus_curr = val.intval;

	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_TI_BUS_TEMPERATURE, &val);
	if (!ret)
		pm_state.bq2597x.bus_temp = val.intval;

	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_TI_BATTERY_TEMPERATURE, &val);
	if (!ret)
		pm_state.bq2597x.bat_temp = val.intval;

	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_TI_DIE_TEMPERATURE, &val);
	if (!ret)
		pm_state.bq2597x.die_temp = val.intval;

	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_TI_BATTERY_PRESENT, &val);
	if (!ret)
		pm_state.bq2597x.batt_pres = val.intval;

	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_TI_VBUS_PRESENT, &val);
	if (!ret)
		pm_state.bq2597x.vbus_pres = val.intval;

	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_TI_BUS_ERROR_STATUS, &val);
	if (!ret)
		pm_state.bq2597x.bus_error_status = val.intval;

//	if (pm_state.bq2597x.vbus_pres == 1) {
	if (1) {//J7 bms ibatt is more accurate than BQ, use bms ibatt
		cp_update_bms_ibat();
		pm_state.bq2597x.ibat_curr = pm_state.ibat_now;
	}
	pr_debug("%s pm_state.bq2597x.ibat_curr form bms is %d.\n", __func__, pm_state.bq2597x.ibat_curr);

	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_CHARGE_ENABLED, &val);
	if (!ret)
		pm_state.bq2597x.charge_enabled = val.intval;

	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_TI_ALARM_STATUS, &val);
	if (!ret) {
		pm_state.bq2597x.bat_ovp_alarm = !!(val.intval & BAT_OVP_ALARM_MASK);
		pm_state.bq2597x.bat_ocp_alarm = !!(val.intval & BAT_OCP_ALARM_MASK);
		pm_state.bq2597x.bus_ovp_alarm = !!(val.intval & BUS_OVP_ALARM_MASK);
		pm_state.bq2597x.bus_ocp_alarm = !!(val.intval & BUS_OCP_ALARM_MASK);
		pm_state.bq2597x.bat_ucp_alarm = !!(val.intval & BAT_UCP_ALARM_MASK);
		pm_state.bq2597x.bat_therm_alarm = !!(val.intval & BAT_THERM_ALARM_MASK);
		pm_state.bq2597x.bus_therm_alarm = !!(val.intval & BUS_THERM_ALARM_MASK);
		pm_state.bq2597x.die_therm_alarm = !!(val.intval & DIE_THERM_ALARM_MASK);
	}

	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_TI_FAULT_STATUS, &val);
	if (!ret) {
		pm_state.bq2597x.bat_ovp_fault = !!(val.intval & BAT_OVP_FAULT_MASK);
		pm_state.bq2597x.bat_ocp_fault = !!(val.intval & BAT_OCP_FAULT_MASK);
		pm_state.bq2597x.bus_ovp_fault = !!(val.intval & BUS_OVP_FAULT_MASK);
		pm_state.bq2597x.bus_ocp_fault = !!(val.intval & BUS_OCP_FAULT_MASK);
		pm_state.bq2597x.bat_therm_fault = !!(val.intval & BAT_THERM_FAULT_MASK);
		pm_state.bq2597x.bus_therm_fault = !!(val.intval & BUS_THERM_FAULT_MASK);
		pm_state.bq2597x.die_therm_fault = !!(val.intval & DIE_THERM_FAULT_MASK);
	}

	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_TI_REG_STATUS, &val);
	if (!ret) {
		pm_state.bq2597x.vbat_reg = !!(val.intval & VBAT_REG_STATUS_MASK);
		pm_state.bq2597x.ibat_reg = !!(val.intval & IBAT_REG_STATUS_MASK);
	}
	pr_debug("%s pm_state.bq2597x.vbat_volt is %d.\n", __func__, pm_state.bq2597x.vbat_volt);
	pr_debug("%s pm_state.bq2597x.vbus_volt is %d, err_hi_lo is %d.\n",
			__func__, pm_state.bq2597x.vbus_volt, pm_state.bq2597x.bus_error_status);
	pr_debug("%s pm_state.bq2597x.ibus_curr is %d.\n", __func__, pm_state.bq2597x.ibus_curr);
	pr_debug("%s pm_state.bq2597x.bus_temp is %d.\n", __func__, pm_state.bq2597x.bus_temp);
	pr_debug("%s pm_state.bq2597x.bat_temp is %d.\n", __func__, pm_state.bq2597x.bat_temp);
	pr_debug("%s pm_state.bq2597x.die_temp is %d.\n", __func__, pm_state.bq2597x.die_temp);
	pr_debug("%s pm_state.bq2597x.batt_pres is %d.\n", __func__, pm_state.bq2597x.batt_pres);
	pr_debug("%s pm_state.bq2597x.vbus_pres is %d.\n", __func__, pm_state.bq2597x.vbus_pres);
	pr_debug("%s pm_state.bq2597x.charge_enabled is %d.\n", __func__, pm_state.bq2597x.charge_enabled);
	//charger_manager_get_ibus(&ibus);
	//pr_debug("%s 6360 ibus is %d.\n", __func__, ibus);
}


static int cp_enable_fc(bool enable)
{
	int ret;
	struct power_supply *psy;
	union power_supply_propval val = {0,};

	psy = cp_get_fc_psy();
	if (!psy)
		return -ENODEV;

	if (!ch_dev)
		ch_dev = get_charger_by_name("primary_chg");

	if (!chg_consumer)
		chg_consumer = charger_manager_get_by_name(p_chip->dev, "charger_port1");

	if ((enable) && (chg_consumer)) {
		pr_info("%s charger_dev_enable_powerpath enable:%d\n", __func__, enable);
		charger_manager_enable_power_path(chg_consumer, MAIN_CHARGER, !enable);
	}

	val.intval = enable;
	ret = power_supply_set_property(psy,
			POWER_SUPPLY_PROP_CHARGING_ENABLED, &val);
	if ((!ret) && (ch_dev)) {
		charger_dev_enable(ch_dev, !enable);
		if (!enable) {
			charger_dev_set_input_current(ch_dev, 3000000);
		}
	}

	return ret;
}

static int cp_set_bq_charge_done(bool enable)
{
	int ret;
	struct power_supply *psy;
	union power_supply_propval val = {0,};

	pr_debug("set bq charge done, ret=%d", enable);
	psy = cp_get_fc_psy();
	if (!psy)
		return -ENODEV;

	val.intval = enable;
	ret = power_supply_set_property(psy,
			POWER_SUPPLY_PROP_BQ_CHARGE_DONE, &val);
	if (ret < 0)
		pr_err("set bq charge done failed, ret=%d", ret);

	return ret;
}

static int cp_get_hv_charge_enable(void)
{
	int ret;
	struct power_supply *psy;
	union power_supply_propval pval = {0,};

	psy = cp_get_fc_psy();
	if (!psy)
		return -ENODEV;

	ret = power_supply_get_property(psy,
			POWER_SUPPLY_PROP_HV_CHARGE_ENABLED, &pval);
	if (ret < 0)
		pr_err("get hv_charge_enable failed, ret=%d", ret);

	return pval.intval;
}

static int cp_set_qc_bus_protections(int hvdcp3_type)
{
	int ret;
	struct power_supply *psy;
	union power_supply_propval val = {0,};

	psy = cp_get_fc_psy();
	if (!psy)
		return -ENODEV;

	val.intval = hvdcp3_type;
	ret = power_supply_set_property(psy,
			POWER_SUPPLY_PROP_TI_SET_BUS_PROTECTION_FOR_QC3, &val);

	return ret;
}

static int cp_enable_sw(bool enable)
{
	int ret;
	struct power_supply *psy;

	psy = cp_get_sw_psy();
	if (!psy)
		return -ENODEV;

	if (enable)
		charger_dev_set_input_current(ch_dev, 3000000);
	else
		charger_dev_set_input_current(ch_dev, 100000);

	cp_set_bq_charge_done(enable);
	pm_state.sw_chager.charge_enabled = enable;

	return ret;
}

static int cp_check_fc_enabled(void)
{
	int ret;
	struct power_supply *psy;
	union power_supply_propval val = {0,};

	psy = cp_get_fc_psy();
	if (!psy)
		return -ENODEV;

	if (!ch_dev)
		ch_dev = get_charger_by_name("primary_chg");

	if (!chg_consumer)
		chg_consumer = charger_manager_get_by_name(p_chip->dev, "charger_port1");

	ret = power_supply_get_property(psy,
			POWER_SUPPLY_PROP_CHARGING_ENABLED, &val);
	if (!ret) {
		pm_state.bq2597x.charge_enabled = !!val.intval;
		// bq is disabled, then open 6360
		if ((pm_state.bq2597x.charge_enabled == 0) && (ch_dev) && (chg_consumer)) {
			charger_dev_enable(ch_dev, !pm_state.bq2597x.charge_enabled);
			charger_manager_enable_power_path(chg_consumer, MAIN_CHARGER, !pm_state.bq2597x.charge_enabled);
			charger_dev_set_input_current(ch_dev, 3000000);
		}
	}

	return ret;
}

static int cp_check_sw_enabled(void)
{
	int ret;
	struct power_supply *psy;

	psy = cp_get_sw_psy();
	if (!psy)
		return -ENODEV;

	return ret;
}

static int cp_tune_vbus_volt(bool up, int pulse)
{
	int ret;
	struct power_supply *psy;
	union power_supply_propval val = {0,};

	psy = cp_get_charger_identify_psy();
	if (!psy)
		return -ENODEV;
	if (up)
		val.intval = (1 << 15 | pulse);
	else
		val.intval = (0 << 15 | pulse);
#ifdef CONFIG_XMUSB350_DET_CHG
	ret = power_supply_set_property(psy,
			POWER_SUPPLY_PROP_QC35_HVDCP_DPDM, &val);
#endif

	pr_debug("tune adapter voltage %s %s\n", up ? "up" : "down",
			ret ? "fail" : "successfully");

	return ret;

}

static int cp_reset_vbus_volt(void)
{
	int ret;
	int voltage = 0;
	struct power_supply *psy;
	struct power_supply *u_psy;
	union power_supply_propval val = {0,};

	u_psy = cp_get_usb_psy();
	if (!u_psy)
		return -ENODEV;

	if (!ch_dev)
		ch_dev = get_charger_by_name("primary_chg");

	if (ch_dev)
		charger_dev_set_mivr(ch_dev, 4600000);

	ret = power_supply_get_property(u_psy,
			POWER_SUPPLY_PROP_VOLTAGE_NOW, &val);
	if (!ret)
		voltage = val.intval;
	pr_debug("%s: vbus is %d!\n", __func__, voltage);

	psy = cp_get_charger_identify_psy();
	if (!psy)
		return -ENODEV;
	return ret;
}

static int cp_get_usb_type(void)
{
	int ret;
	struct power_supply *psy;
	union power_supply_propval val = {0,};

	psy = cp_get_usb_psy();
	if (!psy)
		return -ENODEV;

	ret = power_supply_get_property(psy,
			POWER_SUPPLY_PROP_REAL_TYPE, &val);
	if (!ret)
		pm_state.usb_type = val.intval;
	return ret;
}

static int cp_get_usb_present(void)
{
	int ret;
	struct power_supply *psy;
	union power_supply_propval val = {0,};

	psy = cp_get_usb_psy();
	if (!psy)
		return -ENODEV;

	ret = power_supply_get_property(psy,
			POWER_SUPPLY_PROP_PRESENT, &val);
	if (!ret)
		pm_state.usb_present = val.intval;

	return ret;
}

static int cp_get_qc_hvdcp3_type(void)
{
	int ret;
	struct power_supply *psy;
	union power_supply_propval val = {0,};

	psy = cp_get_usb_psy();
	if (!psy)
		return -ENODEV;

	ret = power_supply_get_property(psy,
			POWER_SUPPLY_PROP_HVDCP3_TYPE, &val);
	if (!ret)
		pm_state.hvdcp3_type = val.intval;
	pr_info("hvdcp3 type %d, val.intval is %d\n", pm_state.hvdcp3_type, val.intval);
	return ret;
}

#define TAPER_TIMEOUT	10
#define IBUS_CHANGE_TIMEOUT  5
static int cp_flash2_charge(struct cp_qc30_data *chip)
{
	static int ibus_limit;
	int thermal_level = 0;
	int effective_fcc_val = cp_get_effective_fcc_val(pm_state);
	static int value;
	bool is_fastcharge_mode = false;

	qc3_get_batt_current_thermal_level(&thermal_level);

	is_fastcharge_mode = qc3_get_fastcharge_mode_enabled(pm_state);
	if (is_fastcharge_mode)
		sys_config.bat_volt_lp_lmt = chip->bat_volt_max;
	else
		sys_config.bat_volt_lp_lmt = chip->non_ffc_bat_volt_max;

	if (ibus_limit == 0)
		ibus_limit = pm_state.ibus_lmt_curr;

	ibus_limit = min(effective_fcc_val/2, pm_state.ibus_lmt_curr);

	pr_info("ibus_limit: %d\n", ibus_limit);

	pr_info("vbus=%d, ibus=%d, vbat=%d, ibat=%d, fcc_val=%d\n",
				pm_state.bq2597x.vbus_volt,
				pm_state.bq2597x.ibus_curr,
				pm_state.bq2597x.vbat_volt,
				pm_state.bq2597x.ibat_curr,
				effective_fcc_val);

	pm_state.is_temp_out_fc2_range = qc3_disable_cp_by_jeita_status();
	pr_info("is_temp_out_fc2_range:%d\n", pm_state.is_temp_out_fc2_range);

	pr_info("bq2597x.bus_ocp_alarm: %d\n", pm_state.bq2597x.bus_ocp_alarm);

	if (pm_state.usb_type == POWER_SUPPLY_TYPE_USB_HVDCP_3) {
		if (pm_state.bq2597x.vbus_volt <= sys_config.bus_volt_alarm_threshold
			&& pm_state.bq2597x.ibus_curr < ibus_limit - value
			&& !pm_state.bq2597x.bus_ocp_alarm
			&& !pm_state.bq2597x.bus_ovp_alarm
			&& pm_state.bq2597x.vbat_volt < sys_config.bat_volt_lp_lmt - 40
			&& pm_state.bq2597x.ibat_curr < sys_config.bat_curr_lp_lmt
			&& pm_state.bq2597x.ibat_curr < effective_fcc_val - 600) {

			cp_tune_vbus_volt(VOLT_UP, 1);
		}

		if (pm_state.bq2597x.bus_ocp_alarm
			|| pm_state.bq2597x.bus_ovp_alarm
			|| pm_state.bq2597x.vbat_reg
			|| pm_state.bq2597x.vbat_volt > sys_config.bat_volt_lp_lmt
			|| pm_state.bq2597x.ibat_curr > sys_config.bat_curr_lp_lmt + 500
			|| pm_state.bq2597x.ibat_curr > effective_fcc_val + 500
			|| pm_state.bq2597x.ibus_curr > ibus_limit + 400 - value) {

			cp_tune_vbus_volt(VOLT_DOWN, 1);
		}
	}
	pr_info("bus_volt_alarm_threshold:%d,ibus_limit:%d,bat_volt_lp_lmt:%d,bat_curr_lp_lmt:%d,effective_fcc_val:%d",
			sys_config.bus_volt_alarm_threshold,
			ibus_limit,
			sys_config.bat_volt_lp_lmt,
			sys_config.bat_curr_lp_lmt,
			effective_fcc_val);

	pr_info("bus_ocp_alarm:%d,bus_ovp_alarm:%d,vbat_reg:%d,bus_ocp_alarm:%d\n",
			pm_state.bq2597x.bus_ocp_alarm,
			pm_state.bq2597x.bus_ovp_alarm,
			pm_state.bq2597x.vbat_reg,
			pm_state.bq2597x.bus_ocp_alarm);
	if (pm_state.usb_type == POWER_SUPPLY_TYPE_USB_HVDCP_3P5) {
		if (pm_state.bq2597x.vbus_volt <= sys_config.bus_volt_alarm_threshold
			&& pm_state.bq2597x.ibus_curr < ibus_limit - value + 50
			&& !pm_state.bq2597x.bus_ocp_alarm
			&& !pm_state.bq2597x.bus_ovp_alarm
			&& pm_state.bq2597x.vbat_volt < sys_config.bat_volt_lp_lmt - 20
			&& pm_state.bq2597x.ibat_curr < sys_config.bat_curr_lp_lmt
			&& pm_state.bq2597x.ibat_curr < effective_fcc_val) {

			cp_tune_vbus_volt(VOLT_UP, 1);
		}

		if (pm_state.bq2597x.bus_ocp_alarm
			|| pm_state.bq2597x.bus_ovp_alarm
			|| pm_state.bq2597x.vbat_reg
			|| pm_state.bq2597x.vbat_volt > sys_config.bat_volt_lp_lmt - 10
			|| pm_state.bq2597x.ibat_curr > sys_config.bat_curr_lp_lmt + 200
			|| pm_state.bq2597x.ibat_curr > effective_fcc_val + 50
			|| pm_state.bq2597x.ibus_curr > ibus_limit - value + 100) {

			cp_tune_vbus_volt(VOLT_DOWN, 1);
		}
	}

	pr_info("value=%d,ibus_lmt_change_timer=%d\n", value, ibus_lmt_change_timer);
	if (pm_state.bq2597x.vbat_reg) {
		value += 100;
	} else if (pm_state.bq2597x.vbat_volt < sys_config.bat_volt_lp_lmt - 150) {
		value = 0;
		ibus_lmt_change_timer = 0;
	}
	cp_check_fc_enabled();

	/* battery overheat, stop charge */
	if (pm_state.bq2597x.bat_therm_fault)
		return -ADC_ERR;
	else if (pm_state.bq2597x.bus_ocp_fault
			|| pm_state.bq2597x.bat_ovp_fault
			|| pm_state.bq2597x.bus_ovp_fault)
		return -ADC_ERR;
	else if (!pm_state.bq2597x.charge_enabled)
		return -CP_ENABLE_FAIL;
	else if (thermal_level >= MAX_THERMAL_LEVEL
			|| pm_state.is_temp_out_fc2_range) {
		pr_info("thermal level too high or batt temp is out of fc2 range\n");
		return CP_ENABLE_FAIL;
	}

	pr_info("fc2_taper_timer=%d\n", fc2_taper_timer);
	if (pm_state.bq2597x.vbat_volt > sys_config.bat_volt_lp_lmt - 50 &&
			pm_state.bq2597x.ibat_curr < sys_config.fc2_taper_current) {
		if (fc2_taper_timer++ > TAPER_TIMEOUT) {
			fc2_taper_timer = 0;
			return 1;
		}
	} else {
		fc2_taper_timer = 0;
	}

	return 0;
}

const unsigned char *pm_state_str[] = {
	"CP_STATE_ENTRY",
	"CP_STATE_DISCONNECT",
	"CP_STATE_SW_ENTRY",
	"CP_STATE_SW_ENTRY_2",
//	"CP_STATE_SW_ENTRY_3",
	"CP_STATE_SW_LOOP",
	"CP_STATE_FLASH2_ENTRY",
	"CP_STATE_FLASH2_ENTRY_1",
//	"CP_STATE_FLASH2_ENTRY_2",
	"CP_STATE_FLASH2_ENTRY_3",
	"CP_STATE_FLASH2_TUNE",
	"CP_STATE_FLASH2_DELAY",
	"CP_STATE_STOP_CHARGE",
};

static void cp_move_state(pm_sm_state_t state)
{
	pr_info("pm_state change:%s -> %s\n",
		pm_state_str[pm_state.state], pm_state_str[state]);
	pm_state.state_log[pm_state.log_idx] = pm_state.state;
	pm_state.log_idx++;
	pm_state.log_idx %= PM_STATE_LOG_MAX;
	pm_state.state = state;
}

void cp_statemachine(struct cp_qc30_data *chip)
{
	int ret;
	static int tune_vbus_retry ,tune_vbus_count;
	int thermal_level = 0;
	static bool recovery;
	pr_debug("cp_statemachine\n");

	if (!pm_state.bq2597x.vbus_pres) {
		pm_state.state = CP_STATE_DISCONNECT;
		recovery = false;
		pr_info("vbus disconnected\n");
	} else  if  (pm_state.state == CP_STATE_DISCONNECT) {
		pr_info("vbus connected\n");
		recovery = false;
		pm_state.jeita_triggered = false;
		pm_state.is_temp_out_fc2_range = false;
		pm_state.sw_near_cv = false;
		cp_move_state(CP_STATE_ENTRY);
	}

	switch (pm_state.state) {
	case CP_STATE_DISCONNECT:
		if (pm_state.bq2597x.charge_enabled) {
			cp_enable_fc(false);
			cp_check_fc_enabled();
		}

		if (!pm_state.sw_chager.charge_enabled) {
			cp_reset_vbus_volt();
			cp_enable_sw(true);
			cp_check_sw_enabled();
		}

		if (pm_state.bms_fastcharge_mode)
			qc3_set_bms_fastcharge_mode(false);
		tune_vbus_count = 0;
		pm_state.usb_type = 0;
		pm_state.sw_from_flash2 = false;
		pm_state.sw_fc2_init_fail = false;
		pm_state.sw_near_cv = false;
		sys_config.bat_curr_lp_lmt = HVDCP3_CLASS_A_BAT_CURRENT_MA;
		sys_config.bus_curr_lp_lmt = HVDCP3_CLASS_A_BUS_CURRENT_MA;
		pm_state.ibus_lmt_curr = HVDCP3_CLASS_A_BUS_CURRENT_MA;
		cp_set_qc_bus_protections(HVDCP3_NONE);
		break;

	case CP_STATE_ENTRY:
		cp_get_usb_type();
		qc3_get_batt_current_thermal_level(&thermal_level);
		pm_state.is_temp_out_fc2_range = qc3_disable_cp_by_jeita_status();
		pr_info("is_temp_out_fc2_range:%d\n", pm_state.is_temp_out_fc2_range);

		if ((pm_state.usb_type == POWER_SUPPLY_TYPE_USB_HVDCP_3) ||
			(pm_state.usb_type == POWER_SUPPLY_TYPE_USB_HVDCP_3P5)) {
			pr_err("vbus_volt:%d\n", pm_state.bq2597x.vbus_volt);
			cp_reset_vbus_volt();
			msleep(100);
			if (thermal_level >= MAX_THERMAL_LEVEL
					|| pm_state.is_temp_out_fc2_range) {
				cp_set_bq_charge_done(true);
				pr_info("thermal too high or batt temp out of range or slowly charging, waiting...\n");
			} else if (pm_state.bq2597x.vbat_volt < sys_config.min_vbat_start_flash2) {
				cp_set_bq_charge_done(true);
				cp_move_state(CP_STATE_SW_ENTRY);
			} else if (pm_state.bq2597x.vbat_volt > sys_config.bat_volt_lp_lmt - 200) {
				cp_set_bq_charge_done(true);
				pm_state.sw_near_cv = true;
				cp_move_state(CP_STATE_SW_ENTRY);
			} else if (!cp_get_hv_charge_enable()) {
				cp_set_bq_charge_done(true);
				pr_info("hv charge disbale, waiting...\n");
				cp_move_state(CP_STATE_SW_ENTRY);
			} else {
				cp_set_bq_charge_done(false);
				cp_move_state(CP_STATE_FLASH2_ENTRY);
			}
		}
		break;

	case CP_STATE_SW_ENTRY:
		cp_reset_vbus_volt();
		if (pm_state.bq2597x.charge_enabled) {
			cp_enable_fc(false);
			cp_check_fc_enabled();
		}

		if (!pm_state.bq2597x.charge_enabled)
			cp_move_state(CP_STATE_SW_ENTRY_2);
		break;

	case CP_STATE_SW_ENTRY_2:
		pr_err("enable sw charger and check enable\n");
		cp_enable_sw(true);
		cp_check_sw_enabled();
		if (pm_state.sw_chager.charge_enabled)
			cp_move_state(CP_STATE_SW_LOOP);
		break;

	case CP_STATE_SW_LOOP:
		if (tune_vbus_count >= 2) {
			pr_info("unsupport qc3 or qc35, use sw charging\n");
			break;
		}
		qc3_get_batt_current_thermal_level(&thermal_level);
		pm_state.is_temp_out_fc2_range = qc3_disable_cp_by_jeita_status();

		if (thermal_level < MAX_THERMAL_LEVEL && !pm_state.is_temp_out_fc2_range && cp_get_hv_charge_enable() && recovery) {
			pr_info("thermal / batt temp / hv charge enable recovery...\n");
			recovery = false;
		} else
			pr_info("thermal(%d) too high or batt temp out of range\n", thermal_level);

		if (pm_state.bq2597x.vbat_volt > sys_config.bat_volt_lp_lmt - 100) {
			pm_state.sw_near_cv = true;
			pr_info("sw_near_cv trigger, bq2597x.vbat_volt = %d\n", pm_state.bq2597x.vbat_volt);
		} else
			pm_state.sw_near_cv = false;

		if (!pm_state.sw_near_cv && !recovery) {
			if (pm_state.bq2597x.vbat_volt > sys_config.min_vbat_start_flash2) {
				pr_info("battery volt: %d is ok, proceeding to flash charging...\n",
						pm_state.bq2597x.vbat_volt);
				cp_move_state(CP_STATE_FLASH2_ENTRY);
			}
		}
		break;

	case CP_STATE_FLASH2_ENTRY:
		if (pm_state.sw_chager.charge_enabled) {
			cp_enable_sw(false);
			cp_check_sw_enabled();
		}

		if (!pm_state.sw_chager.charge_enabled) {
			cp_move_state(CP_STATE_FLASH2_ENTRY_1);
			tune_vbus_retry = 0;
		}

		qc3_get_batt_current_thermal_level(&thermal_level);
		charger_manager_set_prop_system_temp_level(thermal_level);
		cp_get_qc_hvdcp3_type();
		if (pm_state.hvdcp3_type == HVDCP3_CLASSB_27W) {
			sys_config.bus_volt_alarm_threshold = BUS_OVP_ALARM_THRESHOLD_QC30;
			sys_config.bat_curr_lp_lmt = chip->qc3_27w_bat_curr_max;
			sys_config.bus_curr_lp_lmt = chip->qc3_27w_bus_curr_max;
			pm_state.ibus_lmt_curr = sys_config.bus_curr_lp_lmt;
			cp_set_qc_bus_protections(HVDCP3_CLASSB_27W);
		} else if (pm_state.hvdcp3_type == HVDCP3_CLASSA_18W) {
			sys_config.bus_volt_alarm_threshold = BUS_OVP_ALARM_THRESHOLD_QC30;
			sys_config.bat_curr_lp_lmt = HVDCP3_CLASS_A_BAT_CURRENT_MA;
			sys_config.bus_curr_lp_lmt = HVDCP3_CLASS_A_BUS_CURRENT_MA;
			pm_state.ibus_lmt_curr = sys_config.bus_curr_lp_lmt;
			cp_set_qc_bus_protections(HVDCP3_CLASSA_18W);
		} else if (pm_state.hvdcp3_type == HVDCP3_P_CLASSA_18W) {
			sys_config.bus_volt_alarm_threshold = BUS_OVP_ALARM_THRESHOLD_QC35;
			sys_config.bat_curr_lp_lmt = chip->qc3p_bat_curr_max;
			sys_config.bus_curr_lp_lmt = chip->qc3p_bus_curr_max;
			pm_state.ibus_lmt_curr = sys_config.bus_curr_lp_lmt;
			cp_set_qc_bus_protections(HVDCP3_P_CLASSA_18W);
		} else if (pm_state.hvdcp3_type == HVDCP3_P_CLASSB_27W) {
			sys_config.bus_volt_alarm_threshold = BUS_OVP_ALARM_THRESHOLD_QC35;
			sys_config.bat_curr_lp_lmt = HVDCP3_P_CLASSB_BAT_CURRENT_MA;
			sys_config.bus_curr_lp_lmt = HVDCP3_P_CLASSB_BUS_CURRENT_MA;
			pm_state.ibus_lmt_curr = sys_config.bus_curr_lp_lmt;
			cp_set_qc_bus_protections(HVDCP3_P_CLASSB_27W);
		} else {
			cp_set_qc_bus_protections(HVDCP3_NONE);
		}
		break;

	case CP_STATE_FLASH2_ENTRY_1:
		cp_update_fc_status();
		if (pm_state.bq2597x.vbus_volt < (pm_state.bq2597x.vbat_volt * 2 + BUS_VOLT_INIT_UP - 50)) {
			if (!ch_dev)
				ch_dev = get_charger_by_name("primary_chg");

			if (ch_dev)
				charger_dev_set_input_current(ch_dev, 100000);
			tune_vbus_retry++;
			if (pm_state.usb_type == POWER_SUPPLY_TYPE_USB_HVDCP_3)
				cp_tune_vbus_volt(VOLT_UP, 1);
			if (pm_state.usb_type == POWER_SUPPLY_TYPE_USB_HVDCP_3P5) {
				if ((pm_state.bq2597x.vbus_volt < (pm_state.bq2597x.vbat_volt * 2)) &&
						(pm_state.bq2597x.vbus_volt < 8300))
					cp_tune_vbus_volt(VOLT_UP, 10);
				else
					cp_tune_vbus_volt(VOLT_UP, 1);
			}
		} else {
			pr_info("QC30: vbus=%d, vbat=%d, tune_times=%d\n",
					pm_state.bq2597x.vbus_volt, pm_state.bq2597x.vbat_volt, tune_vbus_retry);
			tune_vbus_retry = 0;
			cp_move_state(CP_STATE_FLASH2_ENTRY_3);
			break;
		}

		if ((tune_vbus_retry > 23) &&
			(pm_state.usb_type == POWER_SUPPLY_TYPE_USB_HVDCP_3)) {
			pr_err("Failed to tune adapter volt into valid range, charge with switching charger\n");
			pm_state.sw_fc2_init_fail = true;
			tune_vbus_count++;
			cp_move_state(CP_STATE_SW_ENTRY);
		}
		if ((tune_vbus_retry > 210) &&
			(pm_state.usb_type == POWER_SUPPLY_TYPE_USB_HVDCP_3P5)) {
			pr_err("QC35: Failed to tune adapter volt into valid range, charge with switching charger\n");
			pm_state.sw_fc2_init_fail = true;
			tune_vbus_count++;
			cp_move_state(CP_STATE_SW_ENTRY);
		}
		break;

	case CP_STATE_FLASH2_ENTRY_3:
		pr_info("CP_STATE_FLASH2_ENTRY_3: bus_err_st:%d, cur_vol:%d, retry:%d\n",
				pm_state.bq2597x.bus_error_status, pm_state.bq2597x.vbus_volt, tune_vbus_retry);

		if (pm_state.bq2597x.bus_error_status == CP_VBUS_ERROR_HIGHT) {
			pr_err("vbus is too high to open cp switcher, decrease it.\n");
			tune_vbus_retry++;
			if(pm_state.usb_type == POWER_SUPPLY_TYPE_USB_HVDCP_3)
				cp_tune_vbus_volt(VOLT_DOWN, 1);
			else if(pm_state.usb_type == POWER_SUPPLY_TYPE_USB_HVDCP_3P5)
				cp_tune_vbus_volt(VOLT_DOWN, 4);

			if ((tune_vbus_retry > 5 && pm_state.usb_type == POWER_SUPPLY_TYPE_USB_HVDCP_3)
					|| (tune_vbus_retry > 10 && pm_state.usb_type == POWER_SUPPLY_TYPE_USB_HVDCP_3P5)) {
				pr_err("Failed to decrease vbus into valid range, switch to main charger\n");
				pm_state.sw_fc2_init_fail = true;
				cp_set_bq_charge_done(true);
				cp_move_state(CP_STATE_STOP_CHARGE);
			}
		} else if (pm_state.bq2597x.bus_error_status == CP_VBUS_ERROR_LOW) {
			pr_err("vbus is too low to open cp switcher, increase it.\n");
			tune_vbus_retry++;
			if(pm_state.usb_type == POWER_SUPPLY_TYPE_USB_HVDCP_3)
				cp_tune_vbus_volt(VOLT_UP, 1);
			else if(pm_state.usb_type == POWER_SUPPLY_TYPE_USB_HVDCP_3P5)
				cp_tune_vbus_volt(VOLT_UP, 4);

			if ((tune_vbus_retry > 5 && pm_state.usb_type == POWER_SUPPLY_TYPE_USB_HVDCP_3)
					|| (tune_vbus_retry > 10 && pm_state.usb_type == POWER_SUPPLY_TYPE_USB_HVDCP_3P5)) {
				pr_err("Failed to increase vbus into valid range, switch to main charger\n");
				pm_state.sw_fc2_init_fail = true;
				cp_set_bq_charge_done(true);
				cp_move_state(CP_STATE_STOP_CHARGE);
			}
		} else {
			pr_info("vbus is ok, enable flash charging\n");
			tune_vbus_retry = 0;
			if (!pm_state.bq2597x.charge_enabled) {
				cp_enable_fc(true);
				cp_check_fc_enabled();
				if (pm_state.bq2597x.charge_enabled)
					cp_move_state(CP_STATE_FLASH2_TUNE);
			}
			ibus_lmt_change_timer = 0;
			fc2_taper_timer = 0;
		}
		break;

	case CP_STATE_FLASH2_TUNE:
		if (pm_state.hvdcp3_type == HVDCP3_CLASSB_27W) {
			if (!pm_state.bms_fastcharge_mode)
				qc3_set_bms_fastcharge_mode(true);
		}
		ret = cp_flash2_charge(chip);
		if (ret == -ADC_ERR) {
			pr_err("Move to stop charging:%d\n", ret);
			cp_set_bq_charge_done(true);
			cp_move_state(CP_STATE_STOP_CHARGE);
			break;
		} else if (ret == -CP_ENABLE_FAIL || ret == 1) {
			pr_err("Move to switch charging:%d\n", ret);
			cp_set_bq_charge_done(true);
			cp_move_state(CP_STATE_SW_ENTRY);
			pm_state.sw_from_flash2 = true;
			break;
		} else if (ret == CP_ENABLE_FAIL || !cp_get_hv_charge_enable()) {
			pr_err("Move to switch charging, will try to recover to flash charging:%d\n",
					ret);
			recovery = true;
			pm_state.sw_from_flash2 = true;
			cp_set_bq_charge_done(true);
			cp_move_state(CP_STATE_SW_ENTRY);
		} else {// normal tune adapter output
			cp_set_bq_charge_done(false);
			cp_move_state(CP_STATE_FLASH2_DELAY);
		}
		break;

	case CP_STATE_FLASH2_DELAY:
		cp_move_state(CP_STATE_FLASH2_TUNE);
		break;

	case CP_STATE_STOP_CHARGE:
		pr_err("Stop charging\n");
		if (pm_state.bq2597x.charge_enabled) {
			cp_enable_fc(false);
			cp_check_fc_enabled();
		}
		if (pm_state.sw_chager.charge_enabled) {
			cp_enable_sw(false);
			cp_check_sw_enabled();
		}
		if (pm_state.bms_fastcharge_mode)
			qc3_set_bms_fastcharge_mode(false);
		break;

	default:
		pr_err("No state defined! Move to stop charging\n");
		if (pm_state.bms_fastcharge_mode)
			qc3_set_bms_fastcharge_mode(false);
		cp_move_state(CP_STATE_STOP_CHARGE);
		break;
	}
}

static void cp_workfunc(struct work_struct *work)
{
	struct cp_qc30_data *chip = container_of(work, struct cp_qc30_data,
			qc3_pm_work.work);

	cp_get_usb_type();

	cp_update_sw_status();
	cp_update_fc_status();

	if ((pm_state.hvdcp3_type == HVDCP3_P_CLASSA_18W) ||
	(pm_state.hvdcp3_type == HVDCP3_P_CLASSB_27W) ||
	(pm_state.hvdcp3_type == HVDCP3_CLASSB_27W) ||
	(pm_state.hvdcp3_type == HVDCP3_CLASSA_18W))
		cp_statemachine(chip);
	else
		cp_get_qc_hvdcp3_type();

	cp_get_usb_present();
	pr_debug("pm_state.usb_present: %d\n", pm_state.usb_present);
	/* check whether usb is present */
	if (pm_state.usb_present == 0) {
		cp_set_qc_bus_protections(HVDCP3_NONE);
		if (pm_state.bms_fastcharge_mode)
			qc3_set_bms_fastcharge_mode(false);
		return;
	}

	if (pm_state.usb_type == POWER_SUPPLY_TYPE_USB_HVDCP_3)
		schedule_delayed_work(&chip->qc3_pm_work,
			msecs_to_jiffies(PM_WORK_TIME_500MS));
	else if (pm_state.usb_type == POWER_SUPPLY_TYPE_USB_HVDCP_3P5)
		schedule_delayed_work(&chip->qc3_pm_work, msecs_to_jiffies(100));
}

static int cp_qc30_notifier_call(struct notifier_block *nb,
		unsigned long ev, void *v)
{
	struct cp_qc30_data *chip = container_of(nb, struct cp_qc30_data, nb);
	struct power_supply *psy = v;
	static bool usb_hvdcp3_on;

	if (ev != PSY_EVENT_PROP_CHANGED)
		return NOTIFY_OK;

	if (strcmp(psy->desc->name, "usb") == 0) {
		cp_get_usb_type();
		if (pm_state.usb_type == POWER_SUPPLY_TYPE_USB_HVDCP_3) {
			schedule_delayed_work(&chip->qc3_pm_work, 3*HZ);
			usb_hvdcp3_on = true;
		} else if (pm_state.usb_type == POWER_SUPPLY_TYPE_USB_HVDCP_3P5) {
			schedule_delayed_work(&chip->qc3_pm_work, msecs_to_jiffies(100));
			usb_hvdcp3_on = true;
		} else if (pm_state.usb_type == POWER_SUPPLY_TYPE_UNKNOWN && usb_hvdcp3_on == true) {
			cancel_delayed_work(&chip->qc3_pm_work);
			schedule_delayed_work(&chip->qc3_pm_work, 0);
			pr_info("pm_state.usb_type: %d\n", pm_state.usb_type);
			usb_hvdcp3_on = false;
		}
	}

	return NOTIFY_OK;
}

static int cp_qc30_register_notifier(struct cp_qc30_data *chip)
{
	int rc;

	chip->nb.notifier_call = cp_qc30_notifier_call;
	rc = power_supply_reg_notifier(&chip->nb);
	if (rc < 0) {
		pr_err("Couldn't register psy notifier rc = %d\n", rc);
		return rc;
	}

	return 0;
}

static int cp_qc30_parse_dt(struct cp_qc30_data *chip)
{
	struct device_node *node = chip->dev->of_node;
	int rc = 0;

	if (!node) {
		pr_err("device tree node missing\n");
		return -EINVAL;
	}

	rc = of_property_read_u32(node,
			"mi,qc3-bat-volt-max", &chip->bat_volt_max);
	if (rc < 0)
		pr_err("qc3-bat-volt-max property missing, use default val\n");
	else
		sys_config.bat_volt_lp_lmt = chip->bat_volt_max;

	rc = of_property_read_u32(node,
			"mi,qc3-non-ffc-bat-volt-max", &chip->non_ffc_bat_volt_max);
	if (rc < 0)
		pr_err("qc3-non-ffc-bat-volt-max property missing, use default val\n");
	else
		pr_info("non_ffc_bat_volt_max:%d\n",
				chip->non_ffc_bat_volt_max);

	rc = of_property_read_u32(node,
			"mi,qc3-bat-curr-max", &chip->bat_curr_max);
	if (rc < 0)
		pr_err("qc3-bat-curr-max property missing, use default val\n");
	else
		sys_config.bat_curr_lp_lmt = chip->bat_curr_max;

	rc = of_property_read_u32(node,
			"mi,qc3-bus-volt-max", &chip->bus_volt_max);
	if (rc < 0)
		pr_err("qc3-bus-volt-max property missing, use default val\n");
	else
		sys_config.bus_volt_lp_lmt = chip->bus_volt_max;

	rc = of_property_read_u32(node,
			"mi,qc3-bus-curr-max", &chip->bus_curr_max);
	if (rc < 0)
		pr_err("qc3-bus-curr-max property missing, use default val\n");
	else
		sys_config.bus_curr_lp_lmt = chip->bus_curr_max;

	rc = of_property_read_u32(node,
			"mi,qc3-27w-bat-curr-max", &chip->qc3_27w_bat_curr_max);
	if (rc < 0) {
		chip->qc3_27w_bat_curr_max = HVDCP3_CLASS_B_BAT_CURRENT_MA;
		pr_err("qc3-27w-bat-curr-max property missing, use default val:%d\n",
				chip->qc3_27w_bat_curr_max);
	}

	rc = of_property_read_u32(node,
			"mi,qc3-27w-bus-curr-max", &chip->qc3_27w_bus_curr_max);
	if (rc < 0) {
		chip->qc3_27w_bus_curr_max = HVDCP3_CLASS_B_BUS_CURRENT_MA;
		pr_err("qc3-27w-bus-curr-max property missing, use default val:%d\n",
				chip->qc3_27w_bus_curr_max);
	}

	rc = of_property_read_u32(node,
			"mi,qc3p-bat-curr-max", &chip->qc3p_bat_curr_max);
	if (rc < 0) {
		chip->qc3p_bat_curr_max = HVDCP3_P_CLASSA_BAT_CURRENT_MA;
		pr_err("qc3p-bat-curr-max property missing, use default val:%d\n",
				chip->qc3p_bat_curr_max);
	}

	rc = of_property_read_u32(node,
			"mi,qc3p-bus-curr-max", &chip->qc3p_bus_curr_max);
	if (rc < 0) {
		chip->qc3p_bus_curr_max = HVDCP3_P_CLASSA_BUS_CURRENT_MA;
		pr_err("qc3p-bus-curr-max property missing, use default val:%d\n",
				chip->qc3p_bus_curr_max);
	}

	chip->cp_sec_enable = of_property_read_bool(node,
				"mi,cp-sec-enable");

	sys_config.cp_sec_enable = chip->cp_sec_enable;

	return 0;
}

static int cp_qc30_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct device *dev = &pdev->dev;
	struct cp_qc30_data *chip;

	pr_debug("%s enter\n", __func__);

	chip = devm_kzalloc(dev, sizeof(struct cp_qc30_data), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = dev;
	p_chip = chip;
	ret = cp_qc30_parse_dt(chip);
	if (ret < 0) {
		pr_err("Couldn't parse device tree rc=%d\n", ret);
		return ret;
	}

	ch_dev = get_charger_by_name("primary_chg");
	chg_consumer = charger_manager_get_by_name(&pdev->dev, "charger_port1");
	if (!chg_consumer) {
		pr_err("%s: get charger consumer device failed\n", __func__);
		return -ENODEV;
	}

	platform_set_drvdata(pdev, chip);

	pm_state.state = CP_STATE_DISCONNECT;
	pm_state.usb_type = POWER_SUPPLY_TYPE_UNKNOWN;
	pm_state.ibus_lmt_curr = sys_config.bus_curr_lp_lmt;

	INIT_DELAYED_WORK(&chip->qc3_pm_work, cp_workfunc);

	cp_qc30_register_notifier(chip);

	pr_info("charge pump qc3 probe\n");

	return ret;
}

static int cp_qc30_remove(struct platform_device *pdev)
{
	struct cp_qc30_data *chip = platform_get_drvdata(pdev);

	cancel_delayed_work(&chip->qc3_pm_work);
	return 0;
}

static const struct of_device_id cp_qc30_of_match[] = {
	{ .compatible = "xiaomi,cp-qc30", },
	{},
};

static struct platform_driver cp_qc30_driver = {
	.driver = {
		.name = "cp-qc30",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(cp_qc30_of_match),
	},
	.probe = cp_qc30_probe,
	.remove = cp_qc30_remove,
};

static int __init cp_qc30_init(void)
{
	return platform_driver_register(&cp_qc30_driver);
}

late_initcall(cp_qc30_init);

static void __exit cp_qc30_exit(void)
{
	return platform_driver_unregister(&cp_qc30_driver);
}
module_exit(cp_qc30_exit);

MODULE_AUTHOR("Fei Jiang<jiangfei1@xiaomi.com>");
MODULE_DESCRIPTION("Xiaomi cp qc30");
MODULE_LICENSE("GPL");

