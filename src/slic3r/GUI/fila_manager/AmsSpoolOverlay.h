#ifndef slic3r_GUI_AmsSpoolOverlay_h_
#define slic3r_GUI_AmsSpoolOverlay_h_

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <string>

#include "wgtFilaManagerStore.h"

namespace Slic3r {
class MachineObject;
class DevAmsTray;
}

namespace Slic3r { namespace GUI {

class AmsSpoolOverlay {
public:
    explicit AmsSpoolOverlay(wgtFilaManagerStore* store);
    ~AmsSpoolOverlay();

    // Called on the UI thread after parse_json and before fila_manager sync.
    void on_device_update(MachineObject* obj);

    void apply(MachineObject* obj) const;
    bool has_slot_data() const { return m_has_data; }

private:
    struct CachedSlot {
        int            spool_id = -1;
        FilamentSpool  spool;
        bool           has_spool = false;
    };

    bool should_refresh(MachineObject* obj);
    void request_refresh();
    void apply_tray(DevAmsTray& tray, const CachedSlot& slot) const;
    void remember_tray_exists(MachineObject* obj);
    bool tray_exists_changed(MachineObject* obj) const;
    void apply_refresh_to_store();

    wgtFilaManagerStore* m_store = nullptr;
    std::shared_ptr<std::atomic<bool>> m_alive;

    CachedSlot m_trays[4];
    CachedSlot m_external;
    bool       m_has_data  = false;
    bool       m_inflight  = false;
    bool       m_refresh_again = false;

    int  m_last_layer = -1;
    bool m_have_layer = false;

    std::map<std::string, bool> m_prev_exists;
    std::chrono::steady_clock::time_point m_last_success{};
    std::chrono::steady_clock::time_point m_last_attempt{};
    bool m_have_success = false;
    bool m_have_attempt = false;
};

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_AmsSpoolOverlay_h_
