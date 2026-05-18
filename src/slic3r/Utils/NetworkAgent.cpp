#include <stdio.h>
#include <stdlib.h>
#if defined(_MSC_VER) || defined(_WIN32)
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

#include <boost/log/trivial.hpp>
#include <boost/filesystem.hpp>
#include "libslic3r/Utils.hpp"
#include "slic3r/Utils/BBLUtil.hpp"
#include "NetworkAgent.hpp"

#include "slic3r/Utils/FileTransferUtils.hpp"
#include "slic3r/Utils/CertificateVerify.hpp"

using namespace BBL;

namespace Slic3r {

#if defined(_MSC_VER) || defined(_WIN32)
static std::string win32_error_string(DWORD code)
{
    char* msg = nullptr;
    DWORD len = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<char*>(&msg), 0, nullptr);
    if (len && msg) {
        std::string result(msg, len);
        LocalFree(msg);
        // strip trailing \r\n
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
            result.pop_back();
        return result + " (code " + std::to_string(code) + ")";
    }
    return "unknown error (code " + std::to_string(code) + ")";
}
#endif

// Checks whether a path exists on disk and returns a descriptive string for logging.
static std::string file_existence_note(const std::string& path)
{
    boost::system::error_code ec;
    if (boost::filesystem::exists(path, ec))
        return "file exists but has no valid signature";
    if (ec)
        return "file existence check failed: " + ec.message();
    return "file does not exist on disk";
}

namespace {

constexpr size_t CLOUD_LOG_BODY_MAX = 1024;

static std::string cloud_trunc_body(const std::string &s)
{
    if (s.size() <= CLOUD_LOG_BODY_MAX)
        return s;
    return s.substr(0, CLOUD_LOG_BODY_MAX) + "...<truncated len=" + std::to_string(s.size()) + ">";
}

static std::string cloud_format_dev_list(const std::vector<std::string> &dev_list, size_t max_show = 6)
{
    std::string o = "count=" + std::to_string(dev_list.size()) + " [";
    const size_t cap = dev_list.size() < max_show ? dev_list.size() : max_show;
    for (size_t i = 0; i < cap; ++i) {
        if (i) o += ',';
        o += dev_list[i];
    }
    if (dev_list.size() > max_show)
        o += ",...";
    o += ']';
    return o;
}

static std::string cloud_header_keys(const std::map<std::string, std::string> &h)
{
    std::string o;
    for (const auto &kv : h) {
        o += kv.first;
        o += ';';
    }
    return "header_key_count=" + std::to_string(h.size()) + " keys=" + o;
}

} // namespace

#define BAMBU_SOURCE_LIBRARY "BambuSource"

#if defined(_MSC_VER) || defined(_WIN32)
static HMODULE networking_module = NULL;
static HMODULE source_module = NULL;
#else
static void* networking_module = NULL;
static void* source_module = NULL;
#endif


func_check_debug_consistent         NetworkAgent::check_debug_consistent_ptr = nullptr;
func_get_version                    NetworkAgent::get_version_ptr = nullptr;
func_create_agent                   NetworkAgent::create_agent_ptr = nullptr;
func_destroy_agent                  NetworkAgent::destroy_agent_ptr = nullptr;
func_init_log                       NetworkAgent::init_log_ptr = nullptr;
func_set_config_dir                 NetworkAgent::set_config_dir_ptr = nullptr;
func_set_cert_file                  NetworkAgent::set_cert_file_ptr = nullptr;
func_set_country_code               NetworkAgent::set_country_code_ptr = nullptr;
func_start                          NetworkAgent::start_ptr = nullptr;
func_set_on_ssdp_msg_fn             NetworkAgent::set_on_ssdp_msg_fn_ptr = nullptr;
func_set_on_user_login_fn           NetworkAgent::set_on_user_login_fn_ptr = nullptr;
func_set_on_printer_connected_fn    NetworkAgent::set_on_printer_connected_fn_ptr = nullptr;
func_set_on_server_connected_fn     NetworkAgent::set_on_server_connected_fn_ptr = nullptr;
func_set_on_http_error_fn           NetworkAgent::set_on_http_error_fn_ptr = nullptr;
func_set_get_country_code_fn        NetworkAgent::set_get_country_code_fn_ptr = nullptr;
func_set_on_subscribe_failure_fn    NetworkAgent::set_on_subscribe_failure_fn_ptr = nullptr;
func_set_on_message_fn              NetworkAgent::set_on_message_fn_ptr = nullptr;
func_set_on_user_message_fn         NetworkAgent::set_on_user_message_fn_ptr = nullptr;
func_set_on_local_connect_fn        NetworkAgent::set_on_local_connect_fn_ptr = nullptr;
func_set_on_local_message_fn        NetworkAgent::set_on_local_message_fn_ptr = nullptr;
func_set_queue_on_main_fn           NetworkAgent::set_queue_on_main_fn_ptr = nullptr;
func_connect_server                 NetworkAgent::connect_server_ptr = nullptr;
func_is_server_connected            NetworkAgent::is_server_connected_ptr = nullptr;
func_refresh_connection             NetworkAgent::refresh_connection_ptr = nullptr;
func_start_subscribe                NetworkAgent::start_subscribe_ptr = nullptr;
func_stop_subscribe                 NetworkAgent::stop_subscribe_ptr = nullptr;
func_add_subscribe                  NetworkAgent::add_subscribe_ptr = nullptr;
func_del_subscribe                  NetworkAgent::del_subscribe_ptr = nullptr;
func_enable_multi_machine           NetworkAgent::enable_multi_machine_ptr = nullptr;
func_send_message                   NetworkAgent::send_message_ptr = nullptr;
func_connect_printer                NetworkAgent::connect_printer_ptr = nullptr;
func_disconnect_printer             NetworkAgent::disconnect_printer_ptr = nullptr;
func_send_message_to_printer        NetworkAgent::send_message_to_printer_ptr = nullptr;
func_check_cert                     NetworkAgent::check_cert_ptr = nullptr;
func_install_device_cert            NetworkAgent::install_device_cert_ptr = nullptr;
func_start_discovery                NetworkAgent::start_discovery_ptr = nullptr;
func_change_user                    NetworkAgent::change_user_ptr = nullptr;
func_is_user_login                  NetworkAgent::is_user_login_ptr = nullptr;
func_user_logout                    NetworkAgent::user_logout_ptr = nullptr;
func_get_user_id                    NetworkAgent::get_user_id_ptr = nullptr;
func_get_user_name                  NetworkAgent::get_user_name_ptr = nullptr;
func_get_user_avatar                NetworkAgent::get_user_avatar_ptr = nullptr;
func_get_user_nickanme              NetworkAgent::get_user_nickanme_ptr = nullptr;
func_build_login_cmd                NetworkAgent::build_login_cmd_ptr = nullptr;
func_build_logout_cmd               NetworkAgent::build_logout_cmd_ptr = nullptr;
func_build_login_info               NetworkAgent::build_login_info_ptr = nullptr;
func_ping_bind                      NetworkAgent::ping_bind_ptr = nullptr;
func_bind_detect                    NetworkAgent::bind_detect_ptr = nullptr;
func_report_consent                 NetworkAgent::report_consent_ptr = nullptr;
func_set_server_callback            NetworkAgent::set_server_callback_ptr = nullptr;
func_bind                           NetworkAgent::bind_ptr = nullptr;
func_unbind                         NetworkAgent::unbind_ptr = nullptr;
func_get_bambulab_host              NetworkAgent::get_bambulab_host_ptr = nullptr;
func_get_user_selected_machine      NetworkAgent::get_user_selected_machine_ptr = nullptr;
func_set_user_selected_machine      NetworkAgent::set_user_selected_machine_ptr = nullptr;
func_start_print                    NetworkAgent::start_print_ptr = nullptr;
func_start_local_print_with_record  NetworkAgent::start_local_print_with_record_ptr = nullptr;
func_start_send_gcode_to_sdcard     NetworkAgent::start_send_gcode_to_sdcard_ptr = nullptr;
func_start_local_print              NetworkAgent::start_local_print_ptr = nullptr;
func_start_sdcard_print             NetworkAgent::start_sdcard_print_ptr = nullptr;
func_get_user_presets               NetworkAgent::get_user_presets_ptr = nullptr;
func_request_setting_id             NetworkAgent::request_setting_id_ptr = nullptr;
func_put_setting                    NetworkAgent::put_setting_ptr = nullptr;
func_get_setting_list               NetworkAgent::get_setting_list_ptr = nullptr;
func_get_setting_list2              NetworkAgent::get_setting_list2_ptr = nullptr;
func_delete_setting                 NetworkAgent::delete_setting_ptr = nullptr;
func_get_studio_info_url            NetworkAgent::get_studio_info_url_ptr = nullptr;
func_set_extra_http_header          NetworkAgent::set_extra_http_header_ptr = nullptr;
func_get_my_message                 NetworkAgent::get_my_message_ptr = nullptr;
func_check_user_task_report         NetworkAgent::check_user_task_report_ptr = nullptr;
func_get_user_print_info            NetworkAgent::get_user_print_info_ptr = nullptr;
func_get_user_tasks                 NetworkAgent::get_user_tasks_ptr = nullptr;
func_get_filament_spools            NetworkAgent::get_filament_spools_ptr = nullptr;
func_create_filament_spool          NetworkAgent::create_filament_spool_ptr = nullptr;
func_update_filament_spool          NetworkAgent::update_filament_spool_ptr = nullptr;
func_delete_filament_spools         NetworkAgent::delete_filament_spools_ptr = nullptr;
func_get_filament_config            NetworkAgent::get_filament_config_ptr = nullptr;
func_get_printer_firmware           NetworkAgent::get_printer_firmware_ptr = nullptr;
func_get_task_plate_index           NetworkAgent::get_task_plate_index_ptr = nullptr;
func_get_user_info                  NetworkAgent::get_user_info_ptr = nullptr;
func_request_bind_ticket            NetworkAgent::request_bind_ticket_ptr = nullptr;
func_get_subtask_info               NetworkAgent::get_subtask_info_ptr = nullptr;
func_get_slice_info                 NetworkAgent::get_slice_info_ptr = nullptr;
func_query_bind_status              NetworkAgent::query_bind_status_ptr = nullptr;
func_modify_printer_name            NetworkAgent::modify_printer_name_ptr = nullptr;
func_get_camera_url                 NetworkAgent::get_camera_url_ptr = nullptr;
func_get_camera_url_for_golive      NetworkAgent::get_camera_url_for_golive_ptr = nullptr;
func_get_design_staffpick           NetworkAgent::get_design_staffpick_ptr = nullptr;
func_start_pubilsh                  NetworkAgent::start_publish_ptr = nullptr;
func_get_model_publish_url          NetworkAgent::get_model_publish_url_ptr = nullptr;
func_get_model_mall_home_url        NetworkAgent::get_model_mall_home_url_ptr = nullptr;
func_get_model_mall_detail_url      NetworkAgent::get_model_mall_detail_url_ptr = nullptr;
func_get_subtask                    NetworkAgent::get_subtask_ptr = nullptr;
func_get_my_profile                 NetworkAgent::get_my_profile_ptr = nullptr;
func_get_my_token                   NetworkAgent::get_my_token_ptr = nullptr;
func_track_enable                   NetworkAgent::track_enable_ptr = nullptr;
func_track_remove_files             NetworkAgent::track_remove_files_ptr = nullptr;
func_track_event                    NetworkAgent::track_event_ptr = nullptr;
func_track_header                   NetworkAgent::track_header_ptr = nullptr;
func_track_update_property          NetworkAgent::track_update_property_ptr = nullptr;
func_track_get_property             NetworkAgent::track_get_property_ptr = nullptr;
func_put_model_mall_rating_url      NetworkAgent::put_model_mall_rating_url_ptr = nullptr;
func_get_oss_config                 NetworkAgent::get_oss_config_ptr = nullptr;
func_put_rating_picture_oss         NetworkAgent::put_rating_picture_oss_ptr = nullptr;
func_get_model_mall_rating_result   NetworkAgent::get_model_mall_rating_result_ptr  = nullptr;

