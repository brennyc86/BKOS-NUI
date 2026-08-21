#pragma once

// Herstelmenu tijdens opstarten — raakt de gebruiker het midden van het scherm
// aan binnen het openingsvenster, dan krijgt hij een keuze tussen gewoon
// opstarten, BKOS verwijderen (blanco firmware terugzetten) of een update
// zoeken — zonder van de USB-poort afhankelijk te zijn bij kapotte software.
// Draait met uitsluitend scherm + touch + WiFi geladen (vóór io_boot(),
// app_setup(), net_setup(), lua_runtime, enz.) zodat een bug daar het
// herstelmenu niet kan blokkeren.

bool recovery_check();   // pollt kort op een centrale aanraking; true = herstelmenu tonen
void recovery_menu();    // blokkerend; keert alleen terug bij "GEWOON OPSTARTEN"
