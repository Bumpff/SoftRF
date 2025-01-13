/*
 * Settings.cpp - formerly EEPROMHelper.cpp
 * Copyright (C) 2016-2021 Linar Yusupov
 * Settings scheme redesigned by Moshe Braner 2024
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "../system/SoC.h"
#include "Filesys.h"
#include "Settings.h"
#include "RF.h"
#include "LED.h"
#include "EPD.h"
#include "Buzzer.h"
#include "Bluetooth.h"
#include "../TrafficHelper.h"
#include "../protocol/radio/Legacy.h"
#include "../protocol/data/NMEA.h"
#include "../protocol/data/GDL90.h"
#include "../protocol/data/D1090.h"
#include "../protocol/data/JSON.h"
#include "Battery.h"

#if defined(EXCLUDE_EEPROM)
void Settings_setup()    {}
void EEPROM_store()    {}
#else

#if defined(ESP32)
#define ABANDON_EEPROM
#endif

#if defined(ARDUINO_ARCH_NRF52)
#define ABANDON_EEPROM
#endif

#if defined(ABANDON_EEPROM)
// read the EEPROM one more time, copy to settings.txt file, then mark EEPROM as obsolete
settings_t settings_stored;
#else
#define settings_t settingb_t
// keep using the packed settings structure
#endif

uint8_t settings_used;
int settings_file_version = 0;

settings_t *settings;
settingb_t *settingb;

bool do_alarm_demo = false;        // activated by middle button on T-Beam
bool landed_out_mode = false;      // activated by button in status web page

bool test_mode = false;            // activated by double-clicking middle button on T-Beam
                                    // - or via web interface, or via $PSRFT
// Upon receiving a $PSRFT NMEA command,
// first the variable test_mode is toggled, then
// this is called, whether test_mode is on or off
// put custom code here for debugging, for example:
#include "../protocol/data/GNS5892.h"
void do_test_mode()
{
#if defined(ESP32)
    if (settings->rx1090)
        gns5892_test_mode();
#endif
}

uint32_t baudrates[8] = 
{
    0,
    4800,
    9600,
    19200,
    38400,
    57600,
    115200,
    0
};

setting_struct stgdesc[STG_END];
const char * stgcomment[STG_END] = {NULL};

struct setting_minmax {
    uint8_t index;
    int8_t min;
    int8_t max;
};
#define NUM_MINMAX 2    // may need to manually enlarge this
setting_minmax stgminmax[NUM_MINMAX];

inline int8_t esp_only(int8_t stg_type)
{
#if defined(ESP32)
    return stg_type;
#else
    return STG_VOID;
#endif
}

inline int8_t epd_only(int8_t stg_type)
{
#if defined(USE_EPAPER)
    return stg_type;
#else
    return STG_VOID;
#endif
}

static void init_stgdesc()
{
  stgdesc[STG_NONE]       = { "none",       (char*)&settings->version,    STG_VOID };
  stgdesc[STG_VERSION]    = { "SoftRF",     (char*)&settings->version,    STG_UINT1 };
  stgdesc[STG_MODE]       = { "mode",       (char*)&settings->mode,       STG_UINT1 };
  stgdesc[STG_PROTOCOL]   = { "protocol",   (char*)&settings->rf_protocol,STG_UINT1 };
  stgdesc[STG_BAND]       = { "band",       (char*)&settings->band,       STG_UINT1 };
  stgdesc[STG_ACFT_TYPE]  = { "acft_type",  (char*)&settings->acft_type,  STG_UINT1 };
  stgdesc[STG_ID_METHOD]  = { "id_method",  (char*)&settings->id_method,  STG_UINT1 };
  stgdesc[STG_AIRCRAFT_ID]= { "aircraft_id",(char*)&settings->aircraft_id,STG_HEX6 };
  stgdesc[STG_IGNORE_ID]  = { "ignore_id",  (char*)&settings->ignore_id,  STG_HEX6 };
  stgdesc[STG_FOLLOW_ID]  = { "follow_id",  (char*)&settings->follow_id,  STG_HEX6 };
  stgdesc[STG_ALARM]      = { "alarm",      (char*)&settings->alarm,      STG_UINT1 };
  stgdesc[STG_HRANGE]     = { "hrange",     (char*)&settings->hrange,     STG_UINT1 };
  stgdesc[STG_VRANGE]     = { "vrange",     (char*)&settings->vrange,     STG_UINT1 };
  stgdesc[STG_TXPOWER]    = { "txpower",    (char*)&settings->txpower,    STG_UINT1 };
  stgdesc[STG_VOLUME]     = { "volume",     (char*)&settings->volume,     STG_UINT1 };
  stgdesc[STG_POINTER]    = { "pointer",    (char*)&settings->pointer,    STG_UINT1 };
  stgdesc[STG_STROBE]     = { "strobe",     (char*)&settings->strobe,     esp_only(STG_UINT1) };
  stgdesc[STG_VOICE]      = { "voice",      (char*)&settings->voice,      esp_only(STG_UINT1) };
  stgdesc[STG_OWNSSID]    = { "myssid",     settings->myssid,      esp_only(sizeof(settings->myssid)) };
  stgdesc[STG_EXTSSID]    = { "ssid",       settings->ssid,        esp_only(sizeof(settings->ssid)) };
  stgdesc[STG_PSK]        = { "psk",        settings->psk,         esp_only(sizeof(settings->psk)) };
  stgdesc[STG_HOST_IP]    = { "host_ip",    settings->host_ip,     esp_only(sizeof(settings->host_ip)) };
  stgdesc[STG_TCPMODE]    = { "tcpmode",    (char*)&settings->tcpmode,    esp_only(STG_UINT1) };
  stgdesc[STG_TCPPORT]    = { "tcpport",    (char*)&settings->tcpport,    esp_only(STG_UINT1) };
  stgdesc[STG_BLUETOOTH]  = { "bluetooth",  (char*)&settings->bluetooth,  STG_UINT1 };
  stgdesc[STG_BAUD_RATE]  = { "baud_rate",  (char*)&settings->baud_rate,  STG_UINT1 };
  stgdesc[STG_NMEA_OUT]   = { "nmea_out",   (char*)&settings->nmea_out,   STG_UINT1 };
  stgdesc[STG_NMEA_G]     = { "nmea_g",     (char*)&settings->nmea_g,     STG_UINT1 };
  stgdesc[STG_NMEA_P]     = { "nmea_p",     (char*)&settings->nmea_p,     STG_UINT1 };
  stgdesc[STG_NMEA_L]     = { "nmea_l",     (char*)&settings->nmea_l,     STG_UINT1 };
  stgdesc[STG_NMEA_S]     = { "nmea_s",     (char*)&settings->nmea_s,     STG_UINT1 };
  stgdesc[STG_NMEA_D]     = { "nmea_d",     (char*)&settings->nmea_d,     STG_UINT1 };
  stgdesc[STG_NMEA_E]     = { "nmea_e",     (char*)&settings->nmea_e,     STG_UINT1 };
  stgdesc[STG_NMEA_OUT2]  = { "nmea_out2",  (char*)&settings->nmea_out2,  STG_UINT1 };
  stgdesc[STG_NMEA2_G]    = { "nmea2_g",    (char*)&settings->nmea2_g,    STG_UINT1 };
  stgdesc[STG_NMEA2_P]    = { "nmea2_p",    (char*)&settings->nmea2_p,    STG_UINT1 };
  stgdesc[STG_NMEA2_L]    = { "nmea2_l",    (char*)&settings->nmea2_l,    STG_UINT1 };
  stgdesc[STG_NMEA2_S]    = { "nmea2_s",    (char*)&settings->nmea2_s,    STG_UINT1 };
  stgdesc[STG_NMEA2_D]    = { "nmea2_d",    (char*)&settings->nmea2_d,    STG_UINT1 };
  stgdesc[STG_NMEA2_E]    = { "nmea2_e",    (char*)&settings->nmea2_e,    STG_UINT1 };
  stgdesc[STG_ALTPIN0]    = { "altpin0",    (char*)&settings->altpin0,    esp_only(STG_UINT1) };
  stgdesc[STG_BAUDRATE2]  = { "baudrate2",  (char*)&settings->baudrate2,  esp_only(STG_UINT1) };
  stgdesc[STG_INVERT2]    = { "invert2",    (char*)&settings->invert2,    esp_only(STG_UINT1) };
  stgdesc[STG_ALT_UDP]    = { "alt_udp",    (char*)&settings->alt_udp,    esp_only(STG_UINT1) };
  stgdesc[STG_RX1090]     = { "rx1090",     (char*)&settings->rx1090,     esp_only(STG_UINT1) };
  stgdesc[STG_RX1090X]    = { "rx1090x",    (char*)&settings->rx1090x,    esp_only(STG_UINT1) };
  stgdesc[STG_MODE_S]     = { "mode_s",     (char*)&settings->mode_s,     esp_only(STG_UINT1) };
  stgdesc[STG_HRANGE1090] = { "hrange1090", (char*)&settings->hrange1090, STG_UINT1 };
  stgdesc[STG_VRANGE1090] = { "vrange1090", (char*)&settings->vrange1090, STG_UINT1 };
  stgdesc[STG_GDL90_IN]   = { "gdl90_in",   (char*)&settings->gdl90_in,   esp_only(STG_UINT1) };
  stgdesc[STG_GDL90]      = { "gdl90",      (char*)&settings->gdl90,      STG_UINT1 };
  stgdesc[STG_D1090]      = { "d1090",      (char*)&settings->d1090,      STG_UINT1 };
  stgdesc[STG_RELAY]      = { "relay",      (char*)&settings->relay,      STG_UINT1 };
  stgdesc[STG_STEALTH]    = { "stealth",    (char*)&settings->stealth,    STG_UINT1 };
  stgdesc[STG_NO_TRACK]   = { "no_track",   (char*)&settings->no_track,   STG_UINT1 };
  stgdesc[STG_POWER_SAVE] = { "power_save", (char*)&settings->power_save, STG_UINT1 };
  stgdesc[STG_POWER_EXT]  = { "power_ext",  (char*)&settings->power_ext,  STG_UINT1 };
  stgdesc[STG_RFC]        = { "rfc",        (char*)&settings->freq_corr,  STG_INT1 };
  stgdesc[STG_ALARMLOG]   = { "alarmlog",   (char*)&settings->logalarms,  STG_UINT1 };
  stgdesc[STG_GNSS_PINS]  = { "gnss_pins",  (char*)&settings->gnss_pins,  esp_only(STG_UINT1) };
  stgdesc[STG_PPSWIRE]    = { "ppswire",    (char*)&settings->ppswire,    esp_only(STG_UINT1) };
  stgdesc[STG_SD_CARD]    = { "sd_card",    (char*)&settings->sd_card,    esp_only(STG_UINT1) };
  stgdesc[STG_LOGFLIGHT]  = { "logflight",  (char*)&settings->logflight,  STG_UINT1 };
  stgdesc[STG_LOGINTERVAL]= { "loginterval",(char*)&settings->loginterval,STG_UINT1 };
  stgdesc[STG_IGC_PILOT]  = { "igc_pilot",   settings->igc_pilot,         sizeof(settings->igc_pilot) };
  stgdesc[STG_IGC_TYPE]   = { "igc_type",    settings->igc_type,          sizeof(settings->igc_type) };
  stgdesc[STG_IGC_REG]    = { "igc_reg",     settings->igc_reg,           sizeof(settings->igc_reg) };
  stgdesc[STG_IGC_CS]     = { "igc_cs",      settings->igc_cs,            sizeof(settings->igc_cs) };
  stgdesc[STG_GEOID]      = { "geoid",      (char*)&settings->geoid,      STG_INT1 };
  stgdesc[STG_JSON]       = { "json",       (char*)&settings->json,       STG_UINT1 };
  stgdesc[STG_EPD_UNITS]  = { "units",      (char*)&settings->units,      epd_only(STG_UINT1) };
  stgdesc[STG_EPD_ZOOM]   = { "zoom",       (char*)&settings->zoom,       epd_only(STG_UINT1) };
  stgdesc[STG_EPD_ROTATE] = { "rotate",     (char*)&settings->rotate,     epd_only(STG_UINT1) };
  stgdesc[STG_EPD_ORIENT] = { "orientation",(char*)&settings->orientation,epd_only(STG_UINT1) };
  stgdesc[STG_EPD_ADB]    = { "adb",        (char*)&settings->adb,        epd_only(STG_UINT1) };
  stgdesc[STG_EPD_IDPREF] = { "epdidpref",  (char*)&settings->epdidpref,  epd_only(STG_UINT1) };
  stgdesc[STG_EPD_VMODE]  = { "viewmode",   (char*)&settings->viewmode,   epd_only(STG_UINT1) };
  stgdesc[STG_EPD_AGHOST] = { "antighost",  (char*)&settings->antighost,  epd_only(STG_UINT1) };
  stgdesc[STG_EPD_TEAM]   = { "team",       (char*)&settings->team,       epd_only(STG_HEX6) };
  stgdesc[STG_DEBUG_FLAGS]= { "debug_flags",(char*)&settings->debug_flags,STG_HEX2 };

  // ensure no null labels in the array
  for (int i=0; i<STG_END; i++) {
     if (!stgdesc[i].label) {
         Serial.print("stg[");
         Serial.print(i);
         Serial.println("] - empty label");
         stgdesc[i].label = stgdesc[STG_NONE].label;
         stgdesc[i].type  = STG_VOID;
     }
  }

  const char *yesno = "1=yes 0=no";
  const char *destinations = "0=off 1=serial 2=UDP 3=TCP 4=USB 5=BT ...";
  const char *bauds = "0=default(38) 2=9600 3=19200 4=38400 ...";

  stgcomment[STG_MODE]       = "0=Normal 10=More NMEA";
  stgcomment[STG_PROTOCOL]   = "7=Latest 1=OGNTP 5=FANET 2=P3I";
  stgcomment[STG_BAND]       = "1=EU 2=US ...";
  stgcomment[STG_ACFT_TYPE]  = "1=GL 2=TOWPL 6=HG 7=PG ...";
  stgcomment[STG_ID_METHOD]  = "1=ICAO 2=device";
  stgcomment[STG_ALARM]      = "3=Legacy 2=Vector 1=Dist";
  stgcomment[STG_HRANGE]     = "km";
  stgcomment[STG_VRANGE]     = "x100m";
  stgcomment[STG_TXPOWER]    = "0=full 1=low 2=off";
  stgcomment[STG_VOLUME]     = "0=off 1=low 2=full 3=ext";
  stgcomment[STG_BLUETOOTH]  = "0=off 1=classic 2=BLE";
  stgcomment[STG_BAUD_RATE]  = bauds;
  stgcomment[STG_NMEA_OUT]   = destinations;
  stgcomment[STG_NMEA_G]     = yesno;
  stgcomment[STG_NMEA_P]     = yesno;
  stgcomment[STG_NMEA_L]     = yesno;
  stgcomment[STG_NMEA_S]     = yesno;
  stgcomment[STG_NMEA_D]     = yesno;
  stgcomment[STG_NMEA_E]     = yesno;
  stgcomment[STG_NMEA_OUT2]  = destinations;
  stgcomment[STG_NMEA2_G]    = yesno;
  stgcomment[STG_NMEA2_P]    = yesno;
  stgcomment[STG_NMEA2_L]    = yesno;
  stgcomment[STG_NMEA2_S]    = yesno;
  stgcomment[STG_NMEA2_D]    = yesno;
  stgcomment[STG_NMEA2_E]    = yesno;
  stgcomment[STG_BAUDRATE2]  = bauds;
  stgcomment[STG_HRANGE1090] = "km";
  stgcomment[STG_VRANGE1090] = "x100m";
//stgcomment[STG_GDL90_IN]   = destinations;
//stgcomment[STG_GDL90]      = destinations;
//stgcomment[STG_D1090]      = destinations;
  stgcomment[STG_RELAY]      = "0=off 1=landed 2=ADS-B 3=only";
  stgcomment[STG_STEALTH]    = yesno;
  stgcomment[STG_NO_TRACK]   = yesno;
  stgcomment[STG_ALARMLOG]   = yesno;
  stgcomment[STG_LOGFLIGHT]  = "0=off 1=always 2=airborne 3=traffic";
  stgcomment[STG_LOGINTERVAL]= "0=1s 1=2s 2=4s 3=8s";
#if defined(USE_EPAPER)
  stgcomment[STG_EPD_UNITS]  = "0=metric 1=imperial 2=mixed";
  stgcomment[STG_EPD_IDPREF] = "0=reg 1=tail 2=model 3=type";
  stgcomment[STG_EPD_AGHOST] = "0=off 1=auto 2=2min 3=5min";
#endif

  stgminmax[0] = { STG_RFC,    -30, 30 };
  stgminmax[1] = { STG_GEOID, -104, 84 };
}

#if defined(ABANDON_EEPROM)
// copy the settings from settingb (EEPROM) to settings (file)
static void copy_settingb()
{
    settings->mode = settingb->mode;
    settings->rf_protocol = settingb->rf_protocol;
    settings->band = settingb->band;
    settings->txpower = settingb->txpower;
    settings->volume = settingb->volume;
    settings->acft_type = settingb->acft_type;
    settings->geoid = 0;
    settings->nmea_g = settingb->nmea_g;
    settings->nmea_p = settingb->nmea_p;
    settings->nmea_l = settingb->nmea_l;
    settings->nmea_s = settingb->nmea_s;
    settings->nmea_d = settingb->nmea_d;
    settings->nmea_out = settingb->nmea_out;
    settings->alarm = settingb->alarm;
    settings->stealth = settingb->stealth;
    settings->no_track = settingb->no_track;
    settings->gdl90 = settingb->gdl90;
    settings->d1090 = settingb->d1090;
    settings->json = settingb->json;
    settings->pointer = settingb->pointer;
    settings->freq_corr = settingb->freq_corr;
    settings->relay = settingb->relay;
    settings->nmea_e = settingb->nmea_e;
    settings->nmea2_e = settingb->nmea2_e;
    settings->baud_rate = settingb->baud_rate;
    settings->baudrate2 = settingb->baudrate2;
    settings->aircraft_id = settingb->aircraft_id;
    settings->id_method = settingb->id_method;
    settings->ignore_id = settingb->ignore_id;
    settings->follow_id = settingb->follow_id;
    settings->nmea2_g = settingb->nmea2_g;
    settings->nmea2_p = settingb->nmea2_p;
    settings->nmea2_l = settingb->nmea2_l;
    settings->nmea2_s = settingb->nmea2_s;
    settings->nmea2_d = settingb->nmea2_d;
    settings->nmea_out2 = settingb->nmea_out2;
#if defined(ESP32)
    settings->bluetooth = settingb->bluetooth;
    settings->power_save = settingb->power_save;
    settings->power_ext = settingb->power_ext;
    settings->rx1090 = settingb->rx1090;
    settings->rx1090x = 100;
    settings->mode_s = settingb->mode_s;
    settings->gnss_pins = settingb->gnss_pins;
    settings->sd_card = settingb->sd_card;
    settings->gdl90_in = settingb->gdl90_in;
    settings->alt_udp = settingb->alt_udp;
    settings->invert2 = settingb->invert2;
    settings->altpin0 = settingb->altpin0;
    settings->voice = settingb->voice;
    settings->strobe = settingb->strobe;
    settings->logalarms = settingb->logalarms;
    settings->logflight = settingb->logflight;
    settings->loginterval = settingb->loginterval;
    settings->tcpport = settingb->tcpport;
    settings->tcpmode = settingb->tcpmode;
    settings->ppswire = settingb->ppswire;
    settings->myssid[0] = '\0';
    strcpy(settings->ssid,settingb->ssid);
    strcpy(settings->psk,settingb->psk);
    strcpy(settings->host_ip,settingb->host_ip);
    settings->debug_flags = settingb->debug_flags;
#endif
#if defined(USE_EPAPER)
    // copy from ui_settings packed struct, "ui" already points to unpacked "settings"
    settings->units = ui_settings.units;
    settings->zoom = ui_settings.zoom;
    settings->rotate = ui_settings.rotate;
    settings->orientation = ui_settings.orientation;
    settings->adb = ui_settings.adb;
    settings->epdidpref = ui_settings.epdidpref;
    settings->viewmode = ui_settings.viewmode;
    settings->antighost = ui_settings.antighost;
    settings->team = ui_settings.team;
#endif
}
#endif

// Adjust some settings after loading them
// - code moved from "EEPROM_extension".
void Adjust_Settings()
{
#if defined(ARDUINO_ARCH_NRF52)
    if (settings->mode != SOFTRF_MODE_GPSBRIDGE
        &&
        settings->mode != SOFTRF_MODE_MORENMEA
#if !defined(EXCLUDE_TEST_MODE)
        &&
        settings->mode != SOFTRF_MODE_TXRX_TEST
#endif /* EXCLUDE_TEST_MODE */
        ) {
      settings->mode = SOFTRF_MODE_NORMAL;
    }

    if (settings->bluetooth == BLUETOOTH_SPP)
        settings->bluetooth = BLUETOOTH_LE_HM10_SERIAL;

    if (settings->nmea_out == DEST_UDP  ||
        settings->nmea_out == DEST_TCP ) {
      settings->nmea_out = DEST_BLUETOOTH;
    }
    if (settings->gdl90 == DEST_UDP) {
      settings->gdl90 = DEST_BLUETOOTH;
    }
    if (settings->d1090 == DEST_UDP) {
      settings->d1090 = DEST_BLUETOOTH;
    }

    if (settings->nmea_out2 == settings->nmea_out)
        settings->nmea_out2 = DEST_NONE;
    if (settings->nmea_out2 == DEST_USB && settings->nmea_out  == DEST_UART)
        settings->nmea_out2 = DEST_NONE;
    if (settings->nmea_out  == DEST_USB && settings->nmea_out2 == DEST_UART)
        settings->nmea_out2 = DEST_NONE;

    rst_info *resetInfo = (rst_info *) SoC->getResetInfoPtr();
    if (resetInfo && resetInfo->reason == REASON_SOFT_RESTART)
        ui->viewmode = VIEW_MODE_CONF;     // after software restart show the settings
