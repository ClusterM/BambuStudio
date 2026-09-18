#include "SpoolmanClient.h"
#include "wgtFilaManagerColorType.h"
#include "wgtFilaManagerFeature.h"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/Utils/Http.hpp"
#include "libslic3r/PresetBundle.hpp"

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

bool iequals_ascii(const std::string& a, const std::string& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i]))
            != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

std::string trim_slash(std::string value)
{
    while (!value.empty() && value.back() == '/')
        value.pop_back();
    return value;
}

void apply_auth(Http& http, const SpoolmanEndpoint& ep)
{
    if (ep.has_auth())
        http.auth_basic(ep.user, ep.password);
}

void log_spoolman(const std::string& msg)
{
    std::cerr << "[Spoolman] " << msg << std::endl;
}

std::string json_string(const nlohmann::json& j, const char* key)
{
    if (!j.contains(key))
        return {};
    const auto& v = j[key];
    if (v.is_string())
        return v.get<std::string>();
    if (v.is_number_integer())
        return std::to_string(v.get<int64_t>());
    if (v.is_number_unsigned())
        return std::to_string(v.get<uint64_t>());
    return {};
}

double json_double(const nlohmann::json& j, const char* key, double fallback = 0.0)
{
    if (!j.contains(key) || j[key].is_null())
        return fallback;
    const auto& v = j[key];
    if (v.is_number())
        return v.get<double>();
    if (v.is_string()) {
        try { return std::stod(v.get<std::string>()); } catch (...) {}
    }
    return fallback;
}

// Filament Type filter wants the broad class (PLA / PETG / TPU).
// Spoolman often stores a longer product line in `material`.
std::string base_filament_type(const std::string& material)
{
    if (material.empty())
        return {};
    const auto space = material.find(' ');
    if (space == std::string::npos)
        return material;
    return material.substr(0, space);
}

std::string normalize_color_hex(std::string hex)
{
    hex = trim_copy(hex);
    if (hex.empty())
        return {};
    if (hex.front() != '#')
        hex.insert(hex.begin(), '#');
    return hex;
}

void append_hex_list(std::vector<std::string>& out, const std::string& raw)
{
    size_t start = 0;
    while (start < raw.size()) {
        const size_t comma = raw.find(',', start);
        const size_t end   = comma == std::string::npos ? raw.size() : comma;
        std::string hex = normalize_color_hex(raw.substr(start, end - start));
        if (!hex.empty())
            out.push_back(std::move(hex));
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
}

// OpenAPI documents a comma-separated string; some Spoolman builds
// return a JSON array. Accept both.
std::vector<std::string> parse_spoolman_colors(const nlohmann::json& filament)
{
    std::vector<std::string> colors;
    if (filament.contains("multi_color_hexes") && !filament["multi_color_hexes"].is_null()) {
        const auto& v = filament["multi_color_hexes"];
        if (v.is_array()) {
            for (const auto& item : v) {
                if (item.is_string())
                    append_hex_list(colors, item.get<std::string>());
            }
        } else if (v.is_string()) {
            append_hex_list(colors, v.get<std::string>());
        }
    }
    if (colors.empty()) {
        std::string hex = normalize_color_hex(filament.value("color_hex", std::string()));
        if (!hex.empty())
            colors.push_back(std::move(hex));
    }
    return colors;
}

// Spoolman direction is the same split as Studio color_type:
//   coaxial      = side-by-side / dual colour  → MultiColor (1)
//   longitudinal = colour changes along length → Gradient (0)
int spoolman_color_type(const nlohmann::json& filament, std::size_t color_count)
{
    if (color_count <= 1)
        return to_fila_manager_color_type_int(FilaManagerColorType::Single);

    std::string dir = filament.value("multi_color_direction", std::string());
    for (char& c : dir)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (dir == "longitudinal")
        return to_fila_manager_color_type_int(FilaManagerColorType::Gradient);
    if (dir == "coaxial")
        return to_fila_manager_color_type_int(FilaManagerColorType::MultiColor);
    return to_fila_manager_color_type_int(fallback_fila_manager_color_type(color_count));
}

} // namespace

int remain_percent_from_weights(double remaining_weight, double initial_weight)
{
    if (initial_weight <= 0.0)
        return remaining_weight > 0.0 ? 100 : 0;
    double pct = remaining_weight / initial_weight * 100.0;
    if (pct < 0.0)   pct = 0.0;
    if (pct > 100.0) pct = 100.0;
    return static_cast<int>(std::lround(pct));
}

