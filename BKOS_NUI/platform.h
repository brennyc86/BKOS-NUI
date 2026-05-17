#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// platform.h — Eén plek voor alle platform-afhankelijke defines
//
// Ondersteunde targets:
//   PLATFORM_ESP32  — ESP32-S3, 800×480 RGB panel, PSRAM, GT911 touch
//   PLATFORM_PICO   — RP2040/RP2350 (Pico W), 240×320 SPI display, XPT2046
//   PLATFORM_WROOM  — ESP32 WROOM32, 240×320 SPI display, XPT2046
//
// PLATFORM_WROOM moet worden doorgegeven via compiler flag: -DPLATFORM_WROOM=1
// SCREEN_SMALL is true voor Pico en WROOM (240×320 ILI9341 scherm)
// ─────────────────────────────────────────────────────────────────────────────

// ─── Platform detectie ────────────────────────────────────────────────────────
#ifndef PLATFORM_WROOM
  #define PLATFORM_WROOM 0
#endif

#if defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_RP2350)
  #define PLATFORM_PICO  1
  #define PLATFORM_ESP32 0
  #undef  PLATFORM_WROOM
  #define PLATFORM_WROOM 0
#else
  #define PLATFORM_PICO  0
  #define PLATFORM_ESP32 1
  // PLATFORM_WROOM blijft zoals doorgegeven via compiler flag
#endif

// ─── SCREEN_SMALL: 240×320 ILI9341 scherm (Pico of WROOM) ───────────────────
#define SCREEN_SMALL (PLATFORM_PICO || PLATFORM_WROOM)

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
  #define TFT_MISO  16    // GP16 — SPI0 MISO (nodig voor XPT2046 touch)

  // XPT2046 resistieve touch (gedeelde SPI bus met display)
  #define PICO_TOUCH_XPT2046
  #define PICO_TS_CS   13    // GP13 — touch chip select
  #define PICO_TS_IRQ  11    // GP11 — touch interrupt

  // SD kaart (gedeelde SPI bus: SCK=18, MISO=16, MOSI=19)
  #define PICO_SD_CS   12    // GP12 — SD chip select

  // IO shift register bus (BKOS4 protocol, direct GPIO)
  #define HC_IN    0    // GP0  — seriële data ingang (HC165 → Pico)
  #define HC_SCK   1    // GP1  — seriële klok
  #define HC_PCK   2    // GP2  — parallelle klok (load)
  #define HC_UIT   3    // GP3  — seriële data uitgang (Pico → HC595)
  #define HC_ID    4    // GP4  — module ID data ingang

#elif PLATFORM_WROOM
  // CYD (ESP32-2432S028R): ILI9341 SPI 240×320 portret
  #define TFT_W     240
  #define TFT_H     320
  #define TFT_BL    21   // GPIO21 — backlight PWM
  #define TFT_CS    15   // GPIO15 — display chip select
  #define TFT_DC     2   // GPIO2  — data/command
  #define TFT_RST   -1   // niet aangesloten
  #define TFT_SCK   14   // GPIO14 — SPI SCK (HSPI)
  #define TFT_MOSI  13   // GPIO13 — SPI MOSI
  #define TFT_MISO  12   // GPIO12 — SPI MISO

  // XPT2046 resistieve touch (gedeelde HSPI bus met display)
  #define WROOM_TOUCH_XPT2046
  #define WROOM_TS_CS   33   // GPIO33 — touch chip select
  #define WROOM_TS_IRQ  36   // GPIO36 — touch interrupt

  // UART2 naar ATtiny3217 (net als ESP32-S3)
  #define IO_UART_RX  22   // GPIO22 — ATtiny TX → CYD RX
  #define IO_UART_TX  27   // GPIO27 — CYD TX → ATtiny RX

#else
  // Arduino_ESP32RGBPanel 800×480 liggend
  #define TFT_W    800
  #define TFT_H    480
  #define TFT_BL   2     // GPIO2 — backlight
  // RGB panel pinnen: zie hw_scherm.ino
  // GT911 touch pinnen: zie hw_touch.h
#endif

// ─── IO seriële poort ─────────────────────────────────────────────────────────
#if PLATFORM_WROOM
  #define IO_SERIAL          Serial2
  #define IO_SERIAL_BEGIN()  Serial2.begin(IO_BAUD, SERIAL_8N1, IO_UART_RX, IO_UART_TX)
#elif !PLATFORM_PICO
  #define IO_SERIAL          Serial
  #define IO_SERIAL_BEGIN()  Serial.begin(IO_BAUD)
#endif

// ─── Geheugen allocatie ───────────────────────────────────────────────────────
#if PLATFORM_ESP32 && !PLATFORM_WROOM
  // ESP32-S3 met PSRAM
  #include <esp_heap_caps.h>
  #define PLATFORM_MALLOC(n)    heap_caps_malloc((n), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
  #define PLATFORM_REALLOC(p,n) heap_caps_realloc((p), (n), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
  #define PLATFORM_FREE(p)      heap_caps_free(p)
#else
  // Pico en WROOM: gewoon malloc (geen PSRAM)
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
// Pico:  taken worden NIET gestart (no-op) — alles draait op de hoofd-thread
#if PLATFORM_PICO
  #define PLATFORM_TASK_CREATE(fn, naam, stack, param, prio, handle) ((void)0)
#else
  #define PLATFORM_TASK_CREATE(fn, naam, stack, param, prio, handle) \
      xTaskCreatePinnedToCore((fn), (naam), (stack), (param), (prio), (handle), 0)
#endif

// ─── FreeRTOS types voor RP2040 ──────────────────────────────────────────────
// Pico W gebruikt FreeRTOS intern voor WiFi (CYW43), maar exposeert het NIET
// naar user code. FreeRTOS.h direct includen veroorzaakt een linker-fout omdat
// de library dan verwacht wordt maar niet meegecombineerd wordt.
// Oplossing: compatibele forward-declaraties + macro-stubs zodat code die
// vTaskDelay/TaskHandle_t gebruikt gewoon compileert en delay() aanroept.
// INC_FREERTOS_H: als FreeRTOS.h elders al geïncludeerd is, gebruik die types.
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