#endif /* ARDUINO_ARCH_NRF52 */

#if defined(ESP32)
#if defined(CONFIG_IDF_TARGET_ESP32) || defined(USE_USB_HOST)
    if (settings->nmea_out == DEST_USB) {
      settings->nmea_out = DEST_UART;
    }
    if (settings->gdl90 == DEST_USB) {
      settings->gdl90 = DEST_UART;
    }
    if (settings->gdl90_in == DEST_USB) {
      settings->gdl90_in = DEST_UART;
    }
#if !defined(EXCLUDE_D1090)
    if (settings->d1090 == DEST_USB) {
      settings->d1090 = DEST_UART;
    }
#endif
#endif /* CONFIG_IDF_TARGET_ESP32 */
#if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3)
    if (settings->bluetooth != BLUETOOTH_OFF) {
#if defined(CONFIG_IDF_TARGET_ESP32S3)
      settings->bluetooth = BLUETOOTH_LE_HM10_SERIAL;
#else
      settings->bluetooth = BLUETOOTH_OFF;
#endif
    }
#endif /* CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32S3 */

  /* enforce some restrictions on input and output routes */
  int nmea1 = settings->nmea_out;
  int nmea2 = settings->nmea_out2;
  Serial.print(F("NMEA_Output1 (given) = ")); Serial.println(nmea1);
  Serial.print(F("NMEA_Output2 (given) = ")); Serial.println(nmea2);
  if (hw_info.model == SOFTRF_MODEL_PRIME_MK2) {
    if (nmea1==DEST_USB)    nmea1==DEST_UART;
    if (nmea2==DEST_USB)    nmea2==DEST_UART;   // same thing
  }
  if (nmea2 == nmea1)    nmea2 = DEST_NONE;
  if (settings->gdl90_in == DEST_UDP) {
      if (settings->gdl90 == DEST_UDP) {
          settings->gdl90 = DEST_NONE;
          Serial.println(F("GDL input from UDP, GDL output turned OFF"));
      }
  }
