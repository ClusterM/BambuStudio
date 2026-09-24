#include "wgtFilaManagerFeature.h"

#include "slic3r/GUI/GUI_App.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace Slic3r { namespace GUI {

namespace {

std::string normalize_flag(const std::string& value)
{
    std::string normalized(value);
    normalized.erase(normalized.begin(), std::find_if(normalized.begin(), normalized.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    normalized.erase(std::find_if(normalized.rbegin(), normalized.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), normalized.end());
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    return normalized;
}

bool is_truthy_flag(const std::string& value)
{
    return value == "1" || value == "true" || value == "on" || value == "yes";
}

bool is_falsey_flag(const std::string& value)
{
    return value == "0" || value == "false" || value == "off" || value == "no";
}

std::string trim_copy(const std::string& value)
{
    auto begin = std::find_if(value.begin(), value.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    });
    auto end = std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base();
    if (begin >= end)
        return {};
    return std::string(begin, end);
}

std::string config_or_default(const std::string& key, const char* fallback)
{
    if (!wxGetApp().app_config)
        return fallback ? std::string(fallback) : std::string();
    const std::string value = trim_copy(wxGetApp().app_config->get(key));
    if (!value.empty())
        return value;
    return fallback ? std::string(fallback) : std::string();
}

} // namespace

bool is_fila_manager_disabled_by_config(const std::string& enabled_value, bool is_macos)
{
    const std::string normalized = normalize_flag(enabled_value);
    if (normalized.empty())
        return is_macos;
    if (is_falsey_flag(normalized))
        return true;
    return !is_truthy_flag(normalized);
}

bool is_spoolman_enabled()
{
    return !spoolman_url_config().empty();
}

bool is_ha_overlay_enabled()
{
    return is_spoolman_enabled() && !ha_url_config().empty() && !ha_token_config().empty();
}

std::string spoolman_url_config()
{
    if (!wxGetApp().app_config)
        return {};
    return trim_copy(wxGetApp().app_config->get(SpoolmanUrlConfigKey));
}

std::string ha_url_config()
{
    if (!wxGetApp().app_config)
        return {};
    return trim_copy(wxGetApp().app_config->get(HaUrlConfigKey));
}

std::string ha_token_config()
{
    if (!wxGetApp().app_config)
        return {};
    return trim_copy(wxGetApp().app_config->get(HaTokenConfigKey));
}

std::string ha_ams_tray_entity(int tray_index)
{
    switch (tray_index) {
    case 1: return config_or_default(HaAmsTray1ConfigKey, HaAmsTray1Default);
    case 2: return config_or_default(HaAmsTray2ConfigKey, HaAmsTray2Default);
    case 3: return config_or_default(HaAmsTray3ConfigKey, HaAmsTray3Default);
    case 4: return config_or_default(HaAmsTray4ConfigKey, HaAmsTray4Default);
    default: return {};
    }
}

std::string ha_external_spool_entity()
{
    return config_or_default(HaExternalSpoolConfigKey, HaExternalSpoolDefault);
}

}} // namespace Slic3r::GUI
