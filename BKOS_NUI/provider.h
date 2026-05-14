#pragma once
#include <Arduino.h>

// ============================================================
// provider.h — Achtergrond data-provider framework
// ============================================================
// Providers draaien alleen als het scherm actief is (stroom besparen).
// Zware providers (WiFi, BLE) beheren hun eigen FreeRTOS-taak.
// Lichte providers (berekeningen, polling) kunnen hier registreren.

#define MAX_PROVIDERS 8

typedef bool (*ProviderFn)();

void provider_registreer(const char* id, uint32_t interval_ms, ProviderFn fn);
void provider_loop();   // aanroepen vanuit hw_loop()