SpoolmanEndpoint parse_spoolman_url(const std::string& raw_url)
{
    SpoolmanEndpoint ep;
    std::string raw = trim_copy(raw_url);
    if (raw.empty())
        return ep;

    const auto scheme = raw.find("://");
    if (scheme != std::string::npos) {
        const auto auth_start = scheme + 3;
        const auto slash = raw.find('/', auth_start);
        const auto at = raw.find('@', auth_start);
        if (at != std::string::npos && (slash == std::string::npos || at < slash)) {
            const std::string userinfo = raw.substr(auth_start, at - auth_start);
            const auto colon = userinfo.find(':');
            if (colon == std::string::npos) {
                ep.user = Http::url_decode(userinfo);
            } else {
                ep.user     = Http::url_decode(userinfo.substr(0, colon));
                ep.password = Http::url_decode(userinfo.substr(colon + 1));
            }
            raw = raw.substr(0, auth_start) + raw.substr(at + 1);
        }
    }

    ep.base_url = trim_slash(raw);
    return ep;
}

FilamentSpool spool_from_spoolman_json(const nlohmann::json& j)
{
    FilamentSpool s;
    s.spool_id     = json_string(j, "id");
    s.tag_uid      = s.spool_id;
    s.cloud_synced = true;
    s.entry_method = "manual";

    if (j.contains("filament") && j["filament"].is_object()) {
        const auto& f = j["filament"];
        // Cloud: filamentType = PLA/PETG, filamentName = "PLA Basic" (series).
        // Spoolman: material = "PLA" / "PETG" / "PLA MATTE High Speed",
        //           name = colour ("SILVER"). Do not put the colour in series:
        //           the FM table groups by brand + series and the detail
        //           dialog's Material Type is series.
        const std::string material = f.value("material", std::string());
        s.material_type = base_filament_type(material);
        s.series        = material.empty() ? s.material_type : material;
        s.color_name    = f.value("name", std::string());
        s.colors        = parse_spoolman_colors(f);
        if (!s.colors.empty())
            s.color_code = s.colors.front();
        s.color_type    = spoolman_color_type(f, s.colors.size());
        if (f.contains("vendor") && f["vendor"].is_object())
            s.brand = f["vendor"].value("name", std::string());
    }

    // Studio (STUDIO-17991): initial_weight == full-spool net grams
    // (cloud totalNetWeight), spool_weight == 0. If spool_weight > 0 the
    // UI treats initial_weight as GROSS and shows total = initial - spool.
    // Spoolman.spool_weight is the empty carrier — do not copy it.
    s.initial_weight = json_double(j, "initial_weight");
    s.net_weight     = json_double(j, "remaining_weight");
    s.spool_weight   = 0;
    s.remain_percent = remain_percent_from_weights(s.net_weight, s.initial_weight);

    const bool archived = j.value("archived", false);
    if (archived)
        s.status = "archived";
    else if (s.remain_percent == 0)
        s.status = "empty";
    else if (s.remain_percent < 20)
        s.status = "low";
    else
        s.status = "active";

    if (j.contains("comment") && j["comment"].is_string())
        s.note = j["comment"].get<std::string>();
    s.created_at = json_string(j, "registered");
    s.updated_at = json_string(j, "last_used");
    if (s.updated_at.empty())
        s.updated_at = json_string(j, "registered");
    return s;
}

void apply_spoolman_preset_match(FilamentSpool& spool)
{
    const std::string brand  = trim_copy(spool.brand);
    const std::string series = trim_copy(spool.series);
    if (brand.empty() || series.empty())
        return;

    PresetBundle* bundle = wxGetApp().preset_bundle;
    if (!bundle)
        return;

    const std::string want = brand + " " + series;
    auto& filaments = bundle->filaments;
    for (auto it = filaments.begin(); it != filaments.end(); ++it) {
        Preset& preset = *it;
        if (filaments.get_preset_base(preset) != &preset)
            continue;
        const std::string alias = trim_copy(filaments.get_preset_alias(preset, true));
        if (alias.empty() || !iequals_ascii(alias, want))
            continue;

        const std::string type = preset.config.get_filament_type();
        if (!type.empty())
            spool.material_type = type;
        if (!preset.filament_id.empty())
            spool.filament_id = preset.filament_id;
        return;
    }
}

SpoolmanClient::SpoolmanClient(SpoolmanEndpoint endpoint)
    : m_endpoint(std::move(endpoint))
{}

SpoolmanClient SpoolmanClient::from_app_config()
{
    return SpoolmanClient(parse_spoolman_url(spoolman_url_config()));
}

