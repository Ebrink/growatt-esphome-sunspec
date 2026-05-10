#pragma once

/**
 * components/sunspec_server/sunspec_server.h
 *
 * SunSpec Modbus TCP server — Growatt 3600 TL-X (single-phase).
 * Optimised for ESP8266 (ESP-07): no FPU, ~80 KB heap, single-core 80 MHz.
 *
 * Models implemented:
 *   Model   1  Common           (DID=1,   L=65)
 *   Model 101  Single-phase AC  (DID=101, L=50)
 *   Model 160  Multiple MPPT    (DID=160, L=48, 2 modules)  ← dual PV strings
 *
 * Optimisations:
 *   • Static image is a class member (BSS) — allocated once, never on stack.
 *   • All constant bytes written once in setup(); only live-data words updated
 *     per request (~30 writes vs rebuilding 174 bytes from scratch each time).
 *   • powf() / roundf() eliminated — scale factors -2/-1/0 use ×100/×10/×1.
 *   • isnan() replaced with IEEE-754 bit test (no libm call).
 *   • PROGMEM for all string constants (Common Model body, module IDs).
 *   • RX/TX buffers sized to Modbus TCP maximums (260 bytes each).
 *   • flush() removed — redundant with setNoDelay(true).
 *   • bool flag avoids client_.connected() syscall on idle loops.
 *   • Hot-path functions pinned to IRAM with ICACHE_RAM_ATTR.
 *
 * ── SunSpec register map ────────────────────────────────────────────────────
 * Wire addresses are 0-based.  SunSpec register numbers are 1-based.
 * wire_address = sunspec_register_number − 1
 *
 *  Wire addr    SunSpec reg   Content
 *  ──────────   ───────────   ──────────────────────────────────────────────
 *  40000-40001  40001-40002   "SunS" magic  (0x5375 / 0x6E53)
 *  40002-40003  40003-40004   Common Model header  DID=1,   L=65
 *  40004-40068  40005-40069   Common block body (Mn,Md,Opt,Vr,SN,DA,pad)
 *  40069-40070  40070-40071   Inverter Model 101 header  DID=101, L=50
 *  40071-40120  40072-40121   Inverter Model 101 body
 *  40121-40122  40122-40123   Model 160 header  DID=160, L=48
 *  40123-40130  40124-40131   Model 160 fixed header body (8 regs)
 *  40131-40150  40132-40151   Model 160 module 1 body — PV1 (20 regs)
 *  40151-40170  40152-40171   Model 160 module 2 body — PV2 (20 regs)
 *  40171-40172  40172-40173   End Model  DID=0xFFFF, L=0
 *
 * ── Model 101 body (wire base = 40071) ──────────────────────────────────────
 *  Byte  Δreg  Field               Type   SF   Source
 *   0    +0    I_AC_Current        u16    -1   ac_current
 *   2    +1    I_AC_CurrentA       u16    -1   ac_current  (= total, 1-phase)
 *   4    +2    I_AC_CurrentB       u16    -1   NaN  (static)
 *   6    +3    I_AC_CurrentC       u16    -1   NaN  (static)
 *   8    +4    I_AC_Current_SF     i16         -1   (static)
 *  10    +5    I_AC_Voltage_AB     u16    -1   NaN  (static, no L-L on 1-ph)
 *  12    +6    I_AC_Voltage_BC     u16    -1   NaN  (static)
 *  14    +7    I_AC_Voltage_CA     u16    -1   NaN  (static)
 *  16    +8    I_AC_Voltage_AN     u16    -1   ac_voltage  (L-N)
 *  18    +9    I_AC_Voltage_BN     u16    -1   NaN  (static)
 *  20    +10   I_AC_Voltage_CN     u16    -1   NaN  (static)
 *  22    +11   I_AC_Voltage_SF     i16         -1   (static)
 *  24    +12   I_AC_Power          i16     0   ac_power
 *  26    +13   I_AC_Power_SF       i16          0   (static)
 *  28    +14   I_AC_Frequency      u16    -2   grid_frequency
 *  30    +15   I_AC_Frequency_SF   i16         -2   (static)
 *  32    +16   I_AC_VA             i16     0   ac_power (≈ real, unity PF)
 *  34    +17   I_AC_VA_SF          i16          0   (static)
 *  36    +18   I_AC_VAR            i16     0   NaN  (static)
 *  38    +19   I_AC_VAR_SF         i16          0   (static)
 *  40    +20   I_AC_PF             i16    -1   NaN  (static)
 *  42    +21   I_AC_PF_SF          i16         -1   (static)
 *  44    +22   I_AC_Energy_WH      u32     0   total_energy × 1000 (kWh→Wh)
 *  48    +24   I_AC_Energy_WH_SF   i16          0   (static)
 *  50    +25   I_DC_Current        u16    -2   pv1_current + pv2_current (total DC)
 *  52    +26   I_DC_Current_SF     i16         -2   (static)
 *  54    +27   I_DC_Voltage        u16    -1   pv1_voltage (primary string)
 *  56    +28   I_DC_Voltage_SF     i16         -1   (static)
 *  58    +29   I_DC_Power          i16     0   pv1_power + pv2_power (total DC)
 *  60    +30   I_DC_Power_SF       i16          0   (static)
 *  62    +31   I_Temp_Cab          i16    -1   inverter_temp
 *  64    +32   I_Temp_Sink         i16    -1   inverter_temp (same sensor)
 *  66    +33   I_Temp_Trns         i16    -1   NaN  (static)
 *  68    +34   I_Temp_Other        i16    -1   NaN  (static)
 *  70    +35   I_Temp_SF           i16         -1   (static)
 *  72    +36   I_Status            u16         Growatt → SunSpec state
 *  74    +37   I_Status_Vendor     u16         Raw Growatt status code
 *  76    +38   I_Event_1           u32          0   (static)
 *  80    +40   WMaxLimPct          u16    -2   wmax_lim_pct_ × 100  [RW]
 *  82    +41   WMaxLim_Ena         u16         wmax_lim_ena_         [RW]
 *  84    +42   WMaxLimPct_SF       i16         -2   (static)
 *  86-99       (reserved, zero)                     (static)
 *
 * Writable via FC16:
 *   wire 40111  WMaxLimPct  → wmax_lim_pct_ + power_limit_number_->make_call()
 *   wire 40112  WMaxLim_Ena → wmax_lim_ena_
 *
 * ── Model 160 fixed header body (wire base = 40123, 8 regs) ─────────────────
 *  Byte  Δreg  Field               Type   SF   Value
 *   0    +0    DcCurrent_SF        i16         -2   (static)
 *   2    +1    DcVoltage_SF        i16         -1   (static)
 *   4    +2    DcPower_SF          i16          0   (static)
 *   6    +3    DcEnergy_SF         i16          0   (static)
 *   8    +4    N_Modules           u16          2   (static)
 *   10   +5    Timestamp           u32          0   (static, no RTC)
 *   14   +7    GlobalAlrm          u16          0   (static)
 *
 * ── Model 160 per-module body (20 regs each) ────────────────────────────────
 *  Byte  Δreg  Field               Type   SF   Module 1 (PV1) / Module 2 (PV2)
 *   0    +0    Input_ID            u16         1 / 2   (static)
 *   2    +1    DcCurrent           u16    -2   pv1_current / pv2_current
 *   4    +2    DcVoltage           u16    -1   pv1_voltage / pv2_voltage
 *   6    +3    DcPower             i16     0   pv1_power   / pv2_power
 *   8    +4    DcEnergy            u32     0   NaN (not available per-string)
 *  12    +6    Timestamp           u32     0   0   (static)
 *  16    +8    Tmp                 i16    -1   NaN (static, shared temp in M101)
 *  18    +9    DcCurr_SF  [pad]    u16         0 (pad, SF already in header)
 *  20-38       (reserved, zero)                    (static)
 */

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/number/number.h"
#include <ESP8266WiFi.h>
#include <cstring>