func_get_mw_user_preference         NetworkAgent::get_mw_user_preference_ptr = nullptr;
func_get_mw_user_4ulist             NetworkAgent::get_mw_user_4ulist_ptr     = nullptr;
func_get_hms_snapshot               NetworkAgent::get_hms_snapshot_ptr       = nullptr;

NetworkAgent::NetworkAgent(std::string log_dir)
{
    if (create_agent_ptr) {
        network_agent = create_agent_ptr(log_dir);
    }
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", line %1%, network_agent=%2%, create_agent_ptr=%3%")%__LINE__ %network_agent %create_agent_ptr;
}

NetworkAgent::~NetworkAgent()
{
    int ret = 0;
    if (network_agent && destroy_agent_ptr) {
        ret = destroy_agent_ptr(network_agent);
    }
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", line %1%, network_agent=%2%, destroy_agent_ptr=%3%, ret %4%")%__LINE__ %network_agent %destroy_agent_ptr %ret;
}

std::string NetworkAgent::get_libpath_in_current_directory(std::string library_name)
{
    std::string lib_path;
#if defined(_MSC_VER) || defined(_WIN32)
    wchar_t file_name[512];
    DWORD ret = GetModuleFileNameW(NULL, file_name, 512);
    if (!ret) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", GetModuleFileNameW return error, can not Load Library for %1%") %library_name;
        return lib_path;
    }
    int size_needed = ::WideCharToMultiByte(0, 0, file_name, wcslen(file_name), nullptr, 0, nullptr, nullptr);
    std::string file_name_string(size_needed, 0);
    ::WideCharToMultiByte(0, 0, file_name, wcslen(file_name), file_name_string.data(), size_needed, nullptr, nullptr);

    std::size_t found = file_name_string.find("bambu-studio.exe");
    if (found == (file_name_string.size() - 16)) {
        lib_path = library_name + ".dll";
        lib_path = file_name_string.replace(found, 16, lib_path);
    }
#else
#endif
    return lib_path;
}


// Returns a human-readable reason why IsSamePublisher passed or failed.
static std::string publisher_match_reason(const SignerSummary& a, const SignerSummary& b)
{
    if (!a.team_id.empty() && a.team_id == b.team_id)
        return "team_id match: " + a.team_id;
    if (a.spki_sha256 == b.spki_sha256)
        return "spki_sha256 match";
    if (a.cert_sha256 == b.cert_sha256)
        return "cert_sha256 match";
    return "no match (team_id='" + a.team_id + "'/'" + b.team_id + "', spki differs, cert differs)"
           + " self:" + a.as_print() + " module:" + b.as_print();
}

int NetworkAgent::initialize_network_module(bool using_backup, bool validate_cert)
{
    //int ret = -1;
    std::string library;
    std::string data_dir_str = data_dir();
    boost::filesystem::path data_dir_path(data_dir_str);
    auto plugin_folder = data_dir_path / "plugins";

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__
        << boost::format(": using_backup=%1%, validate_cert=%2%, data_dir=%3%")
           % using_backup % validate_cert % data_dir_str;

    if (using_backup) {
        plugin_folder = plugin_folder/"backup";
    }
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": plugin_folder=" << plugin_folder.string();

    std::optional<SignerSummary> self_cert_summary, module_cert_summary;
    if (validate_cert) {
        self_cert_summary = SummarizeSelf();
        if (self_cert_summary)
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": self cert ok -" << self_cert_summary->as_print();
        else
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": self cert not found - cert validation will be skipped";
    } else {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": cert validation disabled (ignore_module_cert=1)";
    }

    //first load the library
#if defined(_MSC_VER) || defined(_WIN32)
    library = plugin_folder.string() + "\\" + std::string(BAMBU_NETWORK_LIBRARY) + ".dll";
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": trying to load library: " << library;
    wchar_t lib_wstr[128];
    memset(lib_wstr, 0, sizeof(lib_wstr));
    ::MultiByteToWideChar(CP_UTF8, NULL, library.c_str(), strlen(library.c_str())+1, lib_wstr, sizeof(lib_wstr) / sizeof(lib_wstr[0]));
    if (self_cert_summary) {
        module_cert_summary = SummarizeModule(library);
        if (module_cert_summary) {
            std::string reason = publisher_match_reason(*self_cert_summary, *module_cert_summary);
            if (IsSamePublisher(*self_cert_summary, *module_cert_summary)) {
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": cert check passed - " << reason;
                networking_module = LoadLibrary(lib_wstr);
                if (!networking_module) {
                    DWORD err = GetLastError();
                    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": LoadLibrary failed: " << win32_error_string(err);
                }
            } else {
                BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": cert check failed - " << reason;
            }
        } else {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": cannot read certificate from '" << library
                << "': " << file_existence_note(library);
        }
    } else {
        networking_module = LoadLibrary(lib_wstr);
        if (!networking_module) {
            DWORD err = GetLastError();
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": LoadLibrary failed: " << win32_error_string(err);
        }
    }
    if (!networking_module) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": plugin dir load failed, trying current exe directory";

        std::string library_path = get_libpath_in_current_directory(std::string(BAMBU_NETWORK_LIBRARY));
        if (library_path.empty()) {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": cannot resolve exe-directory path for " << BAMBU_NETWORK_LIBRARY;
            return -1;
        }
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": trying to load library: " << library_path;
        memset(lib_wstr, 0, sizeof(lib_wstr));
        ::MultiByteToWideChar(CP_UTF8, NULL, library_path.c_str(), strlen(library_path.c_str())+1, lib_wstr, sizeof(lib_wstr) / sizeof(lib_wstr[0]));
        if (self_cert_summary) {
            module_cert_summary = SummarizeModule(library_path);
            if (module_cert_summary) {
                std::string reason = publisher_match_reason(*self_cert_summary, *module_cert_summary);
                if (IsSamePublisher(*self_cert_summary, *module_cert_summary)) {
                    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": cert check passed - " << reason;
                    networking_module = LoadLibrary(lib_wstr);
                    if (!networking_module) {
                        DWORD err = GetLastError();
                        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": LoadLibrary failed: " << win32_error_string(err);
                    }
                } else {
                    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": cert check failed - " << reason;
                }
            } else {
                BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": cannot read certificate from '" << library_path
                    << "': " << file_existence_note(library_path);
            }
        } else {
            networking_module = LoadLibrary(lib_wstr);
            if (!networking_module) {
                DWORD err = GetLastError();
                BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": LoadLibrary failed: " << win32_error_string(err);
            }
        }
    }
#else
    #if defined(__WXMAC__)
    library = plugin_folder.string() + "/" + std::string("lib") + std::string(BAMBU_NETWORK_LIBRARY) + ".dylib";
    #else
    library = plugin_folder.string() + "/" + std::string("lib") + std::string(BAMBU_NETWORK_LIBRARY) + ".so";
    #endif
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": trying to load library: " << library;
    module_cert_summary = SummarizeModule(library);
    if (self_cert_summary) {
        if (module_cert_summary) {
            std::string reason = publisher_match_reason(*self_cert_summary, *module_cert_summary);
            if (IsSamePublisher(*self_cert_summary, *module_cert_summary)) {
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": cert check passed - " << reason;
                networking_module = dlopen(library.c_str(), RTLD_LAZY);
            } else {
                BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": cert check failed - " << reason;
            }
        } else {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": cannot read certificate from '" << library
                << "': " << file_existence_note(library);
        }
    } else {
        networking_module = dlopen(library.c_str(), RTLD_LAZY);
    }
    if (!networking_module) {
        char* dll_error = dlerror();
        std::string err = dll_error ? std::string(dll_error) : std::string("(null)");
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": dlopen failed: " << err;
    } else {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": dlopen succeeded, handle=" << networking_module;
    }
