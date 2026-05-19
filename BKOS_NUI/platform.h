#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// platform.h — Eén plek voor alle platform-afhankelijke defines
//
// Ondersteunde targets:
//   PLATFORM_ESP32  — ESP32-S3, 800×480 RGB panel, PSRAM, GT911 touch
//   PLATFORM_PICO   — RP2040/RP2350 (Pico W), 240×320 SPI display, XPT2046
//   PLATFORM_WROOM  — ESP32 WROOM32, custom IO board, 240×320 ILI9341 + ATtiny3217
//   PLATFORM_CYD28  — ESP32-2432S028R ("CYD"), 240×320 ILI9341, XPT2046
//   PLATFORM_CYD40H — ESP32 4" CYD, 480×320 ST7796 liggend, XPT2046 (aparte VSPI)
//   PLATFORM_CYD40V — ESP32 4" CYD, 320×480 ST7796 staand,  XPT2046 (aparte VSPI)
//
// Compiler flags: -DPLATFORM_WROOM=1 / -DPLATFORM_CYD28=1 /
//                 -DPLATFORM_CYD40H=1 / -DPLATFORM_CYD40V=1
// ─────────────────────────────────────────────────────────────────────────────

// ─── Platform defaults ────────────────────────────────────────────────────────
#ifndef PLATFORM_WROOM
  #define PLATFORM_WROOM  0
#endif
#ifndef PLATFORM_CYD28
  #define PLATFORM_CYD28  0
#endif
#ifndef PLATFORM_CYD40H
  #define PLATFORM_CYD40H 0
#endif
#ifndef PLATFORM_CYD40V
  #define PLATFORM_CYD40V 0
#endif

// ─── RP2040/RP2350 auto-detectie ─────────────────────────────────────────────
#if defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_RP2350)
  #define PLATFORM_PICO   1
  #define PLATFORM_ESP32  0
  #undef  PLATFORM_WROOM
  #define PLATFORM_WROOM  0
  #undef  PLATFORM_CYD28
  #define PLATFORM_CYD28  0
  #undef  PLATFORM_CYD40H
  #define PLATFORM_CYD40H 0
  #undef  PLATFORM_CYD40V
  #define PLATFORM_CYD40V 0
#else
  #define PLATFORM_PICO   0
  #define PLATFORM_ESP32  1
  // WROOM/CYD flags blijven zoals doorgegeven via compiler flag
#endif

// ─── Convenience macros ───────────────────────────────────────────────────────
#define PLATFORM_CYD     (PLATFORM_CYD28 || PLATFORM_CYD40H || PLATFORM_CYD40V)
#define PLATFORM_XPT2046 (PLATFORM_PICO  || PLATFORM_WROOM  || PLATFORM_CYD)

// ─── SCREEN_SMALL: compacte 240×320 / 320×480 portret UI ─────────────────────
#define SCREEN_SMALL (PLATFORM_PICO || PLATFORM_WROOM || PLATFORM_CYD28 || PLATFORM_CYD40V)

// ─── Scherm en pinnen ─────────────────────────────────────────────────────────
#if PLATFORM_PICO
  // ILI9341 SPI display, 240×320 portret
  #define TFT_W     240
  #define TFT_H     320
  #define TFT_BL    10    // GP10 — backlight PWM
  #define TFT_CS    17    // GP17 — SPI chip select
  #define TFT_DC    15    // GP15 — data/command
  #define TFT_RST   14    // GP14 — reset
  #define TFT_SCK   18    // GP18 — SPI0 clock
  #define TFT_MOSI  19    // GP19 — SPI0 MOSI
  #define TFT_MISO  16    // GP16 — SPI0 MISO (nodig voor XPT2046)

  #define PICO_TS_CS   13    // GP13 — touch chip select
  #define PICO_TS_IRQ  11    // GP11 — touch interrupt

  #define PICO_SD_CS   12    // GP12 — SD chip select

  #define HC_IN    0    // GP0  — HC165 data ingang
  #define HC_SCK   1    // GP1  — seriële klok
  #define HC_PCK   2    // GP2  — parallelle klok
  #define HC_UIT   3    // GP3  — HC595 data uitgang
  #define HC_ID    4    // GP4  — module ID data

#elif PLATFORM_WROOM
  // Custom WROOM IO board: ILI9341 240×320 portret, HC shift-register IO
  #define TFT_W     240
  #define TFT_H     320
  #define TFT_BL    19   // GPIO19 — backlight PWM
  #define TFT_CS     0   // GPIO0  — display CS (BKOS4: cs_tft=0; HC_SCK gebruikt GPIO15)
  #define TFT_DC    23   // GPIO23 — data/command
  #define TFT_RST   16   // GPIO16 — display reset
  #define TFT_SCK   14   // GPIO14 — HSPI SCK
  #define TFT_MOSI  13   // GPIO13 — HSPI MOSI
  #define TFT_MISO  12   // GPIO12 — HSPI MISO

  #define WROOM_TS_CS   22   // GPIO22 — touch chip select
  #define WROOM_TS_IRQ  21   // GPIO21 — touch interrupt

  // HC shift-register IO (zelfde als BKOS4 configuratie 3)
  #define HC_IN    35   // GPIO35 (input-only) — data ingang HC165
  #define HC_SCK   15   // GPIO15 — seriële klok
  #define HC_PCK    2   // GPIO2  — parallelle klok / latch
  #define HC_UIT    4   // GPIO4  — data uitgang HC595
  #define HC_ID    34   // GPIO34 (input-only) — module ID

