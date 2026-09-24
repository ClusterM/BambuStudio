#ifndef slic3r_GUI_HomeAssistantClient_h_
#define slic3r_GUI_HomeAssistantClient_h_

#include <array>
#include <functional>
#include <string>

namespace Slic3r { namespace GUI {

struct HaSlotIds {
    // 1-based AMS trays 1..4, then external. -1 means empty / unknown.
    std::array<int, 4> trays{{-1, -1, -1, -1}};
    int                external = -1;
};

class HomeAssistantClient {
public:
    using SlotsOkFn = std::function<void(HaSlotIds)>;
    using ErrorFn   = std::function<void(int code, const std::string& error)>;

    HomeAssistantClient(std::string base_url, std::string token);

    static HomeAssistantClient from_app_config();

    static int parse_spool_id_state(const std::string& state);

    bool fetch_slot_ids_sync(HaSlotIds& out, std::string& error) const;
    bool fetch_entity_state_sync(const std::string& entity_id, std::string& state, std::string& error) const;

    const std::string& base_url() const { return m_base_url; }

private:
    std::string state_url(const std::string& entity_id) const;

    std::string m_base_url;
    std::string m_token;
};

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_HomeAssistantClient_h_