#endif

    if (!networking_module) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
            << boost::format(": FAILED to load library, using_backup=%1%") % using_backup;
        return -1;
    }
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__
        << boost::format(": library loaded successfully, using_backup=%1%, handle=%2%") % using_backup % networking_module;

    // load file transfer interface
    InitFTModule(networking_module);

    //load the functions
    check_debug_consistent_ptr        =  reinterpret_cast<func_check_debug_consistent>(get_network_function("bambu_network_check_debug_consistent"));
    get_version_ptr                   =  reinterpret_cast<func_get_version>(get_network_function("bambu_network_get_version"));
    create_agent_ptr                  =  reinterpret_cast<func_create_agent>(get_network_function("bambu_network_create_agent"));
    destroy_agent_ptr                 =  reinterpret_cast<func_destroy_agent>(get_network_function("bambu_network_destroy_agent"));
    init_log_ptr                      =  reinterpret_cast<func_init_log>(get_network_function("bambu_network_init_log"));
    set_config_dir_ptr                =  reinterpret_cast<func_set_config_dir>(get_network_function("bambu_network_set_config_dir"));
    set_cert_file_ptr                 =  reinterpret_cast<func_set_cert_file>(get_network_function("bambu_network_set_cert_file"));
    set_country_code_ptr              =  reinterpret_cast<func_set_country_code>(get_network_function("bambu_network_set_country_code"));
    start_ptr                         =  reinterpret_cast<func_start>(get_network_function("bambu_network_start"));
    set_on_ssdp_msg_fn_ptr            =  reinterpret_cast<func_set_on_ssdp_msg_fn>(get_network_function("bambu_network_set_on_ssdp_msg_fn"));
    set_on_user_login_fn_ptr          =  reinterpret_cast<func_set_on_user_login_fn>(get_network_function("bambu_network_set_on_user_login_fn"));
    set_on_printer_connected_fn_ptr   =  reinterpret_cast<func_set_on_printer_connected_fn>(get_network_function("bambu_network_set_on_printer_connected_fn"));
    set_on_server_connected_fn_ptr    =  reinterpret_cast<func_set_on_server_connected_fn>(get_network_function("bambu_network_set_on_server_connected_fn"));
    set_on_http_error_fn_ptr          =  reinterpret_cast<func_set_on_http_error_fn>(get_network_function("bambu_network_set_on_http_error_fn"));
    set_get_country_code_fn_ptr       =  reinterpret_cast<func_set_get_country_code_fn>(get_network_function("bambu_network_set_get_country_code_fn"));
    set_on_subscribe_failure_fn_ptr   =  reinterpret_cast<func_set_on_subscribe_failure_fn>(get_network_function("bambu_network_set_on_subscribe_failure_fn"));
    set_on_message_fn_ptr             =  reinterpret_cast<func_set_on_message_fn>(get_network_function("bambu_network_set_on_message_fn"));
    set_on_user_message_fn_ptr        =  reinterpret_cast<func_set_on_user_message_fn>(get_network_function("bambu_network_set_on_user_message_fn"));
    set_on_local_connect_fn_ptr       =  reinterpret_cast<func_set_on_local_connect_fn>(get_network_function("bambu_network_set_on_local_connect_fn"));
    set_on_local_message_fn_ptr       =  reinterpret_cast<func_set_on_local_message_fn>(get_network_function("bambu_network_set_on_local_message_fn"));
    set_queue_on_main_fn_ptr          = reinterpret_cast<func_set_queue_on_main_fn>(get_network_function("bambu_network_set_queue_on_main_fn"));
    connect_server_ptr                =  reinterpret_cast<func_connect_server>(get_network_function("bambu_network_connect_server"));
    is_server_connected_ptr           =  reinterpret_cast<func_is_server_connected>(get_network_function("bambu_network_is_server_connected"));
    refresh_connection_ptr            =  reinterpret_cast<func_refresh_connection>(get_network_function("bambu_network_refresh_connection"));
    start_subscribe_ptr               =  reinterpret_cast<func_start_subscribe>(get_network_function("bambu_network_start_subscribe"));
    stop_subscribe_ptr                =  reinterpret_cast<func_stop_subscribe>(get_network_function("bambu_network_stop_subscribe"));
    add_subscribe_ptr                 =  reinterpret_cast<func_add_subscribe>(get_network_function("bambu_network_add_subscribe"));
    del_subscribe_ptr                 =  reinterpret_cast<func_del_subscribe>(get_network_function("bambu_network_del_subscribe"));
    enable_multi_machine_ptr          =  reinterpret_cast<func_enable_multi_machine>(get_network_function("bambu_network_enable_multi_machine"));
    send_message_ptr                  =  reinterpret_cast<func_send_message>(get_network_function("bambu_network_send_message"));
    connect_printer_ptr               =  reinterpret_cast<func_connect_printer>(get_network_function("bambu_network_connect_printer"));
    disconnect_printer_ptr            =  reinterpret_cast<func_disconnect_printer>(get_network_function("bambu_network_disconnect_printer"));
    send_message_to_printer_ptr       =  reinterpret_cast<func_send_message_to_printer>(get_network_function("bambu_network_send_message_to_printer"));
    check_cert_ptr                    =  reinterpret_cast<func_check_cert>(get_network_function("bambu_network_update_cert"));
    install_device_cert_ptr           =  reinterpret_cast<func_install_device_cert>(get_network_function("bambu_network_install_device_cert"));
    start_discovery_ptr               =  reinterpret_cast<func_start_discovery>(get_network_function("bambu_network_start_discovery"));
    change_user_ptr                   =  reinterpret_cast<func_change_user>(get_network_function("bambu_network_change_user"));
    is_user_login_ptr                 =  reinterpret_cast<func_is_user_login>(get_network_function("bambu_network_is_user_login"));
    user_logout_ptr                   =  reinterpret_cast<func_user_logout>(get_network_function("bambu_network_user_logout"));
    get_user_id_ptr                   =  reinterpret_cast<func_get_user_id>(get_network_function("bambu_network_get_user_id"));
    get_user_name_ptr                 =  reinterpret_cast<func_get_user_name>(get_network_function("bambu_network_get_user_name"));
    get_user_avatar_ptr               =  reinterpret_cast<func_get_user_avatar>(get_network_function("bambu_network_get_user_avatar"));
    get_user_nickanme_ptr             =  reinterpret_cast<func_get_user_nickanme>(get_network_function("bambu_network_get_user_nickanme"));
    build_login_cmd_ptr               =  reinterpret_cast<func_build_login_cmd>(get_network_function("bambu_network_build_login_cmd"));
    build_logout_cmd_ptr              =  reinterpret_cast<func_build_logout_cmd>(get_network_function("bambu_network_build_logout_cmd"));
    build_login_info_ptr              =  reinterpret_cast<func_build_login_info>(get_network_function("bambu_network_build_login_info"));
    ping_bind_ptr                     =  reinterpret_cast<func_ping_bind>(get_network_function("bambu_network_ping_bind"));
    bind_detect_ptr                   =  reinterpret_cast<func_bind_detect>(get_network_function("bambu_network_bind_detect"));
    report_consent_ptr                =  reinterpret_cast<func_report_consent>(get_network_function("bambu_network_report_consent"));
    set_server_callback_ptr           =  reinterpret_cast<func_set_server_callback>(get_network_function("bambu_network_set_server_callback"));
    bind_ptr                          =  reinterpret_cast<func_bind>(get_network_function("bambu_network_bind"));
    unbind_ptr                        =  reinterpret_cast<func_unbind>(get_network_function("bambu_network_unbind"));
    get_bambulab_host_ptr             =  reinterpret_cast<func_get_bambulab_host>(get_network_function("bambu_network_get_bambulab_host"));
    get_user_selected_machine_ptr     =  reinterpret_cast<func_get_user_selected_machine>(get_network_function("bambu_network_get_user_selected_machine"));
    set_user_selected_machine_ptr     =  reinterpret_cast<func_set_user_selected_machine>(get_network_function("bambu_network_set_user_selected_machine"));
    start_print_ptr                   =  reinterpret_cast<func_start_print>(get_network_function("bambu_network_start_print"));
    start_local_print_with_record_ptr =  reinterpret_cast<func_start_local_print_with_record>(get_network_function("bambu_network_start_local_print_with_record"));
    start_send_gcode_to_sdcard_ptr    =  reinterpret_cast<func_start_send_gcode_to_sdcard>(get_network_function("bambu_network_start_send_gcode_to_sdcard"));
    start_local_print_ptr             =  reinterpret_cast<func_start_local_print>(get_network_function("bambu_network_start_local_print"));
    start_sdcard_print_ptr            =  reinterpret_cast<func_start_sdcard_print>(get_network_function("bambu_network_start_sdcard_print"));
    get_user_presets_ptr              =  reinterpret_cast<func_get_user_presets>(get_network_function("bambu_network_get_user_presets"));
    request_setting_id_ptr            =  reinterpret_cast<func_request_setting_id>(get_network_function("bambu_network_request_setting_id"));
    put_setting_ptr                   =  reinterpret_cast<func_put_setting>(get_network_function("bambu_network_put_setting"));
    get_setting_list_ptr              = reinterpret_cast<func_get_setting_list>(get_network_function("bambu_network_get_setting_list"));
    get_setting_list2_ptr             = reinterpret_cast<func_get_setting_list2>(get_network_function("bambu_network_get_setting_list2"));
    delete_setting_ptr                =  reinterpret_cast<func_delete_setting>(get_network_function("bambu_network_delete_setting"));
    get_studio_info_url_ptr           =  reinterpret_cast<func_get_studio_info_url>(get_network_function("bambu_network_get_studio_info_url"));
    set_extra_http_header_ptr         =  reinterpret_cast<func_set_extra_http_header>(get_network_function("bambu_network_set_extra_http_header"));
    get_my_message_ptr                =  reinterpret_cast<func_get_my_message>(get_network_function("bambu_network_get_my_message"));
    check_user_task_report_ptr        =  reinterpret_cast<func_check_user_task_report>(get_network_function("bambu_network_check_user_task_report"));
    get_user_print_info_ptr           =  reinterpret_cast<func_get_user_print_info>(get_network_function("bambu_network_get_user_print_info"));
    get_user_tasks_ptr                =  reinterpret_cast<func_get_user_tasks>(get_network_function("bambu_network_get_user_tasks"));
    get_filament_spools_ptr           =  reinterpret_cast<func_get_filament_spools>(get_network_function("bambu_network_get_filament_spools"));
    create_filament_spool_ptr         =  reinterpret_cast<func_create_filament_spool>(get_network_function("bambu_network_create_filament_spool"));
    update_filament_spool_ptr         =  reinterpret_cast<func_update_filament_spool>(get_network_function("bambu_network_update_filament_spool"));
    delete_filament_spools_ptr        =  reinterpret_cast<func_delete_filament_spools>(get_network_function("bambu_network_delete_filament_spools"));
    get_filament_config_ptr           =  reinterpret_cast<func_get_filament_config>(get_network_function("bambu_network_get_filament_config"));
    get_printer_firmware_ptr          =  reinterpret_cast<func_get_printer_firmware>(get_network_function("bambu_network_get_printer_firmware"));
    get_task_plate_index_ptr          =  reinterpret_cast<func_get_task_plate_index>(get_network_function("bambu_network_get_task_plate_index"));
    get_user_info_ptr                 =  reinterpret_cast<func_get_user_info>(get_network_function("bambu_network_get_user_info"));
    request_bind_ticket_ptr           =  reinterpret_cast<func_request_bind_ticket>(get_network_function("bambu_network_request_bind_ticket"));
    get_subtask_info_ptr              =  reinterpret_cast<func_get_subtask_info>(get_network_function("bambu_network_get_subtask_info"));
    get_slice_info_ptr                =  reinterpret_cast<func_get_slice_info>(get_network_function("bambu_network_get_slice_info"));
    query_bind_status_ptr             =  reinterpret_cast<func_query_bind_status>(get_network_function("bambu_network_query_bind_status"));
    modify_printer_name_ptr           =  reinterpret_cast<func_modify_printer_name>(get_network_function("bambu_network_modify_printer_name"));
    get_camera_url_ptr                =  reinterpret_cast<func_get_camera_url>(get_network_function("bambu_network_get_camera_url"));
    get_camera_url_for_golive_ptr     =  reinterpret_cast<func_get_camera_url_for_golive>(get_network_function("bambu_network_get_camera_url_for_golive"));
    get_design_staffpick_ptr          =  reinterpret_cast<func_get_design_staffpick>(get_network_function("bambu_network_get_design_staffpick"));
    start_publish_ptr                 =  reinterpret_cast<func_start_pubilsh>(get_network_function("bambu_network_start_publish"));
    get_model_publish_url_ptr         =  reinterpret_cast<func_get_model_publish_url>(get_network_function("bambu_network_get_model_publish_url"));
    get_subtask_ptr                   =  reinterpret_cast<func_get_subtask>(get_network_function("bambu_network_get_subtask"));
    get_model_mall_home_url_ptr       =  reinterpret_cast<func_get_model_mall_home_url>(get_network_function("bambu_network_get_model_mall_home_url"));
    get_model_mall_detail_url_ptr     =  reinterpret_cast<func_get_model_mall_detail_url>(get_network_function("bambu_network_get_model_mall_detail_url"));
    get_my_profile_ptr                =  reinterpret_cast<func_get_my_profile>(get_network_function("bambu_network_get_my_profile"));
    get_my_token_ptr                  =  reinterpret_cast<func_get_my_profile>(get_network_function("bambu_network_get_my_token"));
    track_enable_ptr                  =  reinterpret_cast<func_track_enable>(get_network_function("bambu_network_track_enable"));
    track_remove_files_ptr            =  reinterpret_cast<func_track_remove_files>(get_network_function("bambu_network_track_remove_files"));
    track_event_ptr                   =  reinterpret_cast<func_track_event>(get_network_function("bambu_network_track_event"));
    track_header_ptr                  =  reinterpret_cast<func_track_header>(get_network_function("bambu_network_track_header"));
    track_update_property_ptr         = reinterpret_cast<func_track_update_property>(get_network_function("bambu_network_track_update_property"));
    track_get_property_ptr            = reinterpret_cast<func_track_get_property>(get_network_function("bambu_network_track_get_property"));
    put_model_mall_rating_url_ptr     = reinterpret_cast<func_put_model_mall_rating_url>(get_network_function("bambu_network_put_model_mall_rating"));
    get_oss_config_ptr                = reinterpret_cast<func_get_oss_config>(get_network_function("bambu_network_get_oss_config"));
    put_rating_picture_oss_ptr        = reinterpret_cast<func_put_rating_picture_oss>(get_network_function("bambu_network_put_rating_picture_oss"));
    get_model_mall_rating_result_ptr  = reinterpret_cast<func_get_model_mall_rating_result>(get_network_function("bambu_network_get_model_mall_rating"));

    get_mw_user_preference_ptr = reinterpret_cast<func_get_mw_user_preference>(get_network_function("bambu_network_get_mw_user_preference"));
    get_mw_user_4ulist_ptr     = reinterpret_cast<func_get_mw_user_4ulist>(get_network_function("bambu_network_get_mw_user_4ulist"));
    get_hms_snapshot_ptr              = reinterpret_cast<func_get_hms_snapshot>(get_network_function("bambu_network_get_hms_snapshot"));

    return 0;
}