//  bool wireless1 = (nmea1==DEST_UDP || nmea1==DEST_TCP || nmea1==DEST_BLUETOOTH);
//  bool wireless2 = (nmea2==DEST_UDP || nmea2==DEST_TCP || nmea2==DEST_BLUETOOTH);
  bool wifi1 = (nmea1==DEST_UDP || nmea1==DEST_TCP);
  bool wifi2 = (nmea2==DEST_UDP || nmea2==DEST_TCP);
// >>> try and allow Bluetooth along with WiFi:
//  if (wifi1 && nmea2==DEST_BLUETOOTH)
//        nmea2 = DEST_NONE;      // only one wireless output type possible
//  if (wifi2 && nmea1==DEST_BLUETOOTH)
//        nmea2 = DEST_NONE;
  Serial.print(F("NMEA_Output1 (adjusted) = ")); Serial.println(nmea1);
  settings->nmea_out  = nmea1;
  Serial.print(F("NMEA_Output2 (adjusted) = ")); Serial.println(nmea2);
  settings->nmea_out2 = nmea2;
  //if (nmea1==DEST_BLUETOOTH || nmea2==DEST_BLUETOOTH
  //      || settings->d1090 == DEST_BLUETOOTH || settings->gdl90 == DEST_BLUETOOTH) {
  //    if (settings->bluetooth == BLUETOOTH_OFF)
  //        settings->bluetooth = BLUETOOTH_SPP;
  //}