namespace esphome {
namespace sunspec_server {

static const char *const TAG = "sunspec";

// ── Address layout (0-based wire addresses) ──────────────────────────────────
static const uint16_t SS_BASE    = 40000u;
static const uint16_t INV_BASE   = 40069u;   // Model 101 header
static const uint16_t M160_BASE  = 40121u;   // Model 160 header
static const uint16_t END_BASE   = 40171u;   // End Model
static const uint16_t REG_COUNT  = 174u;     // 40000 … 40173 inclusive
static const uint16_t IMG_BYTES  = REG_COUNT * 2u;  // 348 bytes

// Model 160 sub-offsets from M160_BASE (in registers, 0-based)
//   +0..+1  header (DID, L)
//   +2..+9  fixed header body  (8 regs)
//   +10..+29 module 1 body     (20 regs)
//   +30..+49 module 2 body     (20 regs)
static const uint16_t M160_HDR_BODY = M160_BASE + 2u;   // wire 40123
static const uint16_t M160_MOD1     = M160_BASE + 10u;  // wire 40131
static const uint16_t M160_MOD2     = M160_BASE + 30u;  // wire 40151

// Writable Model 101 control registers (wire addresses)
static const uint16_t WMAX_PCT_ADDR = INV_BASE + 2u + 40u;  // wire 40111
static const uint16_t WMAX_ENA_ADDR = WMAX_PCT_ADDR + 1u;   // wire 40112

// ── Modbus TCP buffer sizes ───────────────────────────────────────────────────
static const int RX_BUF = 260;
static const int TX_BUF = 260;

// ── NaN sentinels ─────────────────────────────────────────────────────────────
static const uint16_t U16_NAN = 0xFFFFu;
static const uint16_t I16_NAN = 0x8000u;

// ── PROGMEM constants ─────────────────────────────────────────────────────────
static const char PM_MN[] PROGMEM = "Growatt";
static const char PM_MD[] PROGMEM = "3600TL-X";
static const char PM_VR[] PROGMEM = "ESPHome";
static const char PM_SN[] PROGMEM = "000000000000";

// ── Big-endian write helpers ──────────────────────────────────────────────────
static ICACHE_RAM_ATTR inline void p16(uint8_t *p, uint16_t v) {
    p[0] = v >> 8;
    p[1] = v & 0xFFu;
}
static ICACHE_RAM_ATTR inline void p32(uint8_t *p, uint32_t v) {
    p[0] = v >> 24;
    p[1] = (v >> 16) & 0xFFu;
    p[2] = (v >>  8) & 0xFFu;
    p[3] =  v        & 0xFFu;
}

// ── Fast isnan (IEEE-754 bit test, no libm) ───────────────────────────────────
static inline bool f_isnan(float v) {
    union { float f; uint32_t u; } t;
    t.f = v;
    return (t.u & 0x7FFFFFFFu) > 0x7F800000u;
}

// ── Scaling helpers (no powf / roundf) ───────────────────────────────────────
static inline uint16_t u16_sf1(float v) {          // SF=-1, raw = v×10
    if (f_isnan(v) || v < 0.0f) return U16_NAN;
    uint32_t r = (uint32_t)(v * 10.0f + 0.5f);
    return (r > 65534u) ? 65534u : (uint16_t)r;
}
static inline uint16_t i16_sf1(float v) {
    if (f_isnan(v)) return I16_NAN;
    int32_t r = (int32_t)(v * 10.0f + (v >= 0.0f ? 0.5f : -0.5f));
    if (r >  32767) r =  32767;
    if (r < -32768) r = -32768;
    return (uint16_t)(int16_t)r;
}
static inline uint16_t u16_sf2(float v) {          // SF=-2, raw = v×100
    if (f_isnan(v) || v < 0.0f) return U16_NAN;
    uint32_t r = (uint32_t)(v * 100.0f + 0.5f);
    return (r > 65534u) ? 65534u : (uint16_t)r;
}
static inline uint16_t i16_sf0(float v) {          // SF=0, raw = v (integer)
    if (f_isnan(v)) return I16_NAN;
    int32_t r = (int32_t)(v + (v >= 0.0f ? 0.5f : -0.5f));
    if (r >  32767) r =  32767;
    if (r < -32768) r = -32768;
    return (uint16_t)(int16_t)r;
}

// ── Growatt status → SunSpec I_Status ────────────────────────────────────────
// SunSpec: 1=OFF 2=SLEEPING 3=STARTING 4=MPPT 5=THROTTLED
//          6=SHUTTING_DOWN 7=FAULT 8=STANDBY
static const uint8_t STATUS_MAP[13] PROGMEM = {
    8, 4, 8, 7, 3, 4, 4, 4, 4, 4, 4, 5, 4
};
static inline uint16_t sunspec_status(float g) {
    int i = (int)g;
    if (i < 0 || i > 12) return 7u;
    return pgm_read_byte(&STATUS_MAP[i]);
}

// ── Safe sensor read ──────────────────────────────────────────────────────────
static inline float rs(sensor::Sensor *s) {
    return (s != nullptr) ? s->state : NAN;
}

// ── PROGMEM string → register image ──────────────────────────────────────────
static void put_str_P(uint8_t *p, const char *s_P, int regs) {
    int bytes = regs * 2;
    memset(p, 0, bytes);
    strncpy_P((char *)p, s_P, bytes);
}

// ─────────────────────────────────────────────────────────────────────────────
class SunSpecServer : public Component {
 public:
    explicit SunSpecServer(uint16_t port) : port_(port), server_(port) {}