int NetworkAgent::unload_network_module()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", network module %1%")%networking_module;
    UnloadFTModule();
#if defined(_MSC_VER) || defined(_WIN32)
    if (networking_module) {
        FreeLibrary(networking_module);
        networking_module = NULL;
    }
    if (source_module) {
        FreeLibrary(source_module);
        source_module = NULL;
    }
#else
    if (networking_module) {
        dlclose(networking_module);
        networking_module = NULL;
    }
    if (source_module) {
        dlclose(source_module);
        source_module = NULL;
    }
#endif

    check_debug_consistent_ptr        =  nullptr;
    get_version_ptr                   =  nullptr;
    create_agent_ptr                  =  nullptr;
    destroy_agent_ptr                 =  nullptr;
    init_log_ptr                      =  nullptr;
    set_config_dir_ptr                =  nullptr;
    set_cert_file_ptr                 =  nullptr;
    set_country_code_ptr              =  nullptr;
    start_ptr                         =  nullptr;
    set_on_ssdp_msg_fn_ptr            =  nullptr;
    set_on_user_login_fn_ptr          =  nullptr;
    set_on_printer_connected_fn_ptr   =  nullptr;
    set_on_server_connected_fn_ptr    =  nullptr;
    set_on_http_error_fn_ptr          =  nullptr;
    set_get_country_code_fn_ptr       =  nullptr;
    set_on_subscribe_failure_fn_ptr   =  nullptr;
    set_on_message_fn_ptr             =  nullptr;
    set_on_user_message_fn_ptr        =  nullptr;
    set_on_local_connect_fn_ptr       =  nullptr;
    set_on_local_message_fn_ptr       =  nullptr;
    set_queue_on_main_fn_ptr          = nullptr;
    connect_server_ptr                =  nullptr;
    is_server_connected_ptr           =  nullptr;
    refresh_connection_ptr            =  nullptr;
    start_subscribe_ptr               =  nullptr;
    stop_subscribe_ptr                =  nullptr;
    send_message_ptr                  =  nullptr;
    connect_printer_ptr               =  nullptr;
    disconnect_printer_ptr            =  nullptr;
    send_message_to_printer_ptr       =  nullptr;
    check_cert_ptr                    =  nullptr;
    start_discovery_ptr               =  nullptr;
    change_user_ptr                   =  nullptr;
    is_user_login_ptr                 =  nullptr;
    user_logout_ptr                   =  nullptr;
    get_user_id_ptr                   =  nullptr;
    get_user_name_ptr                 =  nullptr;
    get_user_avatar_ptr               =  nullptr;
    get_user_nickanme_ptr             =  nullptr;
    build_login_cmd_ptr               =  nullptr;
    build_logout_cmd_ptr              =  nullptr;
    build_login_info_ptr              =  nullptr;
    ping_bind_ptr                     =  nullptr;
    bind_ptr                          =  nullptr;
    unbind_ptr                        =  nullptr;
    get_bambulab_host_ptr             =  nullptr;
    get_user_selected_machine_ptr     =  nullptr;
    set_user_selected_machine_ptr     =  nullptr;
    start_print_ptr                   =  nullptr;
    start_local_print_with_record_ptr =  nullptr;
    start_send_gcode_to_sdcard_ptr    =  nullptr;
    start_local_print_ptr             =  nullptr;
    start_sdcard_print_ptr             =  nullptr;
    get_user_presets_ptr              =  nullptr;
    request_setting_id_ptr            =  nullptr;
    put_setting_ptr                   =  nullptr;
    get_setting_list_ptr              =  nullptr;
    get_setting_list2_ptr             =  nullptr;
    delete_setting_ptr                =  nullptr;
    get_studio_info_url_ptr           =  nullptr;
    set_extra_http_header_ptr         =  nullptr;
    get_my_message_ptr                =  nullptr;
    check_user_task_report_ptr        =  nullptr;
    get_user_print_info_ptr           =  nullptr;
    get_user_tasks_ptr                =  nullptr;
    get_filament_spools_ptr           =  nullptr;
    create_filament_spool_ptr         =  nullptr;
    update_filament_spool_ptr         =  nullptr;
    delete_filament_spools_ptr        =  nullptr;
    get_filament_config_ptr           =  nullptr;
    get_printer_firmware_ptr          =  nullptr;
    get_task_plate_index_ptr          =  nullptr;
    get_user_info_ptr                 =  nullptr;
    get_subtask_info_ptr              =  nullptr;
    get_slice_info_ptr                =  nullptr;
    query_bind_status_ptr             =  nullptr;
    modify_printer_name_ptr           =  nullptr;
    get_camera_url_ptr                =  nullptr;
    get_camera_url_for_golive_ptr     =  nullptr;
    get_design_staffpick_ptr          =  nullptr;
    start_publish_ptr                 =  nullptr;
    get_model_publish_url_ptr         =  nullptr;
    get_subtask_ptr                   =  nullptr;
    get_model_mall_home_url_ptr       =  nullptr;
    get_model_mall_detail_url_ptr     =  nullptr;
    get_my_profile_ptr                =  nullptr;
    get_my_token_ptr                  =  nullptr;
    track_enable_ptr                  =  nullptr;
    track_remove_files_ptr            =  nullptr;
    track_event_ptr                   =  nullptr;
    track_header_ptr                  =  nullptr;
    track_update_property_ptr         =  nullptr;
    track_get_property_ptr            =  nullptr;
    get_oss_config_ptr                =  nullptr;
    put_rating_picture_oss_ptr        =  nullptr;
    put_model_mall_rating_url_ptr     =  nullptr;
    get_model_mall_rating_result_ptr  = nullptr;

    get_mw_user_preference_ptr        = nullptr;
    get_mw_user_4ulist_ptr            = nullptr;

    return 0;
}

#if defined(_MSC_VER) || defined(_WIN32)
HMODULE NetworkAgent::get_bambu_source_entry()
#else
void* NetworkAgent::get_bambu_source_entry()
#endif
{
    if (source_module) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": BambuSource already loaded, handle=" << source_module;
        return source_module;
    }
    if (!networking_module) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": skipping BambuSource load - networking_module is null";
        return nullptr;
    }

    std::string library;
    std::string data_dir_str = data_dir();
    boost::filesystem::path data_dir_path(data_dir_str);
    auto plugin_folder = data_dir_path / "plugins";

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": loading BambuSource from plugin_folder=" << plugin_folder.string();

#if defined(_MSC_VER) || defined(_WIN32)
    wchar_t lib_wstr[128];

    library = plugin_folder.string() + "/" + std::string(BAMBU_SOURCE_LIBRARY) + ".dll";
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": trying: " << library;
    memset(lib_wstr, 0, sizeof(lib_wstr));
    ::MultiByteToWideChar(CP_UTF8, NULL, library.c_str(), strlen(library.c_str())+1, lib_wstr, sizeof(lib_wstr) / sizeof(lib_wstr[0]));
    source_module = LoadLibrary(lib_wstr);
    if (!source_module) {
        DWORD err = GetLastError();
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": LoadLibrary failed: " << win32_error_string(err);

        std::string library_path = get_libpath_in_current_directory(std::string(BAMBU_SOURCE_LIBRARY));
        if (library_path.empty()) {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": cannot resolve exe-directory path for " << BAMBU_SOURCE_LIBRARY
                << " - BambuSource not available";
            return nullptr;
        }
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": trying exe-directory fallback: " << library_path;
        memset(lib_wstr, 0, sizeof(lib_wstr));
        ::MultiByteToWideChar(CP_UTF8, NULL, library_path.c_str(), strlen(library_path.c_str()) + 1, lib_wstr, sizeof(lib_wstr) / sizeof(lib_wstr[0]));
        source_module = LoadLibrary(lib_wstr);
        if (!source_module) {
            err = GetLastError();
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": LoadLibrary fallback failed: " << win32_error_string(err)
                << " - BambuSource not available";
        }
    }
#else
#if defined(__WXMAC__)
    library = plugin_folder.string() + "/" + std::string("lib") + std::string(BAMBU_SOURCE_LIBRARY) + ".dylib";
#else
    library = plugin_folder.string() + "/" + std::string("lib") + std::string(BAMBU_SOURCE_LIBRARY) + ".so";
#endif
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": trying: " << library
        << " [" << file_existence_note(library) << "]";
    source_module = dlopen(library.c_str(), RTLD_LAZY);
    if (!source_module) {
        char* dll_error = dlerror();
        std::string err = dll_error ? std::string(dll_error) : std::string("(null)");
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": dlopen failed: " << err
            << " - BambuSource not available (live555 video streaming will not work)";
    }
#endif

    if (source_module)
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": BambuSource loaded successfully, handle=" << source_module;

    return source_module;
}

void* NetworkAgent::get_network_function(const char* name)
{
    void* function = nullptr;

    if (!networking_module)
        return function;

#if defined(_MSC_VER) || defined(_WIN32)
    function = GetProcAddress(networking_module, name);
#else
    function = dlsym(networking_module, name);
#endif

    if (!function) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", can not find function %1%")%name;
    }
    return function;
}

std::string NetworkAgent::get_version()
{
    bool consistent = true;
    //check the debug consistent first
    if (check_debug_consistent_ptr) {
#if defined(NDEBUG)
        consistent = check_debug_consistent_ptr(false);
#else
        consistent = check_debug_consistent_ptr(true);
#endif
    }
    if (!consistent) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", inconsistent library,return 00.00.00.00!");
        return "00.00.00.00";
    }
    if (get_version_ptr) {
        return get_version_ptr();
    }
    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", get_version not supported,return 00.00.00.00!");
    return "00.00.00.00";
}

int NetworkAgent::init_log()
{
    int ret = 0;
    if (network_agent && init_log_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] init_log enter agent=" << network_agent;
        ret = init_log_ptr(network_agent);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] init_log ret=" << ret << " agent=" << network_agent;
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%")%network_agent %ret;
    }
    return ret;
}

int NetworkAgent::set_config_dir(std::string config_dir)
{
    int ret = 0;
    if (network_agent && set_config_dir_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] set_config_dir dir=" << config_dir;
        ret = set_config_dir_ptr(network_agent, config_dir);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] set_config_dir ret=" << ret;
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%, config_dir=%3%")%network_agent %ret %config_dir ;
    }
    return ret;
}

int NetworkAgent::set_cert_file(std::string folder, std::string filename)
{
    int ret = 0;
    if (network_agent && set_cert_file_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] set_cert_file folder=" << folder << " file=" << filename;
        ret = set_cert_file_ptr(network_agent, folder, filename);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] set_cert_file ret=" << ret;
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%, folder=%3%, filename=%4%")%network_agent %ret %folder %filename;
    }
    return ret;
}

int NetworkAgent::set_country_code(std::string country_code)
{
    int ret = 0;
    if (network_agent && set_country_code_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] set_country_code code=" << country_code;
        ret = set_country_code_ptr(network_agent, country_code);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] set_country_code ret=" << ret;
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%, country_code=%3%")%network_agent %ret %country_code ;
    }
    return ret;
}

int NetworkAgent::start()
{
    int ret = 0;
    if (network_agent && start_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] start enter agent=" << network_agent;
        ret = start_ptr(network_agent);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] start ret=" << ret;
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%")%network_agent %ret;
    }
    return ret;
}

int NetworkAgent::set_on_ssdp_msg_fn(OnMsgArrivedFn fn)
{
    int ret = 0;
    if (network_agent && set_on_ssdp_msg_fn_ptr) {
        ret = set_on_ssdp_msg_fn_ptr(network_agent, fn);
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%")%network_agent %ret;
    }
    return ret;
}