#if !defined(EXCLUDE_D1090)
  if (nmea1 != DEST_BLUETOOTH && nmea2 != DEST_BLUETOOTH
          && settings->d1090 != DEST_BLUETOOTH && settings->gdl90 != DEST_BLUETOOTH) {
      settings->bluetooth = BLUETOOTH_OFF;
  }
#else
  if (nmea1 != DEST_BLUETOOTH && nmea2 != DEST_BLUETOOTH && settings->gdl90 != DEST_BLUETOOTH) {
      settings->bluetooth = BLUETOOTH_OFF;
  }
#endif
  Serial.print(F("Bluetooth (adjusted) = ")); Serial.println(settings->bluetooth);

  /* enforce some hardware limitations (not enough GPIO pins) */
  if (hw_info.model == SOFTRF_MODEL_PRIME_MK2) {

      if (hw_info.revision < 8) {
          if (settings->voice == VOICE_EXT) {
              // pin 14 not available, cannot do external I2S
              settings->voice = VOICE_OFF;
          }
          if (settings->baudrate2 != BAUD_DEFAULT) {
              // aux serial uses VP, cannot use it for main serial rx
              settings->altpin0 = false;
              if (settings->gnss_pins == EXT_GNSS_15_14)
                  settings->ppswire = false;
          }
          if (settings->gnss_pins == EXT_GNSS_39_4) {
              // GNSS uses VP, cannot use it for main serial rx
              settings->altpin0 = false;
              if (settings->voice != VOICE_OFF || settings->strobe != STROBE_OFF)
                  settings->ppswire = false;
              if (settings->sd_card == SD_CARD_13_25) {
                  settings->ppswire = false;
              }
          }
          if (settings->gnss_pins == EXT_GNSS_15_14) {
              // pin 25 is used for GNSS on v0.7 (rather than 15)
              settings->voice = VOICE_OFF;
              settings->strobe = STROBE_OFF;
              //if (settings->sd_card == SD_CARD_13_VP) {    // now uses 4 instead of VP
              //    settings->ppswire = false;
              //}
              if (settings->sd_card == SD_CARD_13_25) {
                  settings->sd_card = SD_CARD_NONE;
                  settings->gnss_pins = EXT_GNSS_NONE;  // don't know what's wired
                  settings->ppswire = false;
              }
          }
          if (settings->gnss_pins == EXT_GNSS_13_2) {
              if (settings->sd_card == SD_CARD_13_25 || settings->sd_card == SD_CARD_13_VP) {
                  settings->sd_card = SD_CARD_NONE;
                  settings->gnss_pins = EXT_GNSS_NONE;  // don't know what's wired
              }
              settings->ppswire = false;
          }
          if (settings->sd_card == SD_CARD_13_VP) {    // now uses 4 instead of VP
              if (settings->gnss_pins == EXT_GNSS_39_4 || settings->gnss_pins == EXT_GNSS_13_2) {
                  settings->sd_card = SD_CARD_NONE;
                  settings->gnss_pins = EXT_GNSS_NONE;
              }
              //if (settings->gnss_pins != EXT_GNSS_NONE)
              //    settings->ppswire = false;
          }
          if (settings->ppswire && settings->gnss_pins == EXT_GNSS_15_14)
              settings->altpin0 = false;
          //if (settings->gnss_pins == EXT_GNSS_39_4)   // already done above
          //    settings->altpin0 = false;
          if (settings->rx1090 != ADSB_RX_NONE)
              settings->altpin0 = false;
      } else {    // T-Beam v1.x
          //if (settings->gnss_pins == EXT_GNSS_NONE || settings->sd_card == SD_CARD_13_VP)
          if (settings->gnss_pins == EXT_GNSS_NONE)
              settings->ppswire = false;
          if (settings->ppswire)
              settings->altpin0 = false;
          if (settings->gnss_pins == EXT_GNSS_13_2) {
              if (settings->sd_card == SD_CARD_13_25 || settings->sd_card == SD_CARD_13_VP) {
                  settings->sd_card = SD_CARD_NONE;
                  settings->gnss_pins = EXT_GNSS_NONE;  // don't know what's wired
              }
          }
      }
      if (settings->gnss_pins == EXT_GNSS_39_4) {
          settings->baudrate2 = BAUD_DEFAULT;       // meaning disabled
          settings->rx1090    = ADSB_RX_NONE;
      }
      if (settings->gnss_pins == EXT_GNSS_15_14) {
          settings->volume = BUZZER_OFF;
          if (settings->voice == VOICE_EXT)
              settings->voice = VOICE_OFF;
      }
      if (settings->sd_card == SD_CARD_13_25) {
          settings->voice = VOICE_OFF;
          settings->strobe = STROBE_OFF;
      }
      if (settings->rx1090 != ADSB_RX_NONE) {
          // dedicate Serial2 to the ADS-B receiver module
          settings->baudrate2 = BAUD_DEFAULT;       // will actually use 921600
          settings->invert2 = false;
          if (settings->nmea_out  == DEST_UART2)
              settings->nmea_out   = DEST_NONE;
          if (settings->nmea_out2 == DEST_UART2)
              settings->nmea_out2  = DEST_NONE;
          if (settings->gdl90     == DEST_UART2)
              settings->gdl90      = DEST_NONE;
          if (settings->gdl90_in  == DEST_UART2)
              settings->gdl90_in   = DEST_NONE;
          if (settings->d1090     == DEST_UART2)
              settings->d1090      = DEST_NONE;
      }
      if (settings->voice == VOICE_EXT) {
          settings->volume = BUZZER_OFF;  // free up pins 14 & 15 for I2S use
      }
  } else {
      settings->voice = VOICE_OFF;
  }

  // if SSID but no password, copy ssid to myssid, it is for AP mode
  if (settings->ssid[0] != '\0' && settings->psk[0] == '\0')
      strcpy(settings->myssid, settings->ssid);

