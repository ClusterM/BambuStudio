#ifndef slic3r_GUI_SpoolmanClient_h_
#define slic3r_GUI_SpoolmanClient_h_

#include <functional>
#include <string>
#include <vector>

#include "wgtFilaManagerStore.h"

namespace Slic3r { namespace GUI {

struct SpoolmanEndpoint {
    std::string base_url;
    std::string user;
    std::string password;

    bool has_auth() const { return !user.empty() || !password.empty(); }
    bool valid() const { return !base_url.empty(); }
};

SpoolmanEndpoint parse_spoolman_url(const std::string& raw_url);
FilamentSpool    spool_from_spoolman_json(const nlohmann::json& j);
int              remain_percent_from_weights(double remaining_weight, double initial_weight);

class SpoolmanClient {
public:
    using ListOkFn  = std::function<void(std::vector<FilamentSpool>)>;
    using SpoolOkFn = std::function<void(FilamentSpool)>;
    using ErrorFn   = std::function<void(int code, const std::string& error)>;

    explicit SpoolmanClient(SpoolmanEndpoint endpoint = {});

    static SpoolmanClient from_app_config();

    bool fetch_spools_sync(std::vector<FilamentSpool>& out, std::string& error) const;
    bool fetch_spool_sync(int spool_id, FilamentSpool& out, std::string& error) const;

    void list_spools(ListOkFn on_ok, ErrorFn on_error) const;
    void get_spool(int spool_id, SpoolOkFn on_ok, ErrorFn on_error) const;

    const SpoolmanEndpoint& endpoint() const { return m_endpoint; }

private:
    bool get_json_sync(const std::string& url, nlohmann::json& out, std::string& error) const;
    void get_json_async(const std::string& url,
                        std::function<void(const nlohmann::json&)> on_ok,
                        ErrorFn on_error) const;

    SpoolmanEndpoint m_endpoint;
};

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_SpoolmanClient_h_
