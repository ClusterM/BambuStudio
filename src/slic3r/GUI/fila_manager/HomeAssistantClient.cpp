#include "HomeAssistantClient.h"
#include "wgtFilaManagerFeature.h"

#include "slic3r/Utils/Http.hpp"

#include <nlohmann/json.hpp>
#include <boost/log/trivial.hpp>
#include <cctype>
#include <cmath>
#include <iostream>

namespace Slic3r { namespace GUI {

namespace {

std::string trim_copy(const std::string& value)
{
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])))
        ++begin;
    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])))
        --end;
    return value.substr(begin, end - begin);
}

std::string trim_slash(std::string value)
{
    while (!value.empty() && value.back() == '/')
        value.pop_back();
    return value;
}

void log_ha(const std::string& msg)
{
    std::cerr << "[HomeAssistant] " << msg << std::endl;
}

} // namespace

int HomeAssistantClient::parse_spool_id_state(const std::string& state)
{
    const std::string s = trim_copy(state);
    if (s.empty() || s == "unknown" || s == "unavailable" || s == "none" || s == "null")
        return -1;
    try {
        const double value = std::stod(s);
        if (value <= 0.0)
            return -1;
        return static_cast<int>(std::lround(value));
    } catch (...) {
        return -1;
    }
}

HomeAssistantClient::HomeAssistantClient(std::string base_url, std::string token)
    : m_base_url(trim_slash(trim_copy(std::move(base_url))))
    , m_token(trim_copy(std::move(token)))
{}

HomeAssistantClient HomeAssistantClient::from_app_config()
{
    return HomeAssistantClient(ha_url_config(), ha_token_config());
}

std::string HomeAssistantClient::state_url(const std::string& entity_id) const
{
    return m_base_url + "/api/states/" + entity_id;
}

bool HomeAssistantClient::fetch_entity_state_sync(const std::string& entity_id,
                                                  std::string& state,
                                                  std::string& error) const
{
    if (m_base_url.empty() || m_token.empty()) {
        error = "Home Assistant URL or token is empty";
        return false;
    }
    if (entity_id.empty()) {
        error = "entity id is empty";
        return false;
    }

    bool ok = false;
    std::string body;
    unsigned status = 0;
    const std::string url = state_url(entity_id);
    log_ha("GET " + url);
    try {
        Http http = Http::get(url);
        http.header("accept", "application/json")
            .header("Authorization", "Bearer " + m_token)
            .timeout_connect(10)
            .timeout_max(15)
            .on_complete([&](std::string resp, unsigned st) {
                body   = std::move(resp);
                status = st;
                ok     = true;
            })
            .on_error([&](std::string resp, std::string err, unsigned st) {
                status = st;
                error  = err.empty() ? resp : err;
                if (error.empty())
                    error = "HTTP " + std::to_string(st);
            })
            .perform_sync();
    } catch (const std::exception& e) {
        error = e.what();
        log_ha("GET " + url + " failed: " + error);
        return false;
    }

    if (!ok) {
        log_ha("GET " + url + " failed: HTTP " + std::to_string(status) + " " + error);
        return false;
    }

    try {
        const auto payload = nlohmann::json::parse(body);
        if (payload.contains("state") && payload["state"].is_string())
            state = payload["state"].get<std::string>();
        else if (payload.contains("state") && payload["state"].is_number())
            state = std::to_string(payload["state"].get<double>());
        else {
            error = "HA state payload missing state";
            log_ha("GET " + url + " parse error: " + error);
            return false;
        }
    } catch (const std::exception& e) {
        error = e.what();
        log_ha("GET " + url + " parse error: " + error);
        return false;
    }
    log_ha("GET " + url + " -> " + std::to_string(status)
           + " state=" + state
           + " spool_id=" + std::to_string(parse_spool_id_state(state)));
    return true;
}

bool HomeAssistantClient::fetch_slot_ids_sync(HaSlotIds& out, std::string& error) const
{
    HaSlotIds ids;
    for (int i = 1; i <= 4; ++i) {
        std::string state;
        std::string err;
        if (!fetch_entity_state_sync(ha_ams_tray_entity(i), state, err)) {
            error = err;
            return false;
        }
        ids.trays[static_cast<size_t>(i - 1)] = parse_spool_id_state(state);
    }
    {
        std::string state;
        std::string err;
        if (!fetch_entity_state_sync(ha_external_spool_entity(), state, err)) {
            error = err;
            return false;
        }
        ids.external = parse_spool_id_state(state);
    }
    out = ids;
    return true;
}

}} // namespace Slic3r::GUI