int NetworkAgent::set_on_user_login_fn(OnUserLoginFn fn)
{
    int ret = 0;
    if (network_agent && set_on_user_login_fn_ptr) {
        ret = set_on_user_login_fn_ptr(network_agent, fn);
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%")%network_agent %ret;
    }
    return ret;
}

int NetworkAgent::set_on_printer_connected_fn(OnPrinterConnectedFn fn)
{
    int ret = 0;
    if (network_agent && set_on_printer_connected_fn_ptr) {
        ret = set_on_printer_connected_fn_ptr(network_agent, fn);
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%")%network_agent %ret;
    }
    return ret;
}

int NetworkAgent::set_on_server_connected_fn(OnServerConnectedFn fn)
{
    int ret = 0;
    if (network_agent && set_on_server_connected_fn_ptr) {
        ret = set_on_server_connected_fn_ptr(network_agent, fn);
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%")%network_agent %ret;
    }
    return ret;
}

int NetworkAgent::set_on_http_error_fn(OnHttpErrorFn fn)
{
    int ret = 0;
    if (network_agent && set_on_http_error_fn_ptr) {
        ret = set_on_http_error_fn_ptr(network_agent, fn);
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%")%network_agent %ret;
    }
    return ret;
}

int NetworkAgent::set_get_country_code_fn(GetCountryCodeFn fn)
{
    int ret = 0;
    if (network_agent && set_get_country_code_fn_ptr) {
        ret = set_get_country_code_fn_ptr(network_agent, fn);
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%")%network_agent %ret;
    }
    return ret;
}

int NetworkAgent::set_on_subscribe_failure_fn(GetSubscribeFailureFn fn)
{
    int ret = 0;
    if (network_agent && set_on_subscribe_failure_fn_ptr) {
        ret = set_on_subscribe_failure_fn_ptr(network_agent, fn);
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%") % network_agent % ret;
    }
    return ret;
}

int NetworkAgent::set_on_message_fn(OnMessageFn fn)
{
    int ret = 0;
    if (network_agent && set_on_message_fn_ptr) {
        ret = set_on_message_fn_ptr(network_agent, fn);
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%")%network_agent %ret;
    }
    return ret;
}

int NetworkAgent::set_on_user_message_fn(OnMessageFn fn)
{
    int ret = 0;
    if (network_agent && set_on_user_message_fn_ptr) {
        ret = set_on_user_message_fn_ptr(network_agent, fn);
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%") % network_agent % ret;
    }
    return ret;
}

int NetworkAgent::set_on_local_connect_fn(OnLocalConnectedFn fn)
{
    int ret = 0;
    if (network_agent && set_on_local_connect_fn_ptr) {
        ret = set_on_local_connect_fn_ptr(network_agent, fn);
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%")%network_agent %ret;
    }
    return ret;
}

int NetworkAgent::set_on_local_message_fn(OnMessageFn fn)
{
    int ret = 0;
    if (network_agent && set_on_local_message_fn_ptr) {
        ret = set_on_local_message_fn_ptr(network_agent, fn);
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%")%network_agent %ret;
    }
    return ret;
}

int NetworkAgent::set_queue_on_main_fn(QueueOnMainFn fn)
{
    int ret = 0;
    if (network_agent && set_queue_on_main_fn_ptr) {
        ret = set_queue_on_main_fn_ptr(network_agent, fn);
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%")%network_agent %ret;
    }
    return ret;
}

int NetworkAgent::connect_server()
{
    int ret = 0;
    if (network_agent && connect_server_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] connect_server enter";
        ret = connect_server_ptr(network_agent);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] connect_server ret=" << ret;
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%")%network_agent %ret;
    }
    return ret;
}

bool NetworkAgent::is_server_connected()
{
    bool ret = false;
    if (network_agent && is_server_connected_ptr) {
        ret = is_server_connected_ptr(network_agent);
        BOOST_LOG_TRIVIAL(trace) << "[cloud_plugin] is_server_connected -> " << ret;
    }
    return ret;
}

int NetworkAgent::refresh_connection()
{
    int ret = 0;
    if (network_agent && refresh_connection_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] refresh_connection enter";
        ret = refresh_connection_ptr(network_agent);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] refresh_connection ret=" << ret;
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%")%network_agent %ret;
    }
    return ret;
}

int NetworkAgent::start_subscribe(std::string module)
{
    int ret = 0;
    if (network_agent && start_subscribe_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] start_subscribe module=" << module;
        ret = start_subscribe_ptr(network_agent, module);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] start_subscribe ret=" << ret << " module=" << module;
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%, module=%3%")%network_agent %ret %module ;
    }
    return ret;
}

int NetworkAgent::stop_subscribe(std::string module)
{
    int ret = 0;
    if (network_agent && stop_subscribe_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] stop_subscribe module=" << module;
        ret = stop_subscribe_ptr(network_agent, module);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] stop_subscribe ret=" << ret << " module=" << module;
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%, module=%3%")%network_agent %ret %module ;
    }
    return ret;
}

int NetworkAgent::add_subscribe(std::vector<std::string> dev_list)
{
    int ret = 0;
    if (network_agent && add_subscribe_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] add_subscribe " << cloud_format_dev_list(dev_list);
        ret = add_subscribe_ptr(network_agent, dev_list);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] add_subscribe ret=" << ret;
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%") %network_agent %ret;
    }
    return ret;
}

int NetworkAgent::del_subscribe(std::vector<std::string> dev_list)
{
    int ret = 0;
    if (network_agent && del_subscribe_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] del_subscribe " << cloud_format_dev_list(dev_list);
        ret = del_subscribe_ptr(network_agent, dev_list);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] del_subscribe ret=" << ret;
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%") %network_agent %ret;
    }
    return ret;
}

void NetworkAgent::enable_multi_machine(bool enable)
{
    if (network_agent && enable_multi_machine_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] enable_multi_machine enable=" << enable;
        enable_multi_machine_ptr(network_agent, enable);
    }
}

int NetworkAgent::send_message(std::string dev_id, std::string json_str, int qos, int flag)
{
    int ret = 0;
    if (network_agent && send_message_ptr) {
        ret = send_message_ptr(network_agent, dev_id, json_str, qos, flag);
        BOOST_LOG_TRIVIAL(debug) << "[cloud_plugin] send_message ret=" << ret << " dev_id=" << BBLCrossTalk::Crosstalk_DevId(dev_id)
            << " qos=" << qos << " flag=" << flag << " json_len=" << json_str.size()
            << " json=" << cloud_trunc_body(json_str);
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ <<
            boost::format(" error: network_agent=%1%, ret=%2%, dev_id=%3%, json_str=%4%, qos=%5%") %network_agent %ret %BBLCrossTalk::Crosstalk_DevId(dev_id) %json_str %qos;
    }
    return ret;
}

int NetworkAgent::connect_printer(std::string dev_id, std::string dev_ip, std::string username, std::string password, bool use_ssl)
{
    int ret = 0;
    if (network_agent && connect_printer_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] connect_printer dev_id=" << BBLCrossTalk::Crosstalk_DevId(dev_id)
            << " dev_ip=" << BBLCrossTalk::Crosstalk_DevIP(dev_ip) << " use_ssl=" << use_ssl << " user_len=" << username.size();
        ret = connect_printer_ptr(network_agent, dev_id, dev_ip, username, password, use_ssl);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] connect_printer ret=" << ret;
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ <<
            (boost::format(" error: network_agent=%1%, ret=%2%, dev_id=%3%, dev_ip=%4%, username=%5%, password=%6%") %network_agent %ret %BBLCrossTalk::Crosstalk_DevId(dev_id) %BBLCrossTalk::Crosstalk_DevIP(dev_ip) %username %password).str();
    }
    return ret;
}

int NetworkAgent::disconnect_printer()
{
    int ret = 0;
    if (network_agent && disconnect_printer_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] disconnect_printer enter";
        ret = disconnect_printer_ptr(network_agent);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] disconnect_printer ret=" << ret;
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%") %network_agent %ret;
    }
    return ret;
}

int NetworkAgent::send_message_to_printer(std::string dev_id, std::string json_str, int qos, int flag)
{
    int ret = 0;
    if (network_agent && send_message_to_printer_ptr) {
        ret = send_message_to_printer_ptr(network_agent, dev_id, json_str, qos, flag);
        BOOST_LOG_TRIVIAL(debug) << "[cloud_plugin] send_message_to_printer ret=" << ret << " dev_id=" << BBLCrossTalk::Crosstalk_DevId(dev_id)
            << " qos=" << qos << " flag=" << flag << " json_len=" << json_str.size()
            << " json=" << cloud_trunc_body(json_str);
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%, dev_id=%3%, json_str=%4%, qos=%5%") %network_agent %ret %BBLCrossTalk::Crosstalk_DevId(dev_id) %json_str %qos;
    }
    return ret;
}

int NetworkAgent::check_cert()
{
    int ret = 0;
    if (network_agent && check_cert_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] check_cert enter";
        ret = check_cert_ptr(network_agent);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] check_cert ret=" << ret;
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%") %network_agent %ret;
    }
    return ret;
}

void NetworkAgent::install_device_cert(std::string dev_id, bool lan_only)
{
    if (network_agent && install_device_cert_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] install_device_cert dev_id=" << BBLCrossTalk::Crosstalk_DevId(dev_id) << " lan_only=" << lan_only;
        install_device_cert_ptr(network_agent, dev_id, lan_only);
    }
}

bool NetworkAgent::start_discovery(bool start, bool sending)
{
    bool ret = false;
    if (network_agent && start_discovery_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] start_discovery start=" << start << " sending=" << sending;
        ret = start_discovery_ptr(network_agent, start, sending);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] start_discovery ret=" << ret;
    }
    return ret;
}

int  NetworkAgent::change_user(std::string user_info)
{
    int ret = 0;
    if (network_agent && change_user_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] change_user payload_len=" << user_info.size();
        ret = change_user_ptr(network_agent, user_info);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] change_user ret=" << ret;
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%") %network_agent %ret ;
    }
    return ret;
}

bool NetworkAgent::is_user_login()
{
    bool ret = false;
    if (network_agent && is_user_login_ptr) {
        ret = is_user_login_ptr(network_agent);
        BOOST_LOG_TRIVIAL(trace) << "[cloud_plugin] is_user_login -> " << ret;
    }
    return ret;
}

int  NetworkAgent::user_logout(bool request)
{
    int ret = 0;
    if (network_agent && user_logout_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] user_logout request=" << request;
        ret = user_logout_ptr(network_agent, request);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] user_logout ret=" << ret;
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%") %network_agent %ret;
    }
    return ret;
}

std::string NetworkAgent::get_user_id()
{
    std::string ret;
    if (network_agent && get_user_id_ptr) {
        ret = get_user_id_ptr(network_agent);
        BOOST_LOG_TRIVIAL(debug) << "[cloud_plugin] get_user_id len=" << ret.size();
    }
    return ret;
}

std::string NetworkAgent::get_user_name()
{
    std::string ret;
    if (network_agent && get_user_name_ptr) {
        ret = get_user_name_ptr(network_agent);
        BOOST_LOG_TRIVIAL(debug) << "[cloud_plugin] get_user_name len=" << ret.size();
    }
    return ret;
}

std::string NetworkAgent::get_user_avatar()
{
    std::string ret;
    if (network_agent && get_user_avatar_ptr) {
        ret = get_user_avatar_ptr(network_agent);
        BOOST_LOG_TRIVIAL(debug) << "[cloud_plugin] get_user_avatar len=" << ret.size();
    }
    return ret;
}

std::string NetworkAgent::get_user_nickanme()
{
    std::string ret;
    if (network_agent && get_user_nickanme_ptr) {
        ret = get_user_nickanme_ptr(network_agent);
        BOOST_LOG_TRIVIAL(debug) << "[cloud_plugin] get_user_nickanme len=" << ret.size();
    }
    return ret;
}