    // ── Sensor setters ────────────────────────────────────────────────────────
    void set_ac_current    (sensor::Sensor *s) { ac_current_     = s; }
    void set_ac_voltage    (sensor::Sensor *s) { ac_voltage_     = s; }
    void set_ac_power      (sensor::Sensor *s) { ac_power_       = s; }
    void set_grid_frequency(sensor::Sensor *s) { grid_frequency_ = s; }
    void set_pv1_current   (sensor::Sensor *s) { pv1_current_    = s; }
    void set_pv2_current   (sensor::Sensor *s) { pv2_current_    = s; }
    void set_pv1_voltage   (sensor::Sensor *s) { pv1_voltage_    = s; }
    void set_pv2_voltage   (sensor::Sensor *s) { pv2_voltage_    = s; }
    void set_pv1_power     (sensor::Sensor *s) { pv1_power_      = s; }
    void set_pv2_power     (sensor::Sensor *s) { pv2_power_      = s; }
    void set_total_energy  (sensor::Sensor *s) { total_energy_   = s; }
    void set_inverter_temp (sensor::Sensor *s) { inverter_temp_  = s; }
    void set_status_code   (sensor::Sensor *s) { status_code_    = s; }

    void set_power_limit_number(number::Number *n) { power_limit_number_ = n; }

