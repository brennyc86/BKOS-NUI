#include "platform_fs.h"

// bkos_fs/bkos_fs_is_fat leven hier (niet in platform_fs.h zelf) zodat er maar
// één definitie in de hele build is — zie platform_fs.h voor de uitleg over
// waarom dit bestand geen enkele "SPIFFS"/"FFat"-aanroep bevat: die zouden
// door de #define SPIFFS bkos_fs (ergens eerder in de gemergde sketch al
// actief) verkeerd herschreven worden. fs::FS heeft geen argumentloze
// constructor, vandaar de expliciete lege-impl-initialisatie.
#if !PLATFORM_PICO
fs::FS bkos_fs      = fs::FS(fs::FSImplPtr());
bool   bkos_fs_is_fat = false;
#endif