std::string NetworkAgent::build_login_cmd()
{
    std::string ret;
    if (network_agent && build_login_cmd_ptr) {
        ret = build_login_cmd_ptr(network_agent);
        BOOST_LOG_TRIVIAL(debug) << "[cloud_plugin] build_login_cmd out_len=" << ret.size();
    }
    return ret;
}

std::string NetworkAgent::build_logout_cmd()
{
    std::string ret;
    if (network_agent && build_logout_cmd_ptr) {
        ret = build_logout_cmd_ptr(network_agent);
        BOOST_LOG_TRIVIAL(debug) << "[cloud_plugin] build_logout_cmd out_len=" << ret.size();
    }
    return ret;
}

std::string NetworkAgent::build_login_info()
{
    std::string ret;
    if (network_agent && build_login_info_ptr) {
        ret = build_login_info_ptr(network_agent);
        BOOST_LOG_TRIVIAL(debug) << "[cloud_plugin] build_login_info out_len=" << ret.size();
    }
    return ret;
}

int NetworkAgent::ping_bind(std::string ping_code)
{
    int ret = 0;
    if (network_agent && ping_bind_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] ping_bind code_len=" << ping_code.size();
        ret = ping_bind_ptr(network_agent, ping_code);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] ping_bind ret=" << ret;
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%")
            % network_agent % ret;
    }
    return ret;
}

int NetworkAgent::bind_detect(std::string dev_ip, std::string sec_link, detectResult& detect)
{
    int ret = 0;
    if (network_agent && bind_detect_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] bind_detect dev_ip=" << BBLCrossTalk::Crosstalk_DevIP(dev_ip) << " sec_link_len=" << sec_link.size();
        ret = bind_detect_ptr(network_agent, dev_ip, sec_link, detect);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] bind_detect ret=" << ret;
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%, dev_ip=%3%") %network_agent %ret %BBLCrossTalk::Crosstalk_DevIP(dev_ip);
    }
    return ret;
}

int NetworkAgent::report_consent(std::string expand)
{
    int ret = 0;
    if (network_agent && report_consent_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] report_consent expand_len=" << expand.size();
        ret = report_consent_ptr(network_agent, expand);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] report_consent ret=" << ret;
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%") % network_agent % ret;
    }
    return ret;
}

int NetworkAgent::set_server_callback(OnServerErrFn fn)
{
    int ret = 0;
    if (network_agent && set_server_callback_ptr) {
        ret = set_server_callback_ptr(network_agent, fn);
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%")
            % network_agent % ret;
    }
    return ret;
}

int NetworkAgent::bind(std::string dev_ip, std::string dev_id, std::string sec_link, std::string timezone,  bool improved, OnUpdateStatusFn update_fn)
{
    int ret = 0;
    if (network_agent && bind_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] bind dev_ip=" << BBLCrossTalk::Crosstalk_DevIP(dev_ip)
            << " dev_id=" << BBLCrossTalk::Crosstalk_DevId(dev_id) << " tz=" << timezone << " improved=" << improved;
        ret = bind_ptr(network_agent, dev_ip, dev_id, sec_link, timezone, improved, update_fn);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] bind ret=" << ret;
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%, dev_ip=%3%, timezone=%4%") %network_agent %ret %BBLCrossTalk::Crosstalk_DevIP(dev_ip) %timezone;
    }
    return ret;
}

int NetworkAgent::unbind(std::string dev_id)
{
    int ret = 0;
    if (network_agent && unbind_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] unbind dev_id=" << BBLCrossTalk::Crosstalk_DevId(dev_id);
        ret = unbind_ptr(network_agent, dev_id);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] unbind ret=" << ret;
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%") %network_agent %ret ;
    }
    return ret;
}

std::string NetworkAgent::get_bambulab_host()
{
    std::string ret;
    if (network_agent && get_bambulab_host_ptr) {
        ret = get_bambulab_host_ptr(network_agent);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_bambulab_host host_len=" << ret.size();
    }
    return ret;
}

std::string NetworkAgent::get_user_selected_machine()
{
    std::string ret;
    if (network_agent && get_user_selected_machine_ptr) {
        ret = get_user_selected_machine_ptr(network_agent);
        BOOST_LOG_TRIVIAL(debug) << "[cloud_plugin] get_user_selected_machine dev_id=" << BBLCrossTalk::Crosstalk_DevId(ret);
    }
    return ret;
}

int NetworkAgent::set_user_selected_machine(std::string dev_id)
{
    int ret = 0;
    if (network_agent && set_user_selected_machine_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] set_user_selected_machine dev_id=" << BBLCrossTalk::Crosstalk_DevId(dev_id);
        ret = set_user_selected_machine_ptr(network_agent, dev_id);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] set_user_selected_machine ret=" << ret;
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%, user_info=%3%") %network_agent %ret %BBLCrossTalk::Crosstalk_DevId(dev_id);
    }
    return ret;
}

int NetworkAgent::start_print(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn, OnWaitFn wait_fn)
{
    int ret = 0;
    if (network_agent && start_print_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] start_print dev_id=" << BBLCrossTalk::Crosstalk_DevId(params.dev_id)
            << " task=" << params.task_name << " project=" << params.project_name;
        ret = start_print_ptr(network_agent, params, update_fn, cancel_fn, wait_fn);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] start_print ret=" << ret << " dev_id=" << BBLCrossTalk::Crosstalk_DevId(params.dev_id);
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__
                                << boost::format(" : network_agent=%1%, ret=%2%, dev_id=%3%, task_name=%4%, project_name=%5%") %network_agent %ret %BBLCrossTalk::Crosstalk_DevId(params.dev_id) %params.task_name %params.project_name;
    }
    return ret;
}

int NetworkAgent::start_local_print_with_record(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn, OnWaitFn wait_fn)
{
    int ret = 0;
    if (network_agent && start_local_print_with_record_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] start_local_print_with_record dev_id=" << BBLCrossTalk::Crosstalk_DevId(params.dev_id)
            << " task=" << params.task_name;
        ret = start_local_print_with_record_ptr(network_agent, params, update_fn, cancel_fn, wait_fn);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] start_local_print_with_record ret=" << ret;
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(" : network_agent=%1%, ret=%2%, dev_id=%3%, task_name=%4%, project_name=%5%") %network_agent %ret %BBLCrossTalk::Crosstalk_DevId(params.dev_id) %params.task_name %params.project_name;
    }
    return ret;
}

int NetworkAgent::start_send_gcode_to_sdcard(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn, OnWaitFn wait_fn)
{
    int ret = 0;
    if (network_agent && start_send_gcode_to_sdcard_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] start_send_gcode_to_sdcard dev_id=" << BBLCrossTalk::Crosstalk_DevId(params.dev_id)
            << " task=" << params.task_name;
        ret = start_send_gcode_to_sdcard_ptr(network_agent, params, update_fn, cancel_fn, wait_fn);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] start_send_gcode_to_sdcard ret=" << ret;
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(" : network_agent=%1%, ret=%2%, dev_id=%3%, task_name=%4%, project_name=%5%") %network_agent %ret %BBLCrossTalk::Crosstalk_DevId(params.dev_id) %params.task_name %params.project_name;
    }
    return ret;
}

int NetworkAgent::start_local_print(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn)
{
    int ret = 0;
    if (network_agent && start_local_print_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] start_local_print dev_id=" << BBLCrossTalk::Crosstalk_DevId(params.dev_id)
            << " task=" << params.task_name;
        ret = start_local_print_ptr(network_agent, params, update_fn, cancel_fn);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] start_local_print ret=" << ret;
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(" : network_agent=%1%, ret=%2%, dev_id=%3%, task_name=%4%, project_name=%5%") %network_agent %ret %BBLCrossTalk::Crosstalk_DevId(params.dev_id) %params.task_name %params.project_name;
    }
    return ret;
}

int NetworkAgent::start_sdcard_print(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn)
{
    int ret = 0;
    if (network_agent && start_sdcard_print_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] start_sdcard_print dev_id=" << BBLCrossTalk::Crosstalk_DevId(params.dev_id)
            << " task=" << params.task_name;
        ret = start_sdcard_print_ptr(network_agent, params, update_fn, cancel_fn);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] start_sdcard_print ret=" << ret;
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(" : network_agent=%1%, ret=%2%, dev_id=%3%, task_name=%4%, project_name=%5%") %network_agent %ret %BBLCrossTalk::Crosstalk_DevId(params.dev_id) %params.task_name %params.project_name;
    }
    return ret;
}

int NetworkAgent::get_user_presets(std::map<std::string, std::map<std::string, std::string>>* user_presets)
{
    int ret = 0;
    if (network_agent && get_user_presets_ptr) {
        ret = get_user_presets_ptr(network_agent, user_presets);
        size_t inner_kv = 0;
        if (user_presets) {
            for (const auto &kv : *user_presets)
                inner_kv += kv.second.size();
        }
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_user_presets ret=" << ret << " outer=" << (user_presets ? user_presets->size() : 0)
            << " inner_kv_total=" << inner_kv;
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(" : network_agent=%1%, ret=%2%, setting_id count=%3%")%network_agent %ret %(user_presets ? user_presets->size() : 0) ;
    }
    return ret;
}

std::string NetworkAgent::request_setting_id(std::string name, std::map<std::string, std::string>* values_map, unsigned int* http_code)
{
    std::string ret;
    if (network_agent && request_setting_id_ptr) {
        const size_t map_sz = values_map ? values_map->size() : 0;
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] request_setting_id name=" << name << " map_keys=" << map_sz;
        ret = request_setting_id_ptr(network_agent, name, values_map, http_code);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] request_setting_id ret_len=" << ret.size()
            << " http_code=" << (http_code ? *http_code : 0u);
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(" : network_agent=%1%, name=%2%, http_code=%3%, ret.setting_id=%4%")
                %network_agent %name %(http_code ? *http_code : 0u) %ret;
    }
    return ret;
}

int NetworkAgent::put_setting(std::string setting_id, std::string name, std::map<std::string, std::string>* values_map, unsigned int* http_code)
{
    int ret = 0;
    if (network_agent && put_setting_ptr) {
        const size_t map_sz = values_map ? values_map->size() : 0;
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] put_setting setting_id=" << setting_id << " name=" << name << " map_keys=" << map_sz;
        ret = put_setting_ptr(network_agent, setting_id, name, values_map, http_code);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] put_setting ret=" << ret << " http_code=" << (http_code ? *http_code : 0u);
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(" : network_agent=%1%, setting_id=%2%, name=%3%, http_code=%4%, ret=%5%")
                %network_agent %setting_id %name %(http_code ? *http_code : 0u) %ret;
    }
    return ret;
}

int NetworkAgent::get_setting_list(std::string bundle_version, ProgressFn pro_fn, WasCancelledFn cancel_fn)
{
    int ret = 0;
    if (network_agent && get_setting_list_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_setting_list bundle_version=" << bundle_version;
        ret = get_setting_list_ptr(network_agent, bundle_version, pro_fn, cancel_fn);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_setting_list ret=" << ret << " bundle_version=" << bundle_version;
        if (ret) BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%, bundle_version=%3%") % network_agent % ret % bundle_version;
    }
    return ret;
}

int NetworkAgent::get_setting_list2(std::string bundle_version, CheckFn chk_fn, ProgressFn pro_fn, WasCancelledFn cancel_fn)
{
    int ret = 0;
    if (network_agent && get_setting_list2_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_setting_list2 bundle_version=" << bundle_version;
        ret = get_setting_list2_ptr(network_agent, bundle_version, chk_fn, pro_fn, cancel_fn);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_setting_list2 ret=" << ret << " bundle_version=" << bundle_version;
        if (ret) BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%, bundle_version=%3%") % network_agent % ret % bundle_version;
    } else {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_setting_list2 fallback get_setting_list bundle_version=" << bundle_version;
        ret = get_setting_list(bundle_version, pro_fn, cancel_fn);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_setting_list2 fallback ret=" << ret;
    }
    return ret;
}

