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

std::vector<std::string> ha_spool_codes()
{
    std::vector<std::string> codes;
    codes.reserve(4 * 4 + 8 + 2);
    for (char unit = 'a'; unit <= 'd'; ++unit) {
        for (int slot = 1; slot <= 4; ++slot)
            codes.push_back(std::string(1, unit) + std::to_string(slot));
    }
    for (char unit = 'a'; unit <= 'h'; ++unit)
        codes.push_back(std::string("ht_") + unit);
    codes.push_back("external");
    codes.push_back("external_aux");
    return codes;
}

std::string ha_spool_entity(const std::string& code)
{
    if (!wxGetApp().app_config || code.empty())
        return {};
    return trim_copy(wxGetApp().app_config->get(std::string("ha_spool_") + code));
}

}} // namespace Slic3r::GUI
