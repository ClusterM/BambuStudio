#include "AmsSpoolOverlay.h"
#include "HomeAssistantClient.h"
#include "SpoolmanClient.h"
#include "wgtFilaManagerFeature.h"
#include "wgtFilaManagerSync.h"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/DeviceWeb/DeviceWebPage.hpp"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/DeviceManager.hpp"

#include <cmath>

#include <wx/app.h>
#include <boost/log/trivial.hpp>

#include <set>
#include <thread>
#include <unordered_map>

namespace Slic3r { namespace GUI {

namespace {

constexpr auto kRefreshInterval = std::chrono::seconds(60);
constexpr auto kRetryInterval   = std::chrono::seconds(5);

std::string tray_key(const std::string& dev_id, const std::string& ams_id, const std::string& slot_id)
{
    return dev_id + ":" + ams_id + ":" + slot_id;
}

int parse_int_id(const std::string& id)
{
    try {
        return std::stoi(id);
    } catch (...) {
        return -1;
    }
}

// AMS / AMS 2 Pro: ams id 0..3, tray 0..3 → a1..d4.
// AMS HT: ams id 128..135 → ht_a..ht_h (one tray).
std::string ams_slot_code(int ams_id, int slot_id, DevAmsType type)
{
    if (type == DevAmsType::N3S || (ams_id >= 128 && ams_id <= 135)) {
        if (ams_id < 128 || ams_id > 135)
            return {};
        return std::string("ht_") + static_cast<char>('a' + (ams_id - 128));
    }
    if (ams_id < 0 || ams_id > 3 || slot_id < 0 || slot_id > 3)
        return {};
    return std::string(1, static_cast<char>('a' + ams_id)) + std::to_string(slot_id + 1);
}

// Studio's MAIN extruder (id 0, vir_slot 255) is the physical right nozzle.
// DEPUTY (id 1, vir_slot 254) is the physical left nozzle. The primary
// external spool is the left one; external_aux is the right one.
// A single-extruder printer only reports vir_slot 255 — that is "external".
std::string external_slot_code(const std::string& vt_id, int extruder_count)
{
    if (extruder_count <= 1)
        return "external";
    if (vt_id == VIRTUAL_AMS_DEPUTY_ID_STR)
        return "external";
    if (vt_id == VIRTUAL_AMS_MAIN_ID_STR)
        return "external_aux";
    return {};
}

} // namespace

AmsSpoolOverlay::AmsSpoolOverlay(wgtFilaManagerStore* store)
    : m_store(store)
    , m_alive(std::make_shared<std::atomic<bool>>(true))
{}

AmsSpoolOverlay::~AmsSpoolOverlay()
{
    if (m_alive)
        m_alive->store(false);
}

void AmsSpoolOverlay::on_device_update(MachineObject* obj)
{
    if (!obj || !is_ha_overlay_enabled())
        return;

    if (should_refresh(obj))
        request_refresh();

    apply(obj);
    remember_tray_exists(obj);
}

bool AmsSpoolOverlay::should_refresh(MachineObject* obj)
{
    const auto now = std::chrono::steady_clock::now();

    auto want = [this]() {
        if (m_inflight) {
            m_refresh_again = true;
            return false;
        }
        return true;
    };

    if (!m_has_data) {
        if (!m_inflight && (!m_have_attempt || now - m_last_attempt >= kRetryInterval))
            return true;
        if (m_inflight)
            m_refresh_again = true;
    }

    if (tray_exists_changed(obj))
        return want();

    if (m_have_layer) {
        if (obj->curr_layer != m_last_layer) {
            m_last_layer = obj->curr_layer;
            return want();
        }
    } else {
        m_last_layer = obj->curr_layer;
        m_have_layer = true;
    }

    if (m_have_success && now - m_last_success >= kRefreshInterval)
        return want();

    return false;
}

void AmsSpoolOverlay::remember_tray_exists(MachineObject* obj)
{
    m_prev_exists.clear();
    auto fila = obj->GetFilaSystem();
    if (fila) {
        for (auto& [ams_id, ams] : fila->GetAmsList()) {
            if (!ams)
                continue;
            for (auto& [slot_id, tray] : ams->GetTrays()) {
                if (tray)
                    m_prev_exists[tray_key(obj->get_dev_id(), ams_id, slot_id)] = tray->is_exists;
            }
        }
    }
    for (auto& vt : obj->vt_slot)
        m_prev_exists[tray_key(obj->get_dev_id(), "ext", vt.id)] = vt.is_exists;
}

bool AmsSpoolOverlay::tray_exists_changed(MachineObject* obj) const
{
    auto fila = obj->GetFilaSystem();
    if (fila) {
        for (auto& [ams_id, ams] : fila->GetAmsList()) {
            if (!ams)
                continue;
            for (auto& [slot_id, tray] : ams->GetTrays()) {
                if (!tray)
                    continue;
                const auto key = tray_key(obj->get_dev_id(), ams_id, slot_id);
                auto it = m_prev_exists.find(key);
                const bool prev = (it == m_prev_exists.end()) ? false : it->second;
                if (prev != tray->is_exists)
                    return true;
            }
        }
    }
    for (auto& vt : obj->vt_slot) {
        const auto key = tray_key(obj->get_dev_id(), "ext", vt.id);
        auto it = m_prev_exists.find(key);
        const bool prev = (it == m_prev_exists.end()) ? false : it->second;
        if (prev != vt.is_exists)
            return true;
    }
    return false;
}

void AmsSpoolOverlay::request_refresh()
{
    if (m_inflight)
        return;

    m_inflight     = true;
    m_have_attempt = true;
    m_last_attempt = std::chrono::steady_clock::now();

    auto alive = m_alive;
    std::thread([this, alive]() {
        HomeAssistantClient ha = HomeAssistantClient::from_app_config();
        SpoolmanClient      sm = SpoolmanClient::from_app_config();

        HaSlotIds slots;
        std::string error;
        if (!ha.fetch_slot_ids_sync(slots, error)) {
            BOOST_LOG_TRIVIAL(warning) << "[AmsSpoolOverlay] HA fetch failed: " << error;
            wxTheApp->CallAfter([this, alive]() {
                if (!alive->load())
                    return;
                m_inflight = false;
                if (m_refresh_again) {
                    m_refresh_again = false;
                    request_refresh();
                }
            });
            return;
        }

        std::set<int> ids;
        for (const auto& [code, spool_id] : slots) {
            (void) code;
            if (spool_id > 0)
                ids.insert(spool_id);
        }

        std::unordered_map<int, FilamentSpool> fetched;
        for (int id : ids) {
            FilamentSpool spool;
            std::string   err;
            if (sm.fetch_spool_sync(id, spool, err))
                fetched.emplace(id, std::move(spool));
            else
                BOOST_LOG_TRIVIAL(warning) << "[AmsSpoolOverlay] Spoolman fetch id=" << id
                                           << " failed: " << err;
        }

        wxTheApp->CallAfter([this, alive, slots, fetched]() {
            if (!alive->load())
                return;

            m_slots.clear();
            for (const auto& [code, spool_id] : slots) {
                CachedSlot slot;
                slot.spool_id = spool_id;
                auto it = fetched.find(spool_id);
                if (it != fetched.end()) {
                    slot.spool     = it->second;
                    slot.has_spool = true;
                }
                m_slots.emplace(code, std::move(slot));
            }

            m_has_data     = true;
            m_inflight     = false;
            m_have_success = true;
            m_last_success = std::chrono::steady_clock::now();
            const bool again = m_refresh_again;
            m_refresh_again  = false;

            apply_refresh_to_store();

            auto* mgr = wxGetApp().getDeviceManager();
            MachineObject* obj = mgr ? mgr->get_selected_machine() : nullptr;
            if (obj) {
                apply(obj);
                remember_tray_exists(obj);
                if (auto* sync = wxGetApp().fila_manager_sync())
                    sync->sync_all_trays(obj);
                if (wxGetApp().mainframe && wxGetApp().mainframe->web_device())
                    wxGetApp().mainframe->web_device()->NotifyFilamentSessionState();
            }
            if (again)
                request_refresh();
        });
    }).detach();
}

void AmsSpoolOverlay::apply_refresh_to_store()
{
    if (!m_store)
        return;

    auto upsert = [this](const CachedSlot& slot) {
        if (!slot.has_spool || slot.spool_id <= 0)
            return;
        FilamentSpool spool = slot.spool;
        apply_spoolman_preset_match(spool);
        if (m_store->get_spool(spool.spool_id)) {
            if (const FilamentSpool* existing = m_store->get_spool(spool.spool_id)) {
                spool.in_printer   = existing->in_printer;
                spool.dev_id       = existing->dev_id;
                spool.ams_sn       = existing->ams_sn;
                spool.ams_id       = existing->ams_id;
                spool.ams_type     = existing->ams_type;
                spool.slot_id      = existing->slot_id;
                spool.device_name  = existing->device_name;
                spool.bound_dev_id = existing->bound_dev_id;
                spool.bound_ams_id = existing->bound_ams_id;
            }
            m_store->update_spool_if_changed(spool);
        } else {
            m_store->add_spool(spool);
        }
    };

    for (const auto& [code, slot] : m_slots) {
        (void) code;
        upsert(slot);
    }
}

void AmsSpoolOverlay::apply_tray(DevAmsTray& tray, const CachedSlot& slot) const
{
    tray.spoolman_overlay = false;
    if (!tray.is_exists || slot.spool_id <= 0 || !slot.has_spool)
        return;

    tray.uuid             = slot.spool.spool_id;
    tray.remain           = slot.spool.remain_percent;
    tray.remain_g         = static_cast<int>(std::lround(slot.spool.net_weight));
    tray.spoolman_overlay = true;
}

void AmsSpoolOverlay::apply(MachineObject* obj) const
{
    if (!obj || !m_has_data)
        return;

    auto apply_code = [this](DevAmsTray& tray, const std::string& code) {
        auto it = m_slots.find(code);
        if (code.empty() || it == m_slots.end()) {
            tray.spoolman_overlay = false;
            return;
        }
        apply_tray(tray, it->second);
    };

    auto fila = obj->GetFilaSystem();
    if (fila) {
        for (auto& [ams_id, ams] : fila->GetAmsList()) {
            if (!ams)
                continue;
            const int ams_index = parse_int_id(ams_id);
            for (auto& [slot_id, tray] : ams->GetTrays()) {
                if (!tray)
                    continue;
                const std::string code = ams_slot_code(ams_index, parse_int_id(slot_id), ams->GetAmsType());
                apply_code(*tray, code);
            }
        }
    }

    int extruder_count = 1;
    if (obj->GetExtderSystem())
        extruder_count = obj->GetExtderSystem()->GetTotalExtderCount();
    for (auto& vt : obj->vt_slot)
        apply_code(vt, external_slot_code(vt.id, extruder_count));
}

}} // namespace Slic3r::GUI