    float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

    // ── setup(): write every static byte into img_ once ──────────────────────
    void setup() override {
        memset(img_, 0, IMG_BYTES);

        // ── "SunS" magic ──────────────────────────────────────────────────────
        p16(img_ + 0, 0x5375u);
        p16(img_ + 2, 0x6E53u);

        // ── Common Model header (wire 40002) ──────────────────────────────────
        uint8_t *cm = img_ + (40002u - SS_BASE) * 2u;
        p16(cm + 0, 1u);    // DID
        p16(cm + 2, 65u);   // L
        uint8_t *cb = cm + 4;
        put_str_P(cb +   0, PM_MN, 16);
        put_str_P(cb +  32, PM_MD, 16);
        put_str_P(cb +  80, PM_VR,  8);
        put_str_P(cb +  96, PM_SN, 16);
        p16(cb + 128, 1u);  // DA
        // pad already zero

        // ── Model 101 header (wire 40069) ─────────────────────────────────────
        uint8_t *ih = img_ + (INV_BASE - SS_BASE) * 2u;
        p16(ih + 0, 101u);
        p16(ih + 2,  50u);
        uint8_t *ib = ih + 4;  // body base (wire 40071)

        // Static fields in Model 101 body
        p16(ib +  4, U16_NAN);                    // I_AC_CurrentB
        p16(ib +  6, U16_NAN);                    // I_AC_CurrentC
        p16(ib +  8, (uint16_t)(int16_t)-1);      // I_AC_Current_SF
        p16(ib + 10, U16_NAN);                    // I_AC_Voltage_AB
        p16(ib + 12, U16_NAN);                    // I_AC_Voltage_BC
        p16(ib + 14, U16_NAN);                    // I_AC_Voltage_CA
        p16(ib + 18, U16_NAN);                    // I_AC_Voltage_BN
        p16(ib + 20, U16_NAN);                    // I_AC_Voltage_CN
        p16(ib + 22, (uint16_t)(int16_t)-1);      // I_AC_Voltage_SF
        p16(ib + 26, 0u);                         // I_AC_Power_SF
        p16(ib + 30, (uint16_t)(int16_t)-2);      // I_AC_Frequency_SF
        p16(ib + 34, 0u);                         // I_AC_VA_SF
        p16(ib + 36, I16_NAN);                    // I_AC_VAR
        p16(ib + 38, 0u);                         // I_AC_VAR_SF
        p16(ib + 40, I16_NAN);                    // I_AC_PF
        p16(ib + 42, (uint16_t)(int16_t)-1);      // I_AC_PF_SF
        p16(ib + 48, 0u);                         // I_AC_Energy_WH_SF
        p16(ib + 52, (uint16_t)(int16_t)-2);      // I_DC_Current_SF
        p16(ib + 56, (uint16_t)(int16_t)-1);      // I_DC_Voltage_SF
        p16(ib + 60, 0u);                         // I_DC_Power_SF
        p16(ib + 66, I16_NAN);                    // I_Temp_Trns
        p16(ib + 68, I16_NAN);                    // I_Temp_Other
        p16(ib + 70, (uint16_t)(int16_t)-1);      // I_Temp_SF
        p32(ib + 76, 0u);                         // I_Event_1
        p16(ib + 84, (uint16_t)(int16_t)-2);      // WMaxLimPct_SF

        // ── Model 160 header (wire 40121) ─────────────────────────────────────
        // L = 8 (fixed header) + 2 × 20 (modules) = 48
        uint8_t *mh = img_ + (M160_BASE - SS_BASE) * 2u;
        p16(mh + 0, 160u);   // DID
        p16(mh + 2,  48u);   // L

        // Model 160 fixed header body (wire 40123, byte base = mh+4)
        uint8_t *hb = img_ + (M160_HDR_BODY - SS_BASE) * 2u;
        p16(hb +  0, (uint16_t)(int16_t)-2);  // DcCurrent_SF = -2
        p16(hb +  2, (uint16_t)(int16_t)-1);  // DcVoltage_SF = -1
        p16(hb +  4, 0u);                     // DcPower_SF   =  0
        p16(hb +  6, 0u);                     // DcEnergy_SF  =  0
        p16(hb +  8, 2u);                     // N_Modules    =  2
        p32(hb + 10, 0u);                     // Timestamp    =  0 (no RTC)
        p16(hb + 14, 0u);                     // GlobalAlrm   =  0

        // Module 1 static fields (wire 40131, byte base = mod1)
        uint8_t *mod1 = img_ + (M160_MOD1 - SS_BASE) * 2u;
        p16(mod1 +  0, 1u);                   // Input_ID = 1 (PV string 1)
        p32(mod1 +  8, 0xFFFFFFFFu);          // DcEnergy = NaN (not per-string)
        p32(mod1 + 12, 0u);                   // Timestamp = 0
        p16(mod1 + 16, I16_NAN);              // Tmp = NaN (see M101 I_Temp)
        // bytes 18-39: reserved, remain zero

        // Module 2 static fields (wire 40151, byte base = mod2)
        uint8_t *mod2 = img_ + (M160_MOD2 - SS_BASE) * 2u;
        p16(mod2 +  0, 2u);                   // Input_ID = 2 (PV string 2)
        p32(mod2 +  8, 0xFFFFFFFFu);          // DcEnergy = NaN
        p32(mod2 + 12, 0u);                   // Timestamp = 0
        p16(mod2 + 16, I16_NAN);              // Tmp = NaN
        // bytes 18-39: reserved, remain zero

        // ── End Model (wire 40171) ────────────────────────────────────────────
        uint8_t *em = img_ + (END_BASE - SS_BASE) * 2u;
        p16(em + 0, 0xFFFFu);
        p16(em + 2, 0x0000u);

        server_.begin();
        server_.setNoDelay(true);
        ESP_LOGI(TAG, "SunSpec TCP on port %d — M101 + M160 (2 MPPT), %u regs",
                 port_, REG_COUNT);
    }