int NetworkAgent::delete_setting(std::string setting_id)
{
    int ret = 0;
    if (network_agent && delete_setting_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] delete_setting setting_id=" << setting_id;
        ret = delete_setting_ptr(network_agent, setting_id);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] delete_setting ret=" << ret;
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%, setting_id=%3%")%network_agent %ret %setting_id ;
    }
    return ret;
}

std::string NetworkAgent::get_studio_info_url()
{
    std::string ret;
    if (network_agent && get_studio_info_url_ptr) {
        ret = get_studio_info_url_ptr(network_agent);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_studio_info_url len=" << ret.size();
    }
    return ret;
}

int NetworkAgent::set_extra_http_header(std::map<std::string, std::string> extra_headers)
{
    int ret = 0;
    if (network_agent && set_extra_http_header_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] set_extra_http_header " << cloud_header_keys(extra_headers);
        ret = set_extra_http_header_ptr(network_agent, extra_headers);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] set_extra_http_header ret=" << ret;
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%, extra_headers count=%3%")%network_agent %ret %extra_headers.size() ;
    }
    return ret;
}

int NetworkAgent::get_my_message(int type, int after, int limit, unsigned int* http_code, std::string* http_body)
{
    int ret = 0;
    if (network_agent && get_my_message_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_my_message type=" << type << " after=" << after << " limit=" << limit;
        ret = get_my_message_ptr(network_agent, type, after, limit, http_code, http_body);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_my_message ret=" << ret
            << " http_code=" << (http_code ? *http_code : 0u)
            << " body_len=" << (http_body ? http_body->size() : size_t(0));
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%") % network_agent % ret;
    }
    return ret;
}

int NetworkAgent::check_user_task_report(int* task_id, bool* printable)
{
    int ret = 0;
    if (network_agent && check_user_task_report_ptr) {
        ret = check_user_task_report_ptr(network_agent, task_id, printable);
        BOOST_LOG_TRIVIAL(debug) << "[cloud_plugin] check_user_task_report ret=" << ret
            << " task_id=" << (task_id ? *task_id : 0) << " printable=" << (printable ? *printable : false);
        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(" : network_agent=%1%, ret=%2%, task_id=%3%, printable=%4%")%network_agent %ret %(task_id ? *task_id : 0) %(printable ? *printable : false);
    }
    return ret;
}

int NetworkAgent::get_user_print_info(unsigned int* http_code, std::string* http_body)
{
    int ret = 0;
    if (network_agent && get_user_print_info_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_user_print_info enter";
        ret = get_user_print_info_ptr(network_agent, http_code, http_body);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_user_print_info ret=" << ret
            << " http_code=" << (http_code ? *http_code : 0u)
            << " body_len=" << (http_body ? http_body->size() : size_t(0))
            << " body_preview=" << cloud_trunc_body(http_body ? *http_body : std::string());
        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%, http_code=%3%")%network_agent %ret %(http_code ? *http_code : 0u);
    }
    return ret;
}

int NetworkAgent::get_user_tasks(TaskQueryParams params, std::string* http_body)
{
    int ret = 0;
    if (network_agent && get_user_tasks_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_user_tasks dev_id=" << BBLCrossTalk::Crosstalk_DevId(params.dev_id)
            << " status=" << params.status << " offset=" << params.offset << " limit=" << params.limit;
        ret = get_user_tasks_ptr(network_agent, params, http_body);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_user_tasks ret=" << ret
            << " body_len=" << (http_body ? http_body->size() : size_t(0));
        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%") %network_agent %ret;
    }
    return ret;
}

int NetworkAgent::get_filament_spools(FilamentQueryParams params, std::string* http_body)
{
    if (!network_agent || !get_filament_spools_ptr) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": unavailable (network_agent="
            << network_agent << " func_ptr=" << (void*)get_filament_spools_ptr << ")";
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;
    }
    BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_filament_spools cat=" << params.category << " status=" << params.status
        << " offset=" << params.offset << " limit=" << params.limit;
    int ret = get_filament_spools_ptr(network_agent, params, http_body);
    BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_filament_spools ret=" << ret
        << " body_len=" << (http_body ? http_body->size() : size_t(0));
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(" : network_agent=%1%, ret=%2%") %network_agent %ret;
    return ret;
}

int NetworkAgent::create_filament_spool(std::string request_body, std::string* http_body)
{
    if (!network_agent || !create_filament_spool_ptr) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": unavailable (network_agent="
            << network_agent << " func_ptr=" << (void*)create_filament_spool_ptr << ")";
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;
    }
    BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] create_filament_spool body_len=" << request_body.size();
    int ret = create_filament_spool_ptr(network_agent, request_body, http_body);
    BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] create_filament_spool ret=" << ret
        << " resp_len=" << (http_body ? http_body->size() : size_t(0));
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(" : network_agent=%1%, ret=%2%") %network_agent %ret;
    return ret;
}

int NetworkAgent::update_filament_spool(std::string spool_id, std::string request_body, std::string* http_body)
{
    if (!network_agent || !update_filament_spool_ptr) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": unavailable (network_agent="
            << network_agent << " func_ptr=" << (void*)update_filament_spool_ptr << ")";
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;
    }
    BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] update_filament_spool spool_id=" << spool_id << " body_len=" << request_body.size();
    int ret = update_filament_spool_ptr(network_agent, spool_id, request_body, http_body);
    BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] update_filament_spool ret=" << ret;
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(" : network_agent=%1%, ret=%2%, spool_id=%3%") %network_agent %ret %spool_id;
    return ret;
}

int NetworkAgent::delete_filament_spools(FilamentDeleteParams params, std::string* http_body)
{
    if (!network_agent || !delete_filament_spools_ptr) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": unavailable (network_agent="
            << network_agent << " func_ptr=" << (void*)delete_filament_spools_ptr << ")";
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;
    }
    BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] delete_filament_spools id_count=" << params.ids.size()
        << " rfid_count=" << params.rfids.size();
    int ret = delete_filament_spools_ptr(network_agent, params, http_body);
    BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] delete_filament_spools ret=" << ret;
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(" : network_agent=%1%, ret=%2%") %network_agent %ret;
    return ret;
}

int NetworkAgent::get_filament_config(std::string* http_body)
{
    if (!network_agent || !get_filament_config_ptr) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": unavailable (network_agent="
            << network_agent << " func_ptr=" << (void*)get_filament_config_ptr << ")";
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;
    }
    BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_filament_config enter";
    int ret = get_filament_config_ptr(network_agent, http_body);
    BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_filament_config ret=" << ret
        << " body_len=" << (http_body ? http_body->size() : size_t(0));
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(" : network_agent=%1%, ret=%2%") %network_agent %ret;
    return ret;
}

int NetworkAgent::get_printer_firmware(std::string dev_id, unsigned* http_code, std::string* http_body)
{
    int ret = 0;
    if (network_agent && get_printer_firmware_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_printer_firmware dev_id=" << BBLCrossTalk::Crosstalk_DevId(dev_id);
        ret = get_printer_firmware_ptr(network_agent, dev_id, http_code, http_body);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_printer_firmware ret=" << ret
            << " http_code=" << (http_code ? *http_code : 0u)
            << " body_len=" << (http_body ? http_body->size() : size_t(0));
        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(" : network_agent=%1%, ret=%2%, dev_id=%3%") %network_agent %ret %BBLCrossTalk::Crosstalk_DevId(dev_id);
    }
    return ret;
}

int NetworkAgent::get_task_plate_index(std::string task_id, int* plate_index)
{
    int ret = 0;
    if (network_agent && get_task_plate_index_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_task_plate_index task_id=" << task_id;
        ret = get_task_plate_index_ptr(network_agent, task_id, plate_index);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_task_plate_index ret=" << ret
            << " plate_index=" << (plate_index ? *plate_index : -1);
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%, task_id=%3%")%network_agent %ret %task_id;
    }
    return ret;
}

int NetworkAgent::get_user_info(int* identifier)
{
    int ret = 0;
    if (network_agent && get_user_info_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_user_info enter";
        ret = get_user_info_ptr(network_agent, identifier);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_user_info ret=" << ret
            << " identifier=" << (identifier ? *identifier : 0);
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%") % network_agent % ret;
    }
    return ret;
}

int NetworkAgent::request_bind_ticket(std::string* ticket)
{
    int ret = 0;
    if (network_agent && request_bind_ticket_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] request_bind_ticket enter";
        ret = request_bind_ticket_ptr(network_agent, ticket);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] request_bind_ticket ret=" << ret
            << " ticket_len=" << (ticket ? ticket->size() : size_t(0));
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%") % network_agent % ret;
    }
    return ret;
}

int NetworkAgent::get_subtask_info(std::string subtask_id, std::string* task_json, unsigned int* http_code, std::string* http_body)
{
    int ret = 0;
    if (network_agent && get_subtask_info_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_subtask_info subtask_id=" << subtask_id;
        ret = get_subtask_info_ptr(network_agent, subtask_id, task_json, http_code, http_body);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_subtask_info ret=" << ret
            << " http_code=" << (http_code ? *http_code : 0u)
            << " task_json_len=" << (task_json ? task_json->size() : size_t(0))
            << " body_len=" << (http_body ? http_body->size() : size_t(0));
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format("error: network_agent=%1%, ret=%2%") % network_agent % ret;
    }
    return ret;
}

int NetworkAgent::get_slice_info(std::string project_id, std::string profile_id, int plate_index, std::string* slice_json)
{
    int ret = 0;
    if (network_agent && get_slice_info_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_slice_info project_id=" << project_id
            << " profile_id=" << profile_id << " plate_index=" << plate_index;
        ret = get_slice_info_ptr(network_agent, project_id, profile_id, plate_index, slice_json);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_slice_info ret=" << ret
            << " slice_json_len=" << (slice_json ? slice_json->size() : size_t(0))
            << " preview=" << cloud_trunc_body(slice_json ? *slice_json : std::string());
        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(" : network_agent=%1%, project_id=%2%, profile_id=%3%, plate_index=%4%, slice_json=%5%")
                %network_agent %project_id %profile_id %plate_index %(slice_json ? *slice_json : std::string());
    }
    return ret;
}

int NetworkAgent::query_bind_status(std::vector<std::string> query_list, unsigned int* http_code, std::string* http_body)
{
    int ret = 0;
    if (network_agent && query_bind_status_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] query_bind_status " << cloud_format_dev_list(query_list);
        ret = query_bind_status_ptr(network_agent, query_list, http_code, http_body);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] query_bind_status ret=" << ret
            << " http_code=" << (http_code ? *http_code : 0u)
            << " body_len=" << (http_body ? http_body->size() : size_t(0));
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%, http_code=%3%") %network_agent %ret %(*http_code);
    }
    return ret;
}

int NetworkAgent::modify_printer_name(std::string dev_id, std::string dev_name)
{
    int ret = 0;
    if (network_agent && modify_printer_name_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] modify_printer_name dev_id=" << BBLCrossTalk::Crosstalk_DevId(dev_id) << " name=" << dev_name;
        ret = modify_printer_name_ptr(network_agent, dev_id, dev_name);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] modify_printer_name ret=" << ret;
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(" : network_agent=%1%, ret=%2%, dev_id=%3%, dev_name=%4%") %network_agent %ret %BBLCrossTalk::Crosstalk_DevId(dev_id) %dev_name;
    }
    return ret;
}

