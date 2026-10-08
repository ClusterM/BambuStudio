#ifndef slic3r_GUI_wgtFilaManagerFeature_h
#define slic3r_GUI_wgtFilaManagerFeature_h

#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

constexpr const char* FilaManagerEnabledConfigKey = "studio_enable_fila_manager";

constexpr const char* SpoolmanUrlConfigKey   = "spoolman_url";
constexpr const char* HaUrlConfigKey         = "ha_url";
constexpr const char* HaTokenConfigKey       = "ha_token";

bool is_fila_manager_disabled_by_config(const std::string& enabled_value, bool is_macos);

// Spoolman / Home Assistant config helpers. Credentials for Spoolman live
// inside spoolman_url as https://user:password@host/api/v1
bool        is_spoolman_enabled();
bool        is_ha_overlay_enabled();
std::string spoolman_url_config();
std::string ha_url_config();
std::string ha_token_config();
// All supported slot codes: a1..d4, ht_a..ht_h, external, external_aux.
// Config key is "ha_spool_" + code. Empty entity means the slot is unused.
std::vector<std::string> ha_spool_codes();
std::string              ha_spool_entity(const std::string& code);

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_wgtFilaManagerFeature_h