#endif /* ESP32 */

  /* if winch mode, use full transmission power */
  if (settings->acft_type == AIRCRAFT_TYPE_WINCH && settings->txpower == RF_TX_POWER_LOW)
      settings->txpower == RF_TX_POWER_FULL;

  // min and max values for some settings (type INT1 only)
  for (int i=0; i<NUM_MINMAX; i++) {
     int idx = stgminmax[i].index;
     if (stgdesc[idx].type == STG_INT1) {
       int8_t *stg = (int8_t *)stgdesc[idx].value;
       if (*stg < stgminmax[i].min)  *stg = stgminmax[i].min;
       if (*stg > stgminmax[i].max)  *stg = stgminmax[i].max;
     }
  }
}


// start reading from the first byte (address 0) of the EEPROM
eeprom_t eeprom_block;

void Settings_setup()
{
#if defined(ABANDON_EEPROM)
  settings = &settings_stored;    // not same as settingb
  init_stgdesc();

  // start with defaults, then overwrite from file or EEPROM
  Settings_defaults(false);

  if (load_settings_from_file()) {
      //Serial.println(F("Current settings:"));
      //show_settings_serial();
      return;
  }
#endif

  // settings file not found, read the old EEPROM block

  bool keepsome = false;  // whether to save some settings from the previous version

  int cmd = EEPROM_EXT_LOAD;

  if (!SoC->EEPROM_begin(sizeof(eeprom_t)))
  {
    Serial.print(F("ERROR: Failed to initialize "));
    Serial.print(sizeof(eeprom_t));
    Serial.println(F(" bytes of EEPROM!"));
    Serial.flush();
    delay(1000000);
  }

  for (int i=0; i<sizeof(eeprom_t); i++) {
    eeprom_block.raw[i] = EEPROM.read(i);
  }

  settingb = &eeprom_block.field.settings;
#if !defined(ABANDON_EEPROM)
  settings = settingb;
#endif

//Serial.print("sizeof(eeprom_t): ");
//Serial.println(sizeof(eeprom_t));
//Serial.print("sizeof(eeprom_block.field.settings): ");
//Serial.println(sizeof(eeprom_block.field.settings));

  if (eeprom_block.field.magic != SOFTRF_EEPROM_MAGIC) {
    Serial.println(F("WARNING! EEPROM magic wrong. Loading defaults..."));

    Settings_defaults(false);
    cmd = EEPROM_EXT_DEFAULTS;
  } else {
    Serial.print(F("EEPROM version: "));
    Serial.println(eeprom_block.field.version, HEX);

    if (eeprom_block.field.version  != SOFTRF_EEPROM_VERSION
    ||  settingb->ssid[sizeof(settingb->ssid)-1] != '\0'
    ||  settingb->psk[sizeof(settingb->psk)-1] != '\0'
    ||  eeprom_block.field.version2 != SOFTRF_EEPROM_VERSION) {
      Serial.println(F("WARNING! EEPROM version wrong. Loading defaults..."));

        keepsome = (eeprom_block.field.version  == SOFTRF_EEPROM_VERSION - 1
                 && eeprom_block.field.version2 == SOFTRF_EEPROM_VERSION - 1);
        // keep some settings from the previous version

#if !defined(ABANDON_EEPROM)
        Settings_defaults(keepsome);
// #else defaults were loaded above, and none kept from wrong-version EEPROM
#endif
        cmd = EEPROM_EXT_DEFAULTS;

    } else {
        Serial.println(F("Loaded existing user settings from EEPROM block"));
        settings_used = STG_EEPROM;
    }
  }

  SoC->EEPROM_extension(cmd);
  // now all fields in settingb & ui are filled in, can copy into settings

#if defined(ABANDON_EEPROM)
  if (settings_used == STG_EEPROM) {  // not defaults
      copy_settingb();      // copy from packed-bits settingb to unpacked settings
      Serial.println(F("Settings copied from EEPROM"));
  }
  //show_settings_serial();
  Adjust_Settings();
  save_settings_to_file();  // save to a file, next boot will read from the file
#else
  Adjust_Settings();
  show_settings_serial();
  // in case re-writing to EEPROM:
  eeprom_block.field.magic    = SOFTRF_EEPROM_MAGIC;
  eeprom_block.field.version  = SOFTRF_EEPROM_VERSION;
  eeprom_block.field.version2 = SOFTRF_EEPROM_VERSION;
#endif
}