bool SpoolmanClient::get_json_sync(const std::string& url, nlohmann::json& out, std::string& error) const
{
    bool ok = false;
    std::string body;
    unsigned status = 0;
    log_spoolman("GET " + url);
    try {
        Http http = Http::get(url);
        http.header("accept", "application/json")
            .timeout_connect(10)
            .timeout_max(20)
            .size_limit(8 * 1024 * 1024);
        apply_auth(http, m_endpoint);
        http.on_complete([&](std::string resp, unsigned st) {
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
        log_spoolman("GET " + url + " failed: " + error);
        return false;
    }

    if (!ok) {
        if (error.empty())
            error = "request failed";
        log_spoolman("GET " + url + " failed: HTTP " + std::to_string(status) + " " + error);
        return false;
    }

    try {
        out = nlohmann::json::parse(body);
    } catch (const std::exception& e) {
        error = e.what();
        log_spoolman("GET " + url + " parse error: " + error);
        return false;
    }
    log_spoolman("GET " + url + " -> " + std::to_string(status)
                 + " (" + std::to_string(body.size()) + " bytes)");
    return true;
}

void SpoolmanClient::get_json_async(const std::string& url,
                                    std::function<void(const nlohmann::json&)> on_ok,
                                    ErrorFn on_error) const
{
    log_spoolman("GET " + url);
    try {
        Http http = Http::get(url);
        http.header("accept", "application/json")
            .timeout_connect(10)
            .timeout_max(20)
            .size_limit(8 * 1024 * 1024);
        apply_auth(http, m_endpoint);
        http.on_complete([on_ok, on_error, url](std::string body, unsigned status) {
                log_spoolman("GET " + url + " -> " + std::to_string(status)
                             + " (" + std::to_string(body.size()) + " bytes)");
                try {
                    on_ok(nlohmann::json::parse(body));
                } catch (const std::exception& e) {
                    log_spoolman("GET " + url + " parse error: " + e.what());
                    if (on_error)
                        on_error(-1, e.what());
                }
            })
            .on_error([on_error, url](std::string body, std::string err, unsigned status) {
                const std::string msg = err.empty() ? body : err;
                const std::string out = msg.empty() ? "network request failed" : msg;
                log_spoolman("GET " + url + " failed: HTTP " + std::to_string(status) + " " + out);
                if (on_error)
                    on_error(static_cast<int>(status), out);
            })
            .perform();
    } catch (const std::exception& e) {
        log_spoolman("GET " + url + " failed: " + e.what());
        if (on_error)
            on_error(-1, e.what());
    }
}

bool SpoolmanClient::fetch_spools_sync(std::vector<FilamentSpool>& out, std::string& error) const
{
    if (!m_endpoint.valid()) {
        error = "spoolman_url is empty";
        return false;
    }
    nlohmann::json json;
    if (!get_json_sync(m_endpoint.base_url + "/spool", json, error))
        return false;
    if (!json.is_array()) {
        error = "unexpected Spoolman /spool response";
        return false;
    }
    out.clear();
    out.reserve(json.size());
    for (const auto& item : json) {
        FilamentSpool spool = spool_from_spoolman_json(item);
        if (!spool.spool_id.empty())
            out.push_back(std::move(spool));
    }
    return true;
}

bool SpoolmanClient::fetch_spool_sync(int spool_id, FilamentSpool& out, std::string& error) const
{
    if (!m_endpoint.valid()) {
        error = "spoolman_url is empty";
        return false;
    }
    if (spool_id <= 0) {
        error = "invalid spool id";
        return false;
    }
    nlohmann::json json;
    if (!get_json_sync(m_endpoint.base_url + "/spool/" + std::to_string(spool_id), json, error))
        return false;
    out = spool_from_spoolman_json(json);
    if (out.spool_id.empty()) {
        error = "spool response missing id";
        return false;
    }
    return true;
}

void SpoolmanClient::list_spools(ListOkFn on_ok, ErrorFn on_error) const
{
    if (!m_endpoint.valid()) {
        if (on_error)
            on_error(-1, "spoolman_url is empty");
        return;
    }
    get_json_async(m_endpoint.base_url + "/spool",
        [on_ok, on_error](const nlohmann::json& json) {
            if (!json.is_array()) {
                if (on_error)
                    on_error(-1, "unexpected Spoolman /spool response");
                return;
            }
            std::vector<FilamentSpool> list;
            list.reserve(json.size());
            for (const auto& item : json) {
                FilamentSpool spool = spool_from_spoolman_json(item);
                if (!spool.spool_id.empty())
                    list.push_back(std::move(spool));
            }
            if (on_ok)
                on_ok(std::move(list));
        },
        on_error);
}

void SpoolmanClient::get_spool(int spool_id, SpoolOkFn on_ok, ErrorFn on_error) const
{
    if (!m_endpoint.valid()) {
        if (on_error)
            on_error(-1, "spoolman_url is empty");
        return;
    }
    get_json_async(m_endpoint.base_url + "/spool/" + std::to_string(spool_id),
        [on_ok, on_error](const nlohmann::json& json) {
            FilamentSpool spool = spool_from_spoolman_json(json);
            if (spool.spool_id.empty()) {
                if (on_error)
                    on_error(-1, "spool response missing id");
                return;
            }
            if (on_ok)
                on_ok(std::move(spool));
        },
        on_error);
}

}} // namespace Slic3r::GUI