#elif PLATFORM_CYD28
  // ESP32-2432S028R: ILI9341 240×320 portret, display HSPI, touch aparte VSPI
  #define TFT_W     240
  #define TFT_H     320
  #define TFT_BL    21   // GPIO21 — backlight PWM
  #define TFT_CS    15   // GPIO15 — display chip select
  #define TFT_DC     2   // GPIO2  — data/command
  #define TFT_RST   -1   // niet aangesloten
  #define TFT_SCK   14   // GPIO14 — HSPI SCK
  #define TFT_MOSI  13   // GPIO13 — HSPI MOSI
  #define TFT_MISO  12   // GPIO12 — HSPI MISO

  // Aparte VSPI bus voor XPT2046 touch
  #define CYD28_TS_SCK   25   // GPIO25
  #define CYD28_TS_MOSI  32   // GPIO32
  #define CYD28_TS_MISO  39   // GPIO39 (SVP, input-only)
  #define CYD28_TS_IRQ   36   // GPIO36 (VP, input-only)
  #define CYD28_TS_CS    33   // GPIO33 — touch chip select

#elif PLATFORM_CYD40H || PLATFORM_CYD40V
  // 4" CYD (ESP32-32E): ST7796, display HSPI + aparte VSPI voor XPT2046
  #if PLATFORM_CYD40H
    #define TFT_W   480    // liggend 480×320
    #define TFT_H   320
  #else
    #define TFT_W   320    // staand 320×480
    #define TFT_H   480
  #endif
  #define TFT_BL    27   // GPIO27 — backlight (anders dan CYD28/WROOM)
  #define TFT_CS    15   // GPIO15 — display chip select
  #define TFT_DC     2   // GPIO2  — data/command
  #define TFT_RST   -1   // niet aangesloten
  #define TFT_SCK   14   // GPIO14 — HSPI SCK
  #define TFT_MOSI  13   // GPIO13 — HSPI MOSI
  #define TFT_MISO  12   // GPIO12 — HSPI MISO

  // XPT2046 deelt HSPI met display (zelfde SCK/MOSI/MISO, eigen CS)
  #define CYD40_TS_SCK   14   // GPIO14 — HSPI SCK  (= TFT_SCK)
  #define CYD40_TS_MOSI  13   // GPIO13 — HSPI MOSI (= TFT_MOSI)
  #define CYD40_TS_MISO  12   // GPIO12 — HSPI MISO (= TFT_MISO)
  #define CYD40_TS_CS    33   // GPIO33 — touch CS  (display CS = 15)
  #define CYD40_TS_IRQ   36   // GPIO36 — touch interrupt (input-only, XPT2046 pull-up)

#else
  // ESP32-S3 800×480 RGB liggend
  #define TFT_W    800
  #define TFT_H    480
  #define TFT_BL   2     // GPIO2 — backlight
  // RGB panel pinnen: zie hw_scherm.ino
  // GT911 touch pinnen: zie hw_touch.h
#endif

// ─── IO seriële poort ─────────────────────────────────────────────────────────
// WROOM en Pico gebruiken HC shift-registers — geen UART IO bus
#if !PLATFORM_PICO && !PLATFORM_WROOM
  // ESP32-S3 en CYD: Serial (S3=USB/JTAG; CYD=USB, geen IO bus aangesloten)
  #define IO_SERIAL          Serial
  #define IO_SERIAL_BEGIN()  Serial.begin(IO_BAUD)
#endif

// ─── Geheugen allocatie ───────────────────────────────────────────────────────
#if PLATFORM_ESP32 && !PLATFORM_WROOM && !PLATFORM_CYD
  // Alleen ESP32-S3 heeft PSRAM
  #include <esp_heap_caps.h>
  #define PLATFORM_MALLOC(n)    heap_caps_malloc((n), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
  #define PLATFORM_REALLOC(p,n) heap_caps_realloc((p), (n), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
  #define PLATFORM_FREE(p)      heap_caps_free(p)
#else
  #define PLATFORM_MALLOC(n)    malloc(n)
  #define PLATFORM_REALLOC(p,n) realloc((p), (n))
  #define PLATFORM_FREE(p)      free(p)
#endif

// ─── Vrij geheugen ───────────────────────────────────────────────────────────
#if PLATFORM_PICO
  #define PLATFORM_FREE_HEAP() ((unsigned)rp2040.getFreeHeap())
#else
  #define PLATFORM_FREE_HEAP() ((unsigned)ESP.getFreeHeap())
#endif

// ─── Herstart ─────────────────────────────────────────────────────────────────
#if PLATFORM_PICO
  #define PLATFORM_REBOOT() rp2040.reboot()
#else
  #define PLATFORM_REBOOT() ESP.restart()
#endif

// ─── FreeRTOS taak aanmaken ───────────────────────────────────────────────────
// ESP32: xTaskCreatePinnedToCore op core 0
// Pico:  no-op — alles draait op de hoofd-thread
#if PLATFORM_PICO
  #define PLATFORM_TASK_CREATE(fn, naam, stack, param, prio, handle) ((void)0)
#else
  #define PLATFORM_TASK_CREATE(fn, naam, stack, param, prio, handle) \
      xTaskCreatePinnedToCore((fn), (naam), (stack), (param), (prio), (handle), 0)
#endif

// ─── FreeRTOS stubs voor RP2040 ──────────────────────────────────────────────
#if PLATFORM_PICO && !defined(INC_FREERTOS_H)
  struct tskTaskControlBlock;
  typedef struct tskTaskControlBlock* TaskHandle_t;
  typedef long          BaseType_t;
  typedef unsigned long TickType_t;
  #define pdTRUE             ((BaseType_t)1)
  #define portTICK_PERIOD_MS 1
  #define vTaskDelay(ms)     delay((unsigned long)(ms))
  static inline void vTaskDelete(TaskHandle_t) {}
#endif
