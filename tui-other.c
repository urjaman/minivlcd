#include "main.h"
#include "backlight.h"
#include "timer.h"
#include "tui.h"
#include "lcd.h"
#include "lib.h"
#include "buttons.h"
#include "dallas.h"
#include "rtc.h"
#include "adc.h"

// Debug Info Menu start	


const unsigned char tui_dm_s1[] PROGMEM = "UPTIME";
const unsigned char tui_dm_s2[] PROGMEM = "RTC INFO";
const unsigned char tui_dm_s3[] PROGMEM = "ADC SAMPLES/S";
const unsigned char tui_dm_s4[] PROGMEM = "5HZ COUNTER";
const unsigned char tui_dm_s5[] PROGMEM = "RAW ADC VIEW";
const unsigned char tui_dm_s6[] PROGMEM = "ADC CALIBRATION";
PGM_P const tui_dm_table[] PROGMEM = {
    (PGM_P)tui_dm_s1, // uptime
    (PGM_P)tui_dm_s2, // rtc info
    (PGM_P)tui_dm_s3, // adc samples
    (PGM_P)tui_dm_s4, // 5hz counter
    (PGM_P)tui_dm_s5, // Raw ADC view
    (PGM_P)tui_dm_s6, // ADC Calibration
    (PGM_P)tui_exit_menu, // exit
};

void tui_time_print(uint32_t nt) {
	unsigned char time[16];
	// Time Format: "DDDDDd HH:MM:SS"
	uint16_t days;
	uint8_t hours, mins, secs, x,z;
	days = nt / 86400L; nt = nt % 86400L;
	hours = nt / 3600L; nt = nt % 3600L;
	mins = nt / 60; 
	secs = nt % 60;

	time[0] = (days/10000)|0x30; days = days % 10000;
	time[1] = (days /1000)|0x30; days = days % 1000;
	time[2] = (days / 100)|0x30; days = days % 100;
	time[3] = (days / 10 )|0x30;
	time[4] = (days % 10 )|0x30;
	time[5] = 'd';
	time[6] = ' ';
	time[7] = (hours/10) | 0x30;
	time[8] = (hours%10) | 0x30;
	time[9] = ':';
	time[10]= (mins /10) | 0x30;
	time[11]= (mins %10) | 0x30;
	time[12]= ':';
	time[13]= (secs /10) | 0x30;
	time[14]= (secs %10) | 0x30;
	time[15] = 0;
	z=0;
	if (time[0] == '0') {
		z=1;
		if (time[1] == '0') {
			z=2;
			if (time[2] == '0') {
				z=3;
				if (time[3]  == '0') {
					z=4;
					if (time[4] == '0') {
						z = 7;
						if (hours == 0) {
							z = 10;
						}
					}
				}
			}
		}
	}
	x = (16 - (15-z)) / 2;

	lcd_gotoxy(x,1);
	lcd_puts(&(time[z]));
}

static void tui_uptime(void) {
	uint8_t x;
	lcd_clear();
	lcd_gotoxy(5,0);
	lcd_puts_P((PGM_P)tui_dm_s1);
	for (;;) {
		tui_time_print(timer_get());
		for (;;) {
			x = buttons_get();
			mini_mainloop();
			if (x) break;
			if (timer_get_1hzp()) 
				break;
		}
		if (x) break;
	}
}

static void tui_adc_ss(void) {
	unsigned char buf[10];
	extern uint16_t adc_avg_cnt;
	for (;;) {
		uint8_t x;
		lcd_clear();
		uint2str(buf,adc_avg_cnt);
		lcd_puts(buf);
		for(;;) {
			x = buttons_get();
			mini_mainloop();
			if (x) break;
			if (timer_get_1hzp()) break;
		}
		if (x) break;
	}
}

static void tui_timer_5hzcnt(void) {
	unsigned char buf[10];
	for (;;) {
		uint8_t x;
		uint8_t timer=timer_get_5hz_cnt();
		lcd_clear();
		uchar2str(buf,timer);
		lcd_puts(buf);
		while (timer==timer_get_5hz_cnt()) {
			x = buttons_get();
			mini_mainloop();
			if (x) return;
		}
	}
}