int NetworkAgent::get_camera_url(std::string dev_id, std::function<void(std::string)> callback)
{
    int ret = 0;
    if (network_agent && get_camera_url_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_camera_url dev_id=" << BBLCrossTalk::Crosstalk_DevId(dev_id);
        ret = get_camera_url_ptr(network_agent, dev_id, callback);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_camera_url ret=" << ret;
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%, dev_id=%3%") %network_agent %ret %BBLCrossTalk::Crosstalk_DevId(dev_id);
    }
    return ret;
}

int NetworkAgent::get_camera_url_for_golive(std::string dev_id, std::string sdev_id, std::function<void(std::string)> callback)
{
    int ret = 0;
    if (network_agent && get_camera_url_for_golive_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_camera_url_for_golive dev_id=" << BBLCrossTalk::Crosstalk_DevId(dev_id)
            << " sdev_id=" << sdev_id;
        ret = get_camera_url_for_golive_ptr(network_agent, dev_id, sdev_id, callback);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_camera_url_for_golive ret=" << ret;
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%, dev_id=%3%") %network_agent %ret %BBLCrossTalk::Crosstalk_DevId(dev_id);
    }
    return ret;
}

int NetworkAgent::get_design_staffpick(int offset, int limit, std::function<void(std::string)> callback)
{
    int ret = 0;
    if (network_agent && get_design_staffpick_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_design_staffpick offset=" << offset << " limit=" << limit;
        ret = get_design_staffpick_ptr(network_agent, offset, limit, callback);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_design_staffpick ret=" << ret;
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%")%network_agent %ret;
    }
    return ret;
}

int NetworkAgent::get_mw_user_preference(std::function<void(std::string)> callback)
{
    int ret = 0;
    if (network_agent && get_mw_user_preference_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_mw_user_preference enter";
        ret = get_mw_user_preference_ptr(network_agent,callback);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_mw_user_preference ret=" << ret;
        if (ret) BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%") % network_agent % ret;
    }
    return ret;
}


int NetworkAgent::get_mw_user_4ulist(int seed, int limit, std::function<void(std::string)> callback)
{
    int ret = 0;
    if (network_agent && get_mw_user_4ulist_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_mw_user_4ulist seed=" << seed << " limit=" << limit;
        ret = get_mw_user_4ulist_ptr(network_agent,seed, limit, callback);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_mw_user_4ulist ret=" << ret;
        if (ret) BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%") % network_agent % ret;
    }
    return ret;
}

int NetworkAgent::get_hms_snapshot(std::string dev_id, std::string file_name, std::function<void(std::string, int)> callback)
{
    int ret = -1;
    if (network_agent && get_hms_snapshot_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_hms_snapshot dev_id=" << BBLCrossTalk::Crosstalk_DevId(dev_id) << " file=" << file_name;
        ret = get_hms_snapshot_ptr(network_agent, dev_id, file_name, callback);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_hms_snapshot ret=" << ret;
        if (ret) BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%") % network_agent % ret;
    }
    return ret;
}

int NetworkAgent::start_publish(PublishParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn, std::string *out)
{
    int ret = 0;
    if (network_agent && start_publish_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] start_publish project=" << params.project_name
            << " design_id=" << params.design_id << " model_id=" << params.project_model_id;
        ret = start_publish_ptr(network_agent, params, update_fn, cancel_fn, out);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] start_publish ret=" << ret << " out_len=" << (out ? out->size() : size_t(0));
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%") % network_agent % ret;
    }
    return ret;
}

int NetworkAgent::get_model_publish_url(std::string* url)
{
    int ret = 0;
    if (network_agent && get_model_publish_url_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_model_publish_url enter";
        ret = get_model_publish_url_ptr(network_agent, url);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_model_publish_url ret=" << ret << " url_len=" << (url ? url->size() : size_t(0));
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%") % network_agent % ret;
    }
    return ret;
}

int NetworkAgent::get_subtask(BBLModelTask* task, OnGetSubTaskFn getsub_fn)
{
    int ret = 0;
    if (network_agent && get_subtask_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_subtask enter";
        ret = get_subtask_ptr(network_agent, task, getsub_fn);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_subtask ret=" << ret;
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%") % network_agent % ret;
    }

    return ret;
}

int NetworkAgent::get_model_mall_home_url(std::string* url)
{
    int ret = 0;
    if (network_agent && get_model_publish_url_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_model_mall_home_url enter";
        ret = get_model_mall_home_url_ptr(network_agent, url);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_model_mall_home_url ret=" << ret << " url_len=" << (url ? url->size() : size_t(0));
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%") % network_agent % ret;
    }
    return ret;
}

int NetworkAgent::get_model_mall_detail_url(std::string* url, std::string id)
{
    int ret = 0;
    if (network_agent && get_model_publish_url_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_model_mall_detail_url id=" << id;
        ret = get_model_mall_detail_url_ptr(network_agent, url, id);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_model_mall_detail_url ret=" << ret << " url_len=" << (url ? url->size() : size_t(0));
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%") % network_agent % ret;
    }
    return ret;
}

int NetworkAgent::get_my_profile(std::string token, unsigned int *http_code, std::string *http_body)
{
    int ret = 0;
    if (network_agent && get_my_profile_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_my_profile token_len=" << token.size();
        ret = get_my_profile_ptr(network_agent, token, http_code, http_body);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_my_profile ret=" << ret
            << " http_code=" << (http_code ? *http_code : 0u)
            << " body_len=" << (http_body ? http_body->size() : size_t(0));
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format("error network_agnet=%1%, ret = %2%") % network_agent % ret;
    }
    return ret;
}

int NetworkAgent::get_my_token(std::string ticket, unsigned int* http_code, std::string* http_body)
{
    int ret = 0;
    if (network_agent && get_my_token_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_my_token ticket_len=" << ticket.size();
        ret = get_my_token_ptr(network_agent, ticket, http_code, http_body);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_my_token ret=" << ret
            << " http_code=" << (http_code ? *http_code : 0u)
            << " body_len=" << (http_body ? http_body->size() : size_t(0));
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format("error network_agnet=%1%, ret = %2%") % network_agent % ret;
    }
    return ret;
}

int NetworkAgent::track_enable(bool enable)
{
    enable_track = enable;
    int ret = 0;
    if (network_agent && track_enable_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] track_enable enable=" << enable;
        ret = track_enable_ptr(network_agent, enable);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] track_enable ret=" << ret;
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format("error network_agnet=%1%, ret = %2%") % network_agent % ret;
    }
    return ret;
}

int NetworkAgent::track_remove_files()
{
    int ret = 0;
    if (network_agent && track_remove_files_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] track_remove_files enter";
        ret = track_remove_files_ptr(network_agent);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] track_remove_files ret=" << ret;
        if (ret) BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format("error network_agnet=%1%, ret = %2%") % network_agent % ret;
    }
    return ret;
}

int NetworkAgent::track_event(std::string evt_key, std::string content)
{
    if (!this->enable_track)
        return 0;

    if (!this->is_user_login())
        return 0;

    int ret = 0;
    if (network_agent && track_event_ptr) {
        BOOST_LOG_TRIVIAL(debug) << "[cloud_plugin] track_event key=" << evt_key << " content_len=" << content.size();
        ret = track_event_ptr(network_agent, evt_key, content);
        BOOST_LOG_TRIVIAL(debug) << "[cloud_plugin] track_event ret=" << ret;
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format("error network_agnet=%1%, ret = %2%") % network_agent % ret;
    }
    return ret;
}

int NetworkAgent::track_header(std::string header)
{
    if (!this->enable_track)
        return 0;
    int ret = 0;
    if (network_agent && track_header_ptr) {
        BOOST_LOG_TRIVIAL(debug) << "[cloud_plugin] track_header len=" << header.size() << " preview=" << cloud_trunc_body(header);
        ret = track_header_ptr(network_agent, header);
        BOOST_LOG_TRIVIAL(debug) << "[cloud_plugin] track_header ret=" << ret;
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format("error network_agnet=%1%, ret = %2%") % network_agent % ret;
    }
    return ret;
}

int NetworkAgent::track_update_property(std::string name, std::string value, std::string type)
{
    if (!this->enable_track)
        return 0;

    int ret = 0;
    if (network_agent && track_update_property_ptr) {
        BOOST_LOG_TRIVIAL(debug) << "[cloud_plugin] track_update_property name=" << name << " type=" << type << " value_len=" << value.size();
        ret = track_update_property_ptr(network_agent, name, value, type);
        BOOST_LOG_TRIVIAL(debug) << "[cloud_plugin] track_update_property ret=" << ret;
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format("error network_agnet=%1%, ret = %2%") % network_agent % ret;
    }
    return ret;
}

int NetworkAgent::track_get_property(std::string name, std::string& value, std::string type)
{
    if (!this->enable_track)
        return 0;

    int ret = 0;
    if (network_agent && track_get_property_ptr) {
        BOOST_LOG_TRIVIAL(debug) << "[cloud_plugin] track_get_property name=" << name << " type=" << type;
        ret = track_get_property_ptr(network_agent, name, value, type);
        BOOST_LOG_TRIVIAL(debug) << "[cloud_plugin] track_get_property ret=" << ret << " value_len=" << value.size();
        if (ret)
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format("error network_agnet=%1%, ret = %2%") % network_agent % ret;
    }
    return ret;
}

int NetworkAgent::put_model_mall_rating(int rating_id, int score, std::string content, std::vector<std::string> images, unsigned int &http_code, std::string &http_error)
{
    int ret = 0;
    if (network_agent && get_model_publish_url_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] put_model_mall_rating id=" << rating_id << " score=" << score
            << " images=" << images.size() << " content_len=" << content.size();
        ret = put_model_mall_rating_url_ptr(network_agent, rating_id, score, content, images, http_code, http_error);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] put_model_mall_rating ret=" << ret << " http_code=" << http_code;
        if (ret) BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%") % network_agent % ret;
    }
    return ret;
}

int NetworkAgent::get_oss_config(std::string &config, std::string country_code, unsigned int &http_code, std::string &http_error)
{
    int ret = 0;
    if (network_agent && get_oss_config_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_oss_config country=" << country_code;
        ret = get_oss_config_ptr(network_agent, config, country_code, http_code, http_error);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_oss_config ret=" << ret << " http_code=" << http_code << " config_len=" << config.size();
        if (ret) BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%") % network_agent % ret;
    }
    return ret;
}

int NetworkAgent::put_rating_picture_oss(std::string &config, std::string &pic_oss_path, std::string model_id, int profile_id, unsigned int &http_code, std::string &http_error)
{
    int ret = 0;
    if (network_agent && put_rating_picture_oss_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] put_rating_picture_oss model_id=" << model_id << " profile_id=" << profile_id
            << " config_len=" << config.size();
        ret = put_rating_picture_oss_ptr(network_agent, config, pic_oss_path, model_id, profile_id, http_code, http_error);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] put_rating_picture_oss ret=" << ret << " http_code=" << http_code;
        if (ret) BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%") % network_agent % ret;
    }
    return ret;
}

int NetworkAgent::get_model_mall_rating_result(int job_id, std::string &rating_result, unsigned int &http_code, std::string &http_error)
{
    int ret = 0;
    if (network_agent && get_model_mall_rating_result_ptr) {
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_model_mall_rating_result job_id=" << job_id;
        ret = get_model_mall_rating_result_ptr(network_agent, job_id, rating_result, http_code, http_error);
        BOOST_LOG_TRIVIAL(info) << "[cloud_plugin] get_model_mall_rating_result ret=" << ret
            << " http_code=" << http_code << " result_len=" << rating_result.size();
        if (ret) BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" error: network_agent=%1%, ret=%2%") % network_agent % ret;
    }
    return ret;
}

} //namespace
