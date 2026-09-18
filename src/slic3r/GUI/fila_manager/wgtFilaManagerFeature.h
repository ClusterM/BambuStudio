#ifndef slic3r_GUI_wgtFilaManagerFeature_h
#define slic3r_GUI_wgtFilaManagerFeature_h

#include <string>

namespace Slic3r { namespace GUI {

constexpr const char* FilaManagerEnabledConfigKey = "studio_enable_fila_manager";

constexpr const char* SpoolmanUrlConfigKey   = "spoolman_url";
constexpr const char* HaUrlConfigKey         = "ha_url";
constexpr const char* HaTokenConfigKey       = "ha_token";
constexpr const char* HaAmsTray1ConfigKey    = "ha_ams_tray_1";
constexpr const char* HaAmsTray2ConfigKey    = "ha_ams_tray_2";
constexpr const char* HaAmsTray3ConfigKey    = "ha_ams_tray_3";
constexpr const char* HaAmsTray4ConfigKey    = "ha_ams_tray_4";
constexpr const char* HaExternalSpoolConfigKey = "ha_external_spool";

constexpr const char* HaAmsTray1Default      = "input_number.ams_tray_1_spool_id";
constexpr const char* HaAmsTray2Default      = "input_number.ams_tray_2_spool_id";
constexpr const char* HaAmsTray3Default      = "input_number.ams_tray_3_spool_id";
constexpr const char* HaAmsTray4Default      = "input_number.ams_tray_4_spool_id";
constexpr const char* HaExternalSpoolDefault = "input_number.external_spool_id";

bool is_fila_manager_disabled_by_config(const std::string& enabled_value, bool is_macos);

// Spoolman / Home Assistant config helpers. Credentials for Spoolman live
// inside spoolman_url as https://user:password@host/api/v1
bool        is_spoolman_enabled();
bool        is_ha_overlay_enabled();
std::string spoolman_url_config();
std::string ha_url_config();
std::string ha_token_config();
// tray_index is 1..4; empty / unknown returns the matching default entity id.
std::string ha_ams_tray_entity(int tray_index);
std::string ha_external_spool_entity();

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_wgtFilaManagerFeature_h