static void tui_raw_adc_view(void) {
	unsigned char buf[10];
	for (;;) {
		uint8_t x;
		lcd_clear();
		uint2str(buf,adc_raw_values[0]);
		lcd_puts(buf);
		adc_print_v(buf,adc_raw_values[0]);
		lcd_gotoxy(10,0);
		lcd_puts(buf);
		lcd_gotoxy(0,1);
		uint2str(buf,adc_raw_values[1]);
		lcd_puts(buf);
		adc_print_v(buf,adc_raw_values[1]);
		lcd_gotoxy(10,1);
		lcd_puts(buf);
		for (;;) {
			x = buttons_get();
			mini_mainloop();
			if (x) break;
			if (timer_get_1hzp()) 
				break;
		}
		if (x) break;
	}
}

// Voltage: 13.63V
// A: 3515
// B: 3507
// Raw 13.63V is 3489,28
// Thus MB_SCALE = 3489,28 / 3515 * 65536 => 65056,45919
#define ADC_MB_SCALE 65056
// SB_SCALE = 3489,28 / 3507 * 65536 => 65204,86284
#define ADC_SB_SCALE 65205
// Calib is 65536+diff, thus saved is diff = calib - 65536.
//int16_t adc_calibration_diff[ADC_MUX_CNT] = { ADC_MB_SCALE-65536, ADC_SB_SCALE-65536 };

static void tui_adc_calibrate(void) {
	const uint16_t min_calib_v = 6*256; // Min 6V on a channel to calibrate it.
	uint16_t target;
	uint16_t mcv = adc_read_mb();
	uint16_t scv = adc_read_sb();
	if ((mcv>min_calib_v)&&(scv>min_calib_v)) {
		target = (mcv+scv)/2;
	} else if (mcv>min_calib_v) {
		target = mcv;
	} else if (scv>min_calib_v) {
		target = scv;
	} else {
		tui_gen_message(PSTR("INVALID VOLTAGE"),PSTR("VALUES; <6V"));
		return;
	}
	uint16_t dV_target = adc_to_dV(target);
	uint32_t mbc=0;
	uint32_t sbc=0;
	for(;;) {
		uint8_t buf[10];
		uint8_t x;
		if (adc_raw_values[0]>min_calib_v) { // Generate MB calib value
			uint16_t v = adc_raw_values[0];
			mbc = ((((uint32_t)target)*65536UL)+(v/2))/v;
			if ((mbc<33000)||(mbc>98000)) mbc=0;
		}
		if (adc_raw_values[1]>min_calib_v) { // Generate SB calib value
			uint16_t v = adc_raw_values[1];
			sbc = ((((uint32_t)target)*65536UL)+(v/2))/v;
			if ((sbc<33000)||(sbc>98000)) sbc=0;
		}
		lcd_clear();
		buf[0] = 'M';
		buf[1] = ':';
		luint2str(buf+2,mbc);
		lcd_puts(buf);
		lcd_gotoxy(0,1);
		buf[0] = 'S';
		luint2str(buf+2,sbc);
		lcd_puts(buf);
		adc_print_dV(buf,dV_target);
		lcd_gotoxy(10,0);
		lcd_puts(buf);
		for (;;) {
			x = buttons_get();
			mini_mainloop();
			if (x) break;
			if (timer_get_1hzp()) 
				break;
		}
		switch (x) {
			default: break;
			case BUTTON_S1:
				dV_target++;
				if (dV_target>1600) dV_target = 800;
				target = adc_from_dV(dV_target);
				x = 0;
				break;
			case BUTTON_S2:
				dV_target--;
				if (dV_target<800) dV_target = 1600;
				target = adc_from_dV(dV_target);
				x = 0;
				break;
		}
		if (x) break;
	}
	PGM_P gts = PSTR("GOING TO SAVE");
	if (mbc) {
		tui_gen_message(gts,PSTR("MB ADC CALIB"));
		if (tui_are_you_sure()) {
			int32_t v = mbc;
			v = v - 65536;
			adc_calibration_diff[0] = v;
		}
	}
	if (sbc) {
		tui_gen_message(gts,PSTR("SB ADC CALIB"));
		if (tui_are_you_sure()) {
			int32_t v = sbc;
			v = v - 65536;
			adc_calibration_diff[1] = v;
		}
	}
}