    // ── loop() ────────────────────────────────────────────────────────────────
    void loop() override {
        if (!connected_) {
            if (server_.hasClient()) {
                client_    = server_.accept();
                connected_ = true;
                buf_len_   = 0;
                client_.setNoDelay(true);
                ESP_LOGI(TAG, "Client: %s", client_.remoteIP().toString().c_str());
            }
            return;
        }

        if (!client_.connected()) {
            client_.stop();
            connected_ = false;
            ESP_LOGI(TAG, "Client disconnected");
            return;
        }

        int avail = client_.available();
        if (avail <= 0) return;

        if (buf_len_ + avail > RX_BUF) {
            ESP_LOGW(TAG, "RX overflow — dropping");
            client_.stop();
            connected_ = false;
            buf_len_   = 0;
            return;
        }
        buf_len_ += client_.read(rx_buf_ + buf_len_, avail);

        while (buf_len_ >= 8) {
            uint16_t mbap_len = ((uint16_t)rx_buf_[4] << 8) | rx_buf_[5];
            int frame_len = 6 + (int)mbap_len;
            if (buf_len_ < frame_len) break;

            int tx_len = handle_frame_(rx_buf_, frame_len);
            if (tx_len > 0)
                client_.write(tx_buf_, tx_len);

            buf_len_ -= frame_len;
            if (buf_len_ > 0)
                memmove(rx_buf_, rx_buf_ + frame_len, buf_len_);
        }
    }