void Settings_defaults(bool keepsome)
{
  settings_used = STG_DEFAULT;

  if (keepsome == false) {    // the following may be kept from previous version

    settings->mode          = SOFTRF_MODE_NORMAL;
    settings->rf_protocol   = hw_info.model == SOFTRF_MODEL_BRACELET ?
                                RF_PROTOCOL_FANET : RF_PROTOCOL_LATEST;
#if defined(DEFAULT_REGION_US)
    settings->band        = RF_BAND_US;
#else
    settings->band        = RF_BAND_EU;
#endif
    settings->acft_type   = hw_info.model == SOFTRF_MODEL_BRACELET ?
                                                AIRCRAFT_TYPE_STATIC :
                                                AIRCRAFT_TYPE_GLIDER;
    settings->id_method   = ADDR_TYPE_FLARM;
    settings->aircraft_id = 0;
    settings->txpower     = RF_TX_POWER_FULL;

#if defined(USBD_USE_CDC) && !defined(DISABLE_GENERIC_SERIALUSB)
    settings->nmea_out    = DEST_USB;
    settings->nmea_out2   = DEST_NONE;
#else
    settings->nmea_out    = hw_info.model == SOFTRF_MODEL_BADGE ?
                                             DEST_BLUETOOTH :
                                          (hw_info.model == SOFTRF_MODEL_PRIME ?
                                             DEST_UDP :
                                          (hw_info.model == SOFTRF_MODEL_PRIME_MK2 ?
                                             DEST_UDP :
                                           DEST_UART));
    settings->nmea_out2   = hw_info.model == SOFTRF_MODEL_BADGE ?
                                             DEST_USB :
                                          (hw_info.model == SOFTRF_MODEL_PRIME ?
                                             DEST_UART :
                                          (hw_info.model == SOFTRF_MODEL_PRIME_MK2 ?
                                             DEST_UART :
                                           DEST_NONE));
#endif

    settings->nmea_g  = true;
    settings->nmea_p  = false;
    settings->nmea_l  = true;
    settings->nmea_s  = true;
    settings->nmea_d  = false;
    settings->nmea_e  = false;

    settings->nmea2_g = true;
    settings->nmea2_p = false;
    settings->nmea2_l = true;
    settings->nmea2_s = true;
    settings->nmea2_d = false;
    settings->nmea2_e = false;

#if defined(ARDUINO_ARCH_NRF52)
    settings->bluetooth  = BLUETOOTH_LE_HM10_SERIAL;
#else
    settings->bluetooth  = BLUETOOTH_OFF;
#endif
    settings->alarm      = TRAFFIC_ALARM_LEGACY;
    settings->stealth    = false;
    settings->no_track   = false;

    settings->baud_rate  = BAUD_DEFAULT;      // Serial  - meaning 38400
    settings->baudrate2  = BAUD_DEFAULT;      // Serial2 - meaning disabled
    settings->invert2    = false;
    settings->freq_corr  = 0;

    if (hw_info.model == SOFTRF_MODEL_STANDALONE
     || hw_info.model == SOFTRF_MODEL_PRIME) {
      //settings->volume  = BUZZER_OFF;
      settings->strobe  = STROBE_OFF;
      settings->pointer = DIRECTION_NORTH_UP;
    } else if (hw_info.model == SOFTRF_MODEL_PRIME_MK2) {
      //settings->volume  = BUZZER_VOLUME_FULL;
      settings->strobe  = STROBE_OFF;
      settings->pointer = LED_OFF;
    } else {
      //settings->volume  = BUZZER_OFF;
      settings->strobe  = STROBE_OFF;
      settings->pointer = LED_OFF;
    }
    settings->voice = VOICE_OFF;

    settings->ignore_id = 0;
    settings->follow_id = 0;

    settings->tcpmode = TCP_MODE_SERVER;
    strncpy(settings->host_ip, NMEA_TCP_IP, sizeof(settings->host_ip)-1);
    settings->host_ip[sizeof(settings->host_ip)-1] = '\0';
    settings->tcpport = 0;   // 2000
    settings->alt_udp    = false;

    settings->gdl90_in   = DEST_NONE;
    settings->gdl90      = DEST_NONE;
#if !defined(EXCLUDE_D1090)
    settings->d1090      = DEST_NONE;
#endif

    settings->logalarms  = false;
  }
  // otherwise keep those settings from the previous version

  // move volume back above next time
    if (hw_info.model == SOFTRF_MODEL_STANDALONE
     || hw_info.model == SOFTRF_MODEL_PRIME) {
      settings->volume  = BUZZER_OFF;
    } else if (hw_info.model == SOFTRF_MODEL_PRIME_MK2) {
      settings->volume  = BUZZER_VOLUME_FULL;
    } else {
      settings->volume  = BUZZER_OFF;
    }

    settings->gnss_pins = EXT_GNSS_NONE;   // whether an external GNSS module was added to a T-Beam
    settings->ppswire   = false;       // whether T-Beam v0.7 or external GNSS has PPS wire connected
    settings->sd_card   = SD_CARD_NONE;
    settings->logflight = FLIGHT_LOG_NONE;
    settings->loginterval = LOG_INTERVAL_4S;
    settings->rx1090    = ADSB_RX_NONE;
    settings->mode_s    = false;
    settings->geoid     = 0;

    settings->myssid[0] = '\0';
    settings->myssid[sizeof(settings->ssid)-1] = '\0';
    settings->ssid[0] = '\0';   // default is empty string - speeds up booting
    settings->ssid[sizeof(settings->ssid)-1] = '\0';
    settings->psk[0] = '\0';
    settings->psk[sizeof(settings->psk)-1] = '\0';

  // the settings below get reset:

  settings->relay = RELAY_OFF;   // >>> revert to RELAY_LANDED as default eventually

  settings->json        = JSON_OFF;
  settings->power_save  = (hw_info.model == SOFTRF_MODEL_BRACELET ? POWER_SAVE_NORECEIVE : POWER_SAVE_NONE);
  settings->power_ext   = 0;
  settings->altpin0     = false;
  settings->debug_flags = 0;      // if and when debug output will be turned on - 0x3F for all

  settings->igc_key[0] = 0;
  settings->igc_key[1] = 0;
  settings->igc_key[2] = 0;
  settings->igc_key[3] = 0;

//#if defined(USE_EPAPER)
#if defined(DEFAULT_REGION_US)
  settings->units       = UNITS_IMPERIAL;
#else
  settings->units       = UNITS_METRIC;
#endif
  settings->zoom        = ZOOM_MEDIUM;
  settings->rotate      = ROTATE_0;
  settings->orientation = DIRECTION_TRACK_UP;
  settings->adb         = DB_NONE;
  settings->epdidpref   = ID_TYPE;
  settings->viewmode    = VIEW_MODE_STATUS;
  settings->antighost   = ANTI_GHOSTING_AUTO;
  settings->team        = 0;
//#endif

#if defined(ABANDON_EEPROM)
  // new settings not in EEPROM
  settings->version = SOFTRF_SETTINGS_VERSION;
  settings->rx1090x     = 100;
  settings->hrange      = 27;   // km
  settings->vrange      = 10;   // 1000m
  settings->hrange1090  = 27;   // km
  settings->vrange1090  = 20;   // 2000m
  strcpy(settings->igc_pilot, "Chuck Yeager");
  strcpy(settings->igc_type,  "ASW20");
  strcpy(settings->igc_reg,   "N1234");
  strcpy(settings->igc_cs,    "XXX");
#endif
}

