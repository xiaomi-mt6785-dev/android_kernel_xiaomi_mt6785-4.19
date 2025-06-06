/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 MediaTek Inc.
*/
#ifndef __MTK_CHARGER_INTF_H__
#define __MTK_CHARGER_INTF_H__

#include <linux/device.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/alarmtimer.h>
#include <mt-plat/v1/charger_type.h>
#include <mt-plat/v1/mtk_charger.h>
#include <mt-plat/v1/mtk_battery.h>

#include <linux/kthread.h>
#include <linux/power_supply.h>

#include <mtk_gauge_time_service.h>

#include <mt-plat/v1/charger_class.h>

struct charger_manager;
#include "mtk_pe_intf.h"
#include "mtk_pe20_intf.h"
#include "mtk_pe40_intf.h"
#include "mtk_pe50_intf.h"
#include "mtk_pdc_intf.h"
#include "adapter_class.h"

#define CHARGING_INTERVAL 10
#define CHARGING_FULL_INTERVAL 20

#define CHRLOG_ERROR_LEVEL   1
#define CHRLOG_DEBUG_LEVEL   2

extern int chr_get_debug_level(void);

#define chr_err(fmt, args...)					\
do {								\
	if (chr_get_debug_level() >= CHRLOG_ERROR_LEVEL) {	\
		pr_notice(fmt, ##args);				\
	}							\
} while (0)

#define chr_info(fmt, args...)					\
do {								\
	if (chr_get_debug_level() >= CHRLOG_ERROR_LEVEL) {	\
		pr_notice_ratelimited(fmt, ##args);		\
	}							\
} while (0)

#define chr_debug(fmt, args...)					\
do {								\
	if (chr_get_debug_level() >= CHRLOG_DEBUG_LEVEL) {	\
		pr_notice(fmt, ##args);				\
	}							\
} while (0)

#define CHR_CC		(0x0001)
#define CHR_TOPOFF	(0x0002)
#define CHR_TUNING	(0x0003)
#define CHR_POSTCC	(0x0004)
#define CHR_BATFULL	(0x0005)
#define CHR_ERROR	(0x0006)
#define	CHR_PE40_INIT	(0x0007)
#define	CHR_PE40_CC	(0x0008)
#define	CHR_PE40_TUNING	(0x0009)
#define	CHR_PE40_POSTCC	(0x000A)
#define CHR_PE30	(0x000B)
#define CHR_PE40	(0x000C)
#define CHR_PDC		(0x000D)
#define CHR_XM_PD_PM		(0x000E)
#define CHR_XM_QC3		(0x000F)
#define CHR_XM_QC20		(0x0010)
#define CHR_PE50_READY		(0x0011)
#define CHR_PE50_RUNNING	(0x0012)
#define CHR_PE50		(0x0013)

/* charging abnormal status */
#define CHG_VBUS_OV_STATUS	(1 << 0)
#define CHG_BAT_OT_STATUS	(1 << 1)
#define CHG_OC_STATUS		(1 << 2)
#define CHG_BAT_OV_STATUS	(1 << 3)
#define CHG_ST_TMO_STATUS	(1 << 4)
#define CHG_BAT_LT_STATUS	(1 << 5)
#define CHG_TYPEC_WD_STATUS	(1 << 6)

/* registers parameter */
// mode
#define XMUSB350_MODE_QC20_V5			0x01
#define XMUSB350_MODE_QC20_V9			0x02
#define XMUSB350_MODE_QC20_V12			0x03
#define XMUSB350_MODE_QC30_V5			0x04
#define XMUSB350_MODE_QC3_PLUS_V5		0x05

/* HVDCP type */
enum hvdcp_status{
	HVDCP_NULL,
	HVDCP,
	HVDCP_3,
};


/*wireless charger type*/
enum wireless_chg_state {
	WIRELESS_NULL = 0,
	WIRELESS_SDP,
	WIRELESS_CDP,
	WIRELESS_CHG_CDP,
	WIRELESS_CHG_DCP,
	WIRELESS_CHG_HVDCP,
};

/* QC35 charger type */
enum xmusb350_chg_type {
	QC35_NA = 0,
	QC35_OCP = 0x1,
	QC35_FLOAT = 0x2,
	QC35_SDP = 0x3,
	QC35_CDP = 0x4,
	QC35_DCP = 0x5,
	QC35_HVDCP_20 = 0x6,
	QC35_HVDCP_30 = 0x7,
	QC35_HVDCP_3_PLUS_18 = 0x8,
	QC35_HVDCP_3_PLUS_27 = 0x9,
	QC35_HVDCP_30_18 = 0xA,
	QC35_HVDCP_30_27 = 0xB,
	QC35_PD = 0xC,
	QC35_PD_DR = 0xD,
	QC35_HVDCP = 0x10,
	QC35_UNKNOW = 0x11,
};

enum hvdcp3_type {
	HVDCP3_NONE = 0,
	HVDCP3_CLASSA_18W,
	HVDCP3_CLASSB_27W,
	HVDCP3_P_CLASSA_18W,
	HVDCP3_P_CLASSB_27W,
};

/* charger_algorithm notify charger_dev */
enum {
	EVENT_EOC,
	EVENT_RECHARGE,
};

/* charger_dev notify charger_manager */
enum {
	CHARGER_DEV_NOTIFY_VBUS_OVP,
	CHARGER_DEV_NOTIFY_BAT_OVP,
	CHARGER_DEV_NOTIFY_EOC,
	CHARGER_DEV_NOTIFY_RECHG,
	CHARGER_DEV_NOTIFY_SAFETY_TIMEOUT,
	CHARGER_DEV_NOTIFY_VBATOVP_ALARM,
	CHARGER_DEV_NOTIFY_VBUSOVP_ALARM,
	CHARGER_DEV_NOTIFY_IBATOCP,
	CHARGER_DEV_NOTIFY_IBUSOCP,
	CHARGER_DEV_NOTIFY_IBUSUCP_FALL,
	CHARGER_DEV_NOTIFY_VOUTOVP,
	CHARGER_DEV_NOTIFY_VDROVP,
};

/*
 * Software JEITA
 * T0: -10 degree Celsius
 * T1: 0 degree Celsius
 * T2: 10 degree Celsius
 * T3: 45 degree Celsius
 * T4: 50 degree Celsius
 */
enum sw_jeita_state_enum {
	TEMP_BELOW_T0 = 0,
	TEMP_TN1_TO_T0,
	TEMP_T0_TO_T1,
	TEMP_T1_TO_T1P5,
	TEMP_T1P5_TO_T2,
	TEMP_T2_TO_T3,
	TEMP_T3_TO_T4,
	TEMP_ABOVE_T4
};

struct sw_jeita_data {
	int sm;
	int pre_sm;
	int cv;
	int pre_cv;
	bool charging;
	bool error_recovery_flag;
};

/* battery thermal protection */
enum bat_temp_state_enum {
	BAT_TEMP_LOW = 0,
	BAT_TEMP_NORMAL,
	BAT_TEMP_HIGH
};

enum {
	NORMAL,
	STEP_A_TR,
	STEP_B_TR,
};

struct battery_thermal_protection_data {
	int sm;
	bool enable_min_charge_temp;
	int min_charge_temp;
	int min_charge_temp_plus_x_degree;
	int max_charge_temp;
	int max_charge_temp_minus_x_degree;
};

struct charger_custom_data {
	int battery_cv;	/* uv */
	int max_charger_voltage;
	int max_charger_voltage_setting;
	int min_charger_voltage;

	int usb_charger_current_suspend;
	int usb_charger_current_unconfigured;
	int usb_charger_current_configured;
	int usb_charger_current;
	int ac_charger_current;
	int ac_charger_input_current;
	int qc_charger_input_current;
	int check_hv_current;
	int non_std_ac_charger_current;
	int charging_host_charger_current;
	int apple_1_0a_charger_current;
	int apple_2_1a_charger_current;
	int ta_ac_charger_current;
	int pd_charger_current;

	/* dynamic mivr */
	int min_charger_voltage_1;
	int min_charger_voltage_2;
	int max_dmivr_charger_current;

	/* sw jeita */
	int jeita_temp_above_t4_cv;
	int jeita_temp_t3_to_t4_cv;
	int jeita_temp_t2_to_t3_cv;
	int jeita_temp_t1p5_to_t2_cv;
	int jeita_temp_t1_to_t1p5_cv;
	int jeita_temp_t0_to_t1_cv;
	int jeita_temp_tn1_to_t0_cv;
	int jeita_temp_below_t0_cv;
	int temp_t4_thres;
	int temp_t4_thres_minus_x_degree;
	int temp_t3_thres;
	int temp_t3_thres_minus_x_degree;
	int temp_t2_thres;
	int temp_t2_thres_plus_x_degree;
	int temp_t1p5_thres;
	int temp_t1p5_thres_plus_x_degree;
	int temp_t1_thres;
	int temp_t1_thres_plus_x_degree;
	int temp_t0_thres;
	int temp_t0_thres_plus_x_degree;
	int temp_tn1_thres;
	int temp_tn1_thres_plus_x_degree;
	int temp_neg_10_thres;
	int temp_t3_to_t4_fcc;
	int temp_t2_to_t3_fcc;
	int temp_t1p5_to_t2_fcc;
	int temp_t1_to_t1p5_fcc;
	int temp_t0_to_t1_fcc;
	int temp_tn1_to_t0_fcc;

	/* battery temperature protection */
	int mtk_temperature_recharge_support;
	int max_charge_temp;
	int max_charge_temp_minus_x_degree;
	int min_charge_temp;
	int min_charge_temp_plus_x_degree;

	/* pe */
	int pe_ichg_level_threshold;	/* ma */
	int ta_ac_12v_input_current;
	int ta_ac_9v_input_current;
	int ta_ac_7v_input_current;
	bool ta_12v_support;
	bool ta_9v_support;

	/* pe2.0 */
	int pe20_ichg_level_threshold;	/* ma */
	int ta_start_battery_soc;
	int ta_stop_battery_soc;

	/* pe4.0 */
	int pe40_single_charger_input_current;	/* ma */
	int pe40_single_charger_current;
	int pe40_dual_charger_input_current;
	int pe40_dual_charger_chg1_current;
	int pe40_dual_charger_chg2_current;
	int pe40_stop_battery_soc;
	int pe40_max_vbus;
	int pe40_max_ibus;
	int high_temp_to_leave_pe40;
	int high_temp_to_enter_pe40;
	int low_temp_to_leave_pe40;
	int low_temp_to_enter_pe40;

#ifdef CONFIG_BQ2597X_CHARGE_PUMP
	/* xiaomi pps */
	int xm_pps_single_charger_input_current;	/* ma */
	int xm_pps_single_charger_current;
	int xm_pps_dual_charger_input_current;
	int xm_pps_dual_charger_chg1_current;
	int xm_pps_dual_charger_chg2_current;
	int xm_pps_max_vbus;
	int xm_pps_max_ibus;
	int xm_pps_single_charger_current_non_verified_pps;	/* ma */
#endif

	/* pe4.0 cable impedance threshold (mohm) */
	u32 pe40_r_cable_1a_lower;
	u32 pe40_r_cable_2a_lower;
	u32 pe40_r_cable_3a_lower;

	/* dual charger */
	u32 chg1_ta_ac_charger_input_current;
	u32 chg2_ta_ac_charger_input_current;
	u32 chg1_ta_ac_charger_current;
	u32 chg2_ta_ac_charger_current;
	int slave_mivr_diff;
	u32 dual_polling_ieoc;

	/* slave charger */
	int chg2_eff;
	bool parallel_vbus;

	/* cable measurement impedance */
	int cable_imp_threshold;
	int vbat_cable_imp_threshold;

	/* bif */
	int bif_threshold1;	/* uv */
	int bif_threshold2;	/* uv */
	int bif_cv_under_threshold2;	/* uv */

	/* power path */
	bool power_path_support;

	int max_charging_time; /* second */

	int bc12_charger;

	/* pd */
	int pd_vbus_upper_bound;
	int pd_vbus_low_bound;
	int pd_ichg_level_threshold;
	int pd_stop_battery_soc;

	int vsys_watt;
	int ibus_err;
	int set_cap_delay;

	/* vote */
	int enable_vote;
	int enable_cv_step;
	int step_a;
	int step_b;
	int current_a;
	int current_b;
	int current_max;
	int step_hy_down_a;
	int step_hy_down_b;

	/* ffc */
	int enable_ffc;
	int non_ffc_ieoc;
	int non_ffc_cv;
	int ffc_ieoc;
	int ffc_ieoc_warm;
	int ffc_ieoc_warm_temp_thres;
	int ffc_cv;

	/* battery verify */
	int batt_unverify_fcc_ua;
};

struct charger_data {
	int force_charging_current;
	int thermal_input_current_limit;
	int thermal_charging_current_limit;
	int input_current_limit;
	int charging_current_limit;
	int disable_charging_count;
	int input_current_limit_by_aicl;
	int junction_temp_min;
	int junction_temp_max;
};

struct charger_manager {
	bool init_done;
	const char *algorithm_name;
	struct platform_device *pdev;
	void	*algorithm_data;
	int usb_state;
	int usb_type;
	bool usb_unlimited;
	bool disable_charger;

	struct charger_device *chg1_dev;
	struct notifier_block chg1_nb;
	struct charger_data chg1_data;
	struct charger_consumer *chg1_consumer;

	struct charger_device *chg2_dev;
	struct notifier_block chg2_nb;
	struct charger_data chg2_data;

	struct charger_device *dvchg1_dev;
	struct notifier_block dvchg1_nb;
	struct charger_data dvchg1_data;

	struct charger_device *dvchg2_dev;
	struct notifier_block dvchg2_nb;
	struct charger_data dvchg2_data;

	struct adapter_device *pd_adapter;
#ifdef CONFIG_XMUSB350_DET_CHG
	int xmusb_vid;
#endif

	enum charger_type chr_type;
	enum hvdcp_status hvdcp_type;
	enum wireless_chg_state wireless_status;
	int hvdcp_check_count;
	bool can_charging;
	int cable_out_cnt;
	int temp_level;

	int (*do_algorithm)(struct charger_manager *cm);
	int (*plug_in)(struct charger_manager *cm);
	int (*plug_out)(struct charger_manager *cm);
	int (*do_charging)(struct charger_manager *cm, bool en);
	int (*do_event)(struct notifier_block *nb, unsigned long ev, void *v);
	int (*change_current_setting)(struct charger_manager *cm);

	/* notify charger user */
	struct srcu_notifier_head evt_nh;
	/* receive from battery */
	struct notifier_block psy_nb;
	bool swjeita_enable_dual_charging;
	/* common info */
	int battery_temp;

	/* sw jeita */
	bool enable_sw_jeita;
	struct sw_jeita_data sw_jeita;

	/* dynamic_cv */
	bool enable_dynamic_cv;

	bool cmd_discharging;
	bool safety_timeout;
	bool vbusov_stat;

	/* battery warning */
	unsigned int notify_code;
	unsigned int notify_test_mode;

	/* battery thermal protection */
	struct battery_thermal_protection_data thermal;

	/* dtsi custom data */
	struct charger_custom_data data;

	bool enable_sw_safety_timer;
	bool sw_safety_timer_setting;

	/* High voltage charging */
	bool enable_hv_charging;

	/* pe */
	bool enable_pe_plus;
	struct mtk_pe pe;

	/* pe 2.0 */
	bool enable_pe_2;
	struct mtk_pe20 pe2;

	/* pe 4.0 */
	bool enable_pe_4;
	bool leave_pe4;
	struct mtk_pe40 pe4;

	/* pe 5.0 */
	bool enable_pe_5;
	bool leave_pe5;
	struct mtk_pe50 pe5;

	/* type-C*/
	bool enable_type_c;

	/* water detection */
	bool water_detected;

	/* pd */
	bool leave_pdc;
	bool stop_pdc_with_dis_hv;
	struct mtk_pdc pdc;
	bool disable_pd_dual;
	bool is_pdc_run;

		/* Ra Rp detection */
	bool ra_detected;
	int	rp_lvl;
	
	int pd_type;
	bool pd_reset;

	/* thread related */
	struct hrtimer charger_kthread_timer;

	/* alarm timer */
	struct alarm charger_timer;
	struct timespec endtime;
	bool is_suspend;

	struct wakeup_source *charger_wakelock;
	struct mutex charger_lock;
	struct mutex charger_pd_lock;
	struct mutex cable_out_lock;
	spinlock_t slock;
	unsigned int polling_interval;
	bool charger_thread_timeout;
	wait_queue_head_t  wait_que;
	bool charger_thread_polling;

	/* kpoc */
	atomic_t enable_kpoc_shdn;

	/* ATM */
	bool atm_enabled;

	/* dynamic mivr */
	bool enable_dynamic_mivr;

	bool force_disable_pp[TOTAL_CHARGER];
	bool enable_pp[TOTAL_CHARGER];
	struct mutex pp_lock[TOTAL_CHARGER];

	/* input suspend*/
	bool is_input_suspend;

	/*thermal level*/
	int system_temp_level;
	int system_temp_level_max;
	int set_temp_enable;
	int set_temp_num;

	int	 *thermal_mitigation_dcp;
	int	 *thermal_mitigation_qc3p5;
	int	 *thermal_mitigation_qc3;
	int	 *thermal_mitigation_qc3_classb;
	int	 *thermal_mitigation_qc2;
	int	 *thermal_mitigation_pd_base;

	struct power_supply	*usb_psy;
	struct power_supply	*battery_psy;
	struct power_supply	*bq_psy;
	struct power_supply	*bms_psy;
	struct power_supply	*main_psy;
	struct power_supply	*batt_verify_psy;

	/*delay work*/
	struct delayed_work	pd_hard_reset_work;
	struct delayed_work	charger_type_recheck_work;
	struct delayed_work dcp_confirm_work;
	struct work_struct	batt_verify_update_work;

	/* vote */
	int effective_fcc;

	/* step chg */
	int step_flag;

	/* ffc */
	int ffc_ieoc;
	int ffc_cv;

	/* soc decimal rate */
	int     *dec_rate_seq;
	int     dec_rate_len;

	/* charger type recheck related */
	int			recheck_charger;
	int			precheck_charger_type;
	int			real_charger_type;
	int			check_count;
	bool			use_xmusb350_do_apsd;

	bool disable_soc_decimal;

	/* flag to confirm dcp type to fix some qc3 detection error issue */
	bool			dcp_confirmed;

	/* battery verify */
	bool			batt_verified;

	/* pd verify in */
	bool			pd_verify_in_process;

	int			mode_bf;

	/* plug in time*/
	struct timespec plugintime;
};

/* Power Supply */
struct mt_charger {
	struct device *dev;
	struct power_supply_desc chg_desc;
	struct power_supply_config chg_cfg;
	struct power_supply *chg_psy;
	struct power_supply_desc ac_desc;
	struct power_supply_config ac_cfg;
	struct power_supply *ac_psy;
	struct power_supply_desc usb_desc;
	struct power_supply_config usb_cfg;
	struct power_supply *usb_psy;
	struct power_supply *bms_psy;
	struct power_supply_desc main_desc;
	struct power_supply_config main_cfg;
	struct power_supply *main_psy;
	struct power_supply *charger_identify_psy;
	struct chg_type_info *cti;
	bool chg_online; /* Has charger in or not */
	bool vbus_disable;
	enum charger_type chg_type;
	//enum hvdcp_status	hvdcp_type;
	struct charger_device *chg1_dev;

	struct delayed_work	clear_soc_decimal_rate_work;
};

/* charger related module interface */
extern int charger_manager_notifier(struct charger_manager *info, int event);
extern int mtk_switch_charging_init(struct charger_manager *info);
extern int mtk_switch_charging_init2(struct charger_manager *info);
extern int mtk_dual_switch_charging_init(struct charger_manager *info);
extern int mtk_linear_charging_init(struct charger_manager *info);
extern void _wake_up_charger(struct charger_manager *info);
extern int mtk_get_dynamic_cv(struct charger_manager *info, unsigned int *cv);
extern bool is_dual_charger_supported(struct charger_manager *info);
extern int charger_enable_vbus_ovp(struct charger_manager *pinfo, bool enable);
extern bool is_typec_adapter(struct charger_manager *info);

/* pmic API */
extern unsigned int upmu_get_rgs_chrdet(void);
extern int pmic_get_vbus(void);
extern int pmic_get_charging_current(void);
extern int pmic_get_battery_voltage(void);
extern int pmic_get_bif_battery_voltage(int *vbat);
extern int pmic_is_bif_exist(void);
extern int pmic_enable_hw_vbus_ovp(bool enable);
extern bool pmic_is_battery_exist(void);


/* ffc */
extern int chg_get_fastcharge_mode(void);
extern int chg_set_fastcharge_mode(bool enable);

/* soc decimal */
int get_disable_soc_decimal_flag(void);

extern void notify_adapter_event(enum adapter_type type, enum adapter_event evt,
	void *val);

/* maxim */
extern bool suppld_maxim;

int mtk_charger_set_prop_type_recheck(const union power_supply_propval *val);
int mtk_charger_get_prop_type_recheck(union power_supply_propval *val);

int mtk_charger_set_prop_pd_verify_process(const union power_supply_propval *val);
int mtk_charger_get_prop_pd_verify_process(union power_supply_propval *val);

/* FIXME */
enum usb_state_enum {
	USB_SUSPEND = 0,
	USB_UNCONFIGURED,
	USB_CONFIGURED
};

#if defined(CONFIG_MACH_MT6877) || defined(CONFIG_MACH_MT6893) \
	|| defined(CONFIG_MACH_MT6885) || defined(CONFIG_MACH_MT6785) \
	|| defined(CONFIG_MACH_MT6853) || defined(CONFIG_MACH_MT6873)
bool is_usb_rdy(struct device *dev);
#else
bool __attribute__((weak)) is_usb_rdy(void)
{
	pr_info("%s is not defined\n", __func__);
	return false;
}
#endif

/* procfs */
#define PROC_FOPS_RW(name)						\
static int mtk_chg_##name##_open(struct inode *node, struct file *file)	\
{									\
	return single_open(file, mtk_chg_##name##_show, PDE_DATA(node));\
}									\
static const struct file_operations mtk_chg_##name##_fops = {		\
	.owner = THIS_MODULE,						\
	.open = mtk_chg_##name##_open,					\
	.read = seq_read,						\
	.llseek = seq_lseek,						\
	.release = single_release,					\
	.write = mtk_chg_##name##_write,				\
}

#endif /* __MTK_CHARGER_INTF_H__ */