 private:
    sensor::Sensor *ac_current_      = nullptr;
    sensor::Sensor *ac_voltage_      = nullptr;
    sensor::Sensor *ac_power_        = nullptr;
    sensor::Sensor *grid_frequency_  = nullptr;
    sensor::Sensor *pv1_current_     = nullptr;
    sensor::Sensor *pv2_current_     = nullptr;
    sensor::Sensor *pv1_voltage_     = nullptr;
    sensor::Sensor *pv2_voltage_     = nullptr;
    sensor::Sensor *pv1_power_       = nullptr;
    sensor::Sensor *pv2_power_       = nullptr;
    sensor::Sensor *total_energy_    = nullptr;
    sensor::Sensor *inverter_temp_   = nullptr;
    sensor::Sensor *status_code_     = nullptr;
    number::Number *power_limit_number_ = nullptr;
    float  wmax_lim_pct_{100.0f};
    int    wmax_lim_ena_{0};

    uint16_t   port_;
    WiFiServer server_;
    WiFiClient client_;
    bool       connected_{false};

    uint8_t img_[IMG_BYTES];     // 348 bytes — pre-built register image
    uint8_t rx_buf_[RX_BUF];    // 260 bytes
    uint8_t tx_buf_[TX_BUF];    // 260 bytes
    int     buf_len_{0};