void EEPROM_store()
{
  Serial.println("Writing EEPROM...");

  for (int i=0; i<sizeof(eeprom_t); i++) {
    EEPROM.write(i, eeprom_block.raw[i]);
  }

  SoC->EEPROM_extension(EEPROM_EXT_STORE);

  EEPROM_commit();
}

#if defined(FILESYS)

bool format_setting(const int i, const bool comment)
{
    int t = stgdesc[i].type;
    if (t == STG_VOID)
        return false;
    char *v = stgdesc[i].value;
    const char *w = stgdesc[i].label;
    int n = sizeof(NMEABuffer);
    switch (t) {
    case STG_INT1:
       snprintf(NMEABuffer,n,"%s,%d\r\n", w, (int)(*(int8_t*)v));
       break;
    case STG_UINT1:
       snprintf(NMEABuffer,n,"%s,%d\r\n", w, (int)(*(uint8_t*)v));
       break;
    case STG_HEX2:
       snprintf(NMEABuffer,n,"%s,%02X\r\n", w, (int)(*(uint8_t*)v));
       break;
    case STG_HEX6:
       snprintf(NMEABuffer,n,"%s,%06X\r\n", w, (int)(*(uint32_t*)v));
       break;
    default:
       snprintf(NMEABuffer,n,"%s,%s\r\n", w, (t > STG_VOID)? v : "?");
       break;
    }
    if (comment && stgcomment[i] != NULL) {
        int len = strlen(NMEABuffer) - 2;   // clobber the \r\n
        while (len < 18)
            NMEABuffer[len++] = ' ';
        snprintf(NMEABuffer+len,n-len,"%s%s\r\n", " # ", stgcomment[i]);
    }
    return true;
}