const unsigned char tui_om_s5[] PROGMEM = "DEBUG INFO";
static void tui_debuginfomenu(void) {
	uint8_t sel=0;	
	for (;;) {
		sel = tui_gen_listmenu((PGM_P)tui_om_s5, tui_dm_table, 7, sel);
		switch (sel) {
			case 0:
				tui_uptime();
				break;
			case 1: {
				PGM_P l1 = PSTR("RTC IS");
				if (rtc_valid()) {
					tui_gen_message(l1,PSTR("VALID"));
				} else {
					tui_gen_message(l1,PSTR("INVALID"));
				}
				}
				break;
			case 2: 
				tui_adc_ss();
				break;
			case 3:
				tui_timer_5hzcnt();
				break;
			case 4:
				tui_raw_adc_view();
				break;
			case 5:
				tui_adc_calibrate();
				break;
			default:
				return;
		}
	}
}
// Useful tools start

const unsigned char tui_om_s2[] PROGMEM = "STOPWATCH";
static void tui_stopwatch(void) {
	uint16_t timer=0;
	unsigned char time[8];
	// Format: mm:ss.s
	lcd_clear();
	lcd_gotoxy(3,0);
	lcd_puts_P((PGM_P)tui_om_s2);
	for(;;) {
		mini_mainloop();
		if (!buttons_get_v()) break;
	}
	timer_delay_ms(100);
	for(;;) {
		mini_mainloop();
		if (buttons_get_v()) break;
	}
	uint8_t last_5hztimer = timer_get_5hz_cnt();
	time[6] = time[4] = time[3] = time[1] = time[0] = '0';
	time[2] = ':';
	time[5] = '.';
	time[7] = 0;
	lcd_gotoxy(4,1);
	lcd_puts(time);
	timer_delay_ms(150);
	for(;;) {
		mini_mainloop();
		if (timer_get_1hzp()) backlight_activate(); // Keep backlight on
		uint16_t tt,t2;
		uint8_t passed = timer_get_5hz_cnt() - last_5hztimer;
		timer += passed*2;
		last_5hztimer += passed;
		if (timer>=60000) timer=0;
		time[6] = 0x30 | (timer%10);
		tt = timer/10;
		t2 = tt%60;
		tt = tt/60;
		time[4] = 0x30 | (t2%10);
		time[3] = 0x30 | (t2/10);
		time[1] = 0x30 | (tt%10);
		time[0] = 0x30 | (tt/10);
		lcd_gotoxy(4,1);
		lcd_puts(time);
		if (buttons_get_v()) break;
	}
	tui_waitforkey();
}

const unsigned char tui_om_s1[] PROGMEM = "CALC";
// StopWatch
const unsigned char tui_om_s3[] PROGMEM = "FUEL COST";
const unsigned char tui_om_s4[] PROGMEM = "FC HISTORY";
// Debug Info
const unsigned char tui_om_s6[] PROGMEM = "POWER OFF";

PGM_P const tui_om_table[] PROGMEM = {
    (PGM_P)tui_om_s1, // calc
    (PGM_P)tui_om_s2, // stopwatch
    (PGM_P)tui_om_s3, // fc
    (PGM_P)tui_om_s4, // fc history
    (PGM_P)tui_om_s5, // debug info
    (PGM_P)tui_om_s6, // power off
    (PGM_P)tui_exit_menu, // exit
};

void tui_othermenu(void) {
	uint8_t sel=0;	
	for (;;) {
		sel = tui_gen_listmenu(PSTR("OTHERS"), tui_om_table, 7, sel);
		switch (sel) {
			default:
				return;
			case 0:
				tui_calc();
				break;
			case 1:
				tui_stopwatch();
				break;
			case 2: 
				tui_calc_fuel_cost();
				break;
			case 3:
				tui_calc_fc_history();
				break;
			case 4:
				tui_debuginfomenu();
				break;
			case 5:
				tui_poweroff();
				break;
		}
	}
}