    // ── Update all live-data positions in img_ ────────────────────────────────
    void ICACHE_RAM_ATTR update_live_data_() {
        // ── Model 101 body ────────────────────────────────────────────────────
        uint8_t *ib = img_ + (INV_BASE - SS_BASE) * 2u + 4u;

        float i_ac = rs(ac_current_);
        p16(ib +  0, u16_sf1(i_ac));                          // I_AC_Current
        p16(ib +  2, u16_sf1(i_ac));                          // I_AC_CurrentA
        p16(ib + 16, u16_sf1(rs(ac_voltage_)));               // I_AC_Voltage_AN

        float p_ac = rs(ac_power_);
        p16(ib + 24, i16_sf0(p_ac));                          // I_AC_Power
        p16(ib + 32, i16_sf0(p_ac));                          // I_AC_VA

        p16(ib + 28, u16_sf2(rs(grid_frequency_)));           // I_AC_Frequency

        float e_kwh = rs(total_energy_);
        p32(ib + 44, f_isnan(e_kwh) ? 0xFFFFFFFFu
                     : (uint32_t)(e_kwh * 1000.0f + 0.5f));   // I_AC_Energy_WH

        // Model 101 DC totals (sum of both strings)
        p16(ib + 50, u16_sf2(rs(pv1_current_) + rs(pv2_current_)));  // I_DC_Current
        p16(ib + 54, u16_sf1(rs(pv1_voltage_)));                      // I_DC_Voltage (primary)
        p16(ib + 58, i16_sf0(rs(pv1_power_)   + rs(pv2_power_)));     // I_DC_Power

        float temp = rs(inverter_temp_);
        uint16_t t = i16_sf1(temp);
        p16(ib + 62, t);                                      // I_Temp_Cab
        p16(ib + 64, t);                                      // I_Temp_Sink

        float gstat = rs(status_code_);
        p16(ib + 72, sunspec_status(gstat));                  // I_Status
        p16(ib + 74, (uint16_t)(int)gstat);                   // I_Status_Vendor

        p16(ib + 80, (uint16_t)((uint32_t)(wmax_lim_pct_ * 100.0f + 0.5f))); // WMaxLimPct
        p16(ib + 82, (uint16_t)wmax_lim_ena_);               // WMaxLim_Ena

        // ── Model 160 module 1 (PV1) ──────────────────────────────────────────
        uint8_t *mod1 = img_ + (M160_MOD1 - SS_BASE) * 2u;
        p16(mod1 +  2, u16_sf2(rs(pv1_current_)));   // DcCurrent  (SF -2 from header)
        p16(mod1 +  4, u16_sf1(rs(pv1_voltage_)));   // DcVoltage  (SF -1 from header)
        p16(mod1 +  6, i16_sf0(rs(pv1_power_)));     // DcPower    (SF  0 from header)

        // ── Model 160 module 2 (PV2) ──────────────────────────────────────────
        uint8_t *mod2 = img_ + (M160_MOD2 - SS_BASE) * 2u;
        p16(mod2 +  2, u16_sf2(rs(pv2_current_)));   // DcCurrent
        p16(mod2 +  4, u16_sf1(rs(pv2_voltage_)));   // DcVoltage
        p16(mod2 +  6, i16_sf0(rs(pv2_power_)));     // DcPower
    }

    // ── Modbus exception response ─────────────────────────────────────────────
    int exception_(uint8_t tid_hi, uint8_t tid_lo, uint8_t unit,
                   uint8_t fc, uint8_t code) {
        tx_buf_[0] = tid_hi;  tx_buf_[1] = tid_lo;
        tx_buf_[2] = 0;       tx_buf_[3] = 0;
        tx_buf_[4] = 0;       tx_buf_[5] = 3;
        tx_buf_[6] = unit;
        tx_buf_[7] = fc | 0x80u;
        tx_buf_[8] = code;
        return 9;
    }