void show_settings_serial()
{
  for (int i=STG_MODE; i<STG_END; i++) {
     if (format_setting(i, true) == false)
         continue;
     Serial.print(NMEABuffer);
  }

#if defined(USE_OGN_ENCRYPTION)
    if (settings->rf_protocol == RF_PROTOCOL_OGNTP) {
        Serial.print("IGC key");
        Serial.printf(" %08X", (settings->igc_key[0]? 0x88888888 : 0));
        Serial.printf(" %08X", (settings->igc_key[1]? 0x88888888 : 0));
        Serial.printf(" %08X", (settings->igc_key[2]? 0x88888888 : 0));
        Serial.printf(" %08X\r\n", (settings->igc_key[3]? 0x88888888 : 0));
    }
#endif
}

void save_settings_to_file()
{
#if !defined(ABANDON_EEPROM)
  EEPROM_store();
  return;
#endif
  if (! FS_is_mounted) {
      Serial.println(F("File system is not mounted"));
      return;
  }
  Serial.println(F("Saving settings to settings.txt ..."));
  if (FILESYS.exists("/settings.txt"))
      FILESYS.remove("/settings.txt");
  File SettingsFile = FILESYS.open("/settings.txt", FILE_WRITE);
  if (!SettingsFile) {
      Serial.println(F("Failed to open settings.txt"));
      return;
  }
  snprintf(NMEABuffer,sizeof(NMEABuffer),"# originator: model %d ID %06X\r\n",
                               hw_info.model, SoC->getChipId());
  Serial.print(NMEABuffer);
  SettingsFile.write((const uint8_t *)NMEABuffer, strlen(NMEABuffer));
  settings->version = SOFTRF_SETTINGS_VERSION;
  bool write_error = false;
  for (int i=STG_VERSION; i<STG_END; i++) {
       if (format_setting(i, true) == false)
           continue;
       int len = strlen(NMEABuffer);
       if (SettingsFile.write((const uint8_t *)NMEABuffer, len) == len) {
           Serial.print(NMEABuffer);
       } else {
           Serial.println(F("Error writing to settings.txt"));
           write_error = true;
           break;
       }
       yield();
  }
  SettingsFile.close();
  if (write_error)
      FILESYS.remove("/settings.txt");
  else
      Serial.println(F("... OK"));
}

int find_setting(const char *p)
{
    for (int i=STG_VERSION; i<STG_END; i++) {
         if (strcmp(p,stgdesc[i].label)==0)
            return i;
    }
    return STG_NONE;
}

bool load_setting(const int idx, const char *q)
{
    int t = stgdesc[idx].type;
    if (t == STG_VOID) {
        Serial.print(F(" - ignored on this platform"));
        return true;
    }
    if (t < STG_VOID && *q == '\0')    // empty value and not a string
            return false; 
    char *v = stgdesc[idx].value;
    switch(t) {
    case STG_INT1:
        *(int8_t *)v = (int8_t) atoi(q);
        break;
    case STG_UINT1:
        *(uint8_t *)v = (uint8_t) atoi(q);
        break;
    case STG_HEX2:
        *(uint8_t *)v = (uint8_t) strtol(q,NULL,16);
        break;
    case STG_HEX6:
        *(uint32_t *)v = (uint32_t) strtol(q,NULL,16);
        break;
    default:
        if (t < STG_VOID)
            return false;
        strncpy(v, q, t);
        v[t-1] = '\0';
        break;
    }
    return true;
}

static bool interpretSetting()
{
    char *p = NMEABuffer;
    char *q = p;
    while (*q != ',') {
        q++;
        if (*q == '\0')
            return false;
    }
    *q = '\0';  // overwrites the comma
    ++q;

    int i = find_setting(p);
    if (i == STG_NONE)
        return false;

    bool is_numerical = (stgdesc[i].type < STG_VOID);

    char *r = q;
    char *s = NULL;
    while (1) {
        if (*r == '\0') {     // end of the line
            if (s)
                *s = '\0';    // drop trailing spaces
            break;
        } else if (*r == ' ') {
            if (is_numerical) {    // for settings that are numbers
                *r = '\0';         // treat anything after first space as a comment
                break;
            }
            if (s == NULL)
                s = r;  // first space
        } else if (*r=='#' || *r=='*' || *r==';' || *r=='/') {
            if (s)
                *s = '\0';    // end the string at the first space
            else
                *r = '\0';    // comment started without a space
            break;
        } else {  // not a space and not one of the chars signaling a comment
            s = NULL;
            // consider the previous space and this non-space part of the string value
        }
        r++;
    }

    return load_setting(i,q);
}

bool load_settings_from_file()
{
    if (! FS_is_mounted) {
        Serial.println(F("File system is not mounted"));
        return false;
    }
    if (! FILESYS.exists("/settings.txt")) {
        Serial.println(F("File settings.txt does not exist"));
        return false;
    }
    File SettingsFile = FILESYS.open("/settings.txt", FILE_READ);
    if (!SettingsFile) {
        Serial.println(F("Failed to open settings.txt"));
        return false;
    }
    Settings_defaults(false);    // defaults used for any setting not mentioned in the file
    int limit = 200;
    Serial.println(F("Loading settings from file..."));
    //settings->version = 0;
    int nsettings = 0;
    while (getline(SettingsFile, NMEABuffer, sizeof(NMEABuffer)) && --limit>0) {
        Serial.println(NMEABuffer);
        // allow blank or comment lines
        if (NMEABuffer[0] == '#')  continue;
        if (NMEABuffer[0] == '*')  continue;
        if (NMEABuffer[0] == ';')  continue;
        if (NMEABuffer[0] == '/')  continue;
        if (NMEABuffer[0] == ' ')  continue;
        if (NMEABuffer[0] == '\0')  continue;
        if (interpretSetting()) {
            //Serial.println("  - loaded OK");
        } else {
            SettingsFile.close();
            Serial.println(F("  - invalid setting label, erased file"));
            FILESYS.remove("/settings.txt");
            Settings_defaults(false);
            return false;
        }
        ++nsettings;
        yield();
    }
    SettingsFile.close();
    if (settings->version != SOFTRF_SETTINGS_VERSION) {
        // all labels valid, but version number was wrong or missing
        Serial.println(F("bad settings.txt version, erased file"));
        FILESYS.remove("/settings.txt");
        Settings_defaults(false);
        return false;
    }
    Serial.print("... Loaded ");
    Serial.print(nsettings);
    Serial.println(F(" settings from file"));
    settings_used = STG_FILE;    // may be a mix of defaults and values from file
    Adjust_Settings();
    return true;
}

#endif /* FILESYS */

#endif /* ! EXCLUDE_EEPROM */