    // ── Process one complete Modbus TCP frame ─────────────────────────────────
    int ICACHE_RAM_ATTR handle_frame_(const uint8_t *in, int in_len) {
        if (in_len < 8) return 0;

        uint8_t tid_hi = in[0], tid_lo = in[1];
        uint8_t unit   = in[6];
        uint8_t fc     = in[7];

        if (fc == 0x03 || fc == 0x04) {
            if (in_len < 12) return 0;
            uint16_t addr = ((uint16_t)in[8]  << 8) | in[9];
            uint16_t qty  = ((uint16_t)in[10] << 8) | in[11];

            if (qty == 0 || qty > 125u
                || addr < SS_BASE
                || (uint32_t)addr + qty > (uint32_t)(SS_BASE + REG_COUNT))
                return exception_(tid_hi, tid_lo, unit, fc, 0x02);

            update_live_data_();

            uint16_t offset     = (addr - SS_BASE) * 2u;
            uint16_t byte_count = qty * 2u;
            uint16_t pdu_len    = 2u + byte_count;

            tx_buf_[0] = tid_hi;  tx_buf_[1] = tid_lo;
            tx_buf_[2] = 0;       tx_buf_[3] = 0;
            tx_buf_[4] = pdu_len >> 8;  tx_buf_[5] = pdu_len & 0xFFu;
            tx_buf_[6] = unit;
            tx_buf_[7] = fc;
            tx_buf_[8] = (uint8_t)byte_count;
            memcpy(tx_buf_ + 9, img_ + offset, byte_count);
            return 9 + byte_count;

        } else if (fc == 0x06) {
            // ── FC06: Write Single Register ───────────────────────────────────
            if (in_len < 10) return 0;
            uint16_t reg = ((uint16_t)in[8] << 8) | in[9];
            uint16_t val = ((uint16_t)in[10] << 8) | in[11];

            if (reg == WMAX_PCT_ADDR) {
                float pct = (float)val / 100.0f;
                if (pct > 100.0f) pct = 100.0f;
                wmax_lim_pct_ = pct;
                if (power_limit_number_ != nullptr)
                    power_limit_number_->make_call().set_value(pct).perform();
                ESP_LOGI(TAG, "WMaxLimPct=%.2f%%", pct);
            } else if (reg == WMAX_ENA_ADDR) {
                wmax_lim_ena_ = (int)val;
                ESP_LOGI(TAG, "WMaxLim_Ena=%d", (int)val);
            } else {
                return exception_(tid_hi, tid_lo, unit, fc, 0x02); // Illegal Data Address
            }

            // FC06 echo: identical to request
            memcpy(tx_buf_, in, 12);
            return 12;

        } else if (fc == 0x10) {
            if (in_len < 13) return 0;
            uint16_t addr     = ((uint16_t)in[8]  << 8) | in[9];
            uint16_t qty      = ((uint16_t)in[10] << 8) | in[11];
            uint8_t  byte_cnt = in[12];
            if (in_len < 13 + (int)byte_cnt) return 0;

            for (uint16_t i = 0; i < qty; i++) {
                uint16_t reg = addr + i;
                uint16_t val = ((uint16_t)in[13 + i * 2] << 8) | in[14 + i * 2];

                if (reg == WMAX_PCT_ADDR) {
                    float pct = (float)val / 100.0f;
                    if (pct > 100.0f) pct = 100.0f;
                    wmax_lim_pct_ = pct;
                    if (power_limit_number_ != nullptr)
                        power_limit_number_->make_call().set_value(pct).perform();
                    ESP_LOGI(TAG, "WMaxLimPct=%.2f%%", pct);
                } else if (reg == WMAX_ENA_ADDR) {
                    wmax_lim_ena_ = (int)val;
                    ESP_LOGI(TAG, "WMaxLim_Ena=%d", (int)val);
                }
            }

            tx_buf_[0] = tid_hi;  tx_buf_[1] = tid_lo;
            tx_buf_[2] = 0;       tx_buf_[3] = 0;
            tx_buf_[4] = 0;       tx_buf_[5] = 6;
            tx_buf_[6] = unit;
            tx_buf_[7] = 0x10;
            tx_buf_[8] = in[8];   tx_buf_[9]  = in[9];
            tx_buf_[10] = in[10]; tx_buf_[11] = in[11];
            return 12;

        } else {
            return exception_(tid_hi, tid_lo, unit, fc, 0x01);
        }
    }
};

}  // namespace sunspec_server
}  // namespace esphome
