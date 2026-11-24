// The sink headers need boost's own trivial.hpp, which libslic3r/Log.hpp locks out once it is included.
#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/smart_ptr/make_shared_object.hpp>

#include <catch2/catch.hpp>

#include "libslic3r/Print.hpp"
#include "libslic3r/GCode/ToolOrdering.hpp"
#include "libslic3r/GCode/WipeTower.hpp"

#include "test_data.hpp"

#include <set>
#include <sstream>

using namespace Slic3r;
using namespace Slic3r::Test;

namespace {

constexpr int CATEGORY_PETG = 300;
constexpr int CATEGORY_PVA  = 704;

// Filaments 1 and 2 are PETG, filament 3 is PVA.
DynamicPrintConfig make_separation_config(bool separate, bool skip_sparse_layers)
{
    constexpr unsigned num_filaments = 3;
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_num_filaments(num_filaments);

    config.set_key_value("enable_prime_tower", new ConfigOptionBool(true));
    config.set_key_value("enable_support", new ConfigOptionBool(false));
    config.set_key_value("timelapse_type", new ConfigOptionEnum<TimelapseType>(TimelapseType::tlTraditional));
    config.set_key_value("single_extruder_multi_material", new ConfigOptionBool(true));
    config.set_key_value("layer_height", new ConfigOptionFloat(0.2));
    // Not resized by set_num_filaments(), yet indexed by filament id when the tower is planned.
    config.set_key_value("filament_prime_volume", new ConfigOptionFloats(std::vector<double>(num_filaments, 45.)));
    config.set_key_value("filament_prime_volume_nc", new ConfigOptionFloats(std::vector<double>(num_filaments, 60.)));
    config.set_key_value("filament_diameter", new ConfigOptionFloats(std::vector<double>(num_filaments, 1.75)));
    config.set_key_value("filament_colour", new ConfigOptionStrings({"#FF0000", "#00FF00", "#FFFFFF"}));
    config.set_key_value("filament_type", new ConfigOptionStrings({"PETG", "PETG", "PVA"}));
    config.set_key_value("filament_adhesiveness_category", new ConfigOptionInts({CATEGORY_PETG, CATEGORY_PETG, CATEGORY_PVA}));
    config.set_key_value("wipe_tower_no_sparse_layers", new ConfigOptionBool(skip_sparse_layers));
    config.set_key_value("prime_tower_separate_materials", new ConfigOptionBool(separate));
    return config;
}

// Two 20 mm cubes: the first one in PETG #1, the second one in PETG #2 up to 10 mm and in PVA above.
// The tower thus carries PETG changes on every layer, and the PVA section only starts half way up.
void init_print(Print &print, Model &model, const DynamicPrintConfig &config_in)
{
    model.clear_objects();
    for (int i = 0; i < 2; ++i) {
        ModelObject *object = model.add_object();
        object->add_volume(mesh(TestMesh::cube_20x20x20));
        object->add_instance();
        object->instances.front()->set_offset(Vec3d(30.0 * i, 0.0, 0.0));
        object->config.set_key_value("extruder", new ConfigOptionInt(i + 1));
    }
    ModelConfig &upper_half = model.objects.back()->layer_config_ranges[{10.0, 20.0}];
    upper_half.set_key_value("layer_height", new ConfigOptionFloat(0.2));
    upper_half.set_key_value("extruder", new ConfigOptionInt(3));

    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_num_filaments(config_in.option<ConfigOptionFloats>("filament_diameter")->size());
    config.apply(config_in);

    for (ModelObject *mo : model.objects)
        mo->ensure_on_bed();

    print.apply(model, config);
    print.set_status_silent();
    print.set_no_check_flag(true);
}

struct TowerStats
{
    size_t tool_changes            = 0;
    size_t tower_layers            = 0;
    size_t layers_missing_category = 0;
};

// Replays the tool sequence the way the wipe tower sees it: a layer visits the tower with every filament it
// changes to, and with the filament loaded from the layer below, which changes away on the tower.
TowerStats tower_stats(const Print &print, bool skip_sparse_layers)
{
    const ToolOrdering     &ordering   = print.tool_ordering();
    const std::vector<int> &categories = print.config().filament_adhesiveness_category.values;

    int top_layer = -1;
    for (int i = 0; i < int(ordering.layer_tools().size()); ++i)
        if (ordering.layer_tools()[i].wipe_tower_partitions > 0)
            top_layer = i;

    std::set<int> all_categories;
    for (const LayerTools &lt : ordering.layer_tools())
        for (unsigned int filament : lt.extruders)
            all_categories.insert(categories[filament]);

    TowerStats stats;
    int        loaded = -1;
    for (int i = 0; i <= top_layer; ++i) {
        const LayerTools &lt      = ordering.layer_tools()[i];
        size_t            changes = 0;
        int               current = loaded;
        for (unsigned int filament : lt.extruders) {
            if (current >= 0 && unsigned(current) != filament)
                ++changes;
            current = int(filament);
        }
        stats.tool_changes += changes;

        if (lt.has_wipe_tower && (! skip_sparse_layers || changes > 0)) {
            ++stats.tower_layers;
            std::set<int> visiting;
            if (loaded >= 0)
                visiting.insert(categories[loaded]);
            for (unsigned int filament : lt.extruders)
                visiting.insert(categories[filament]);
            if (visiting != all_categories)
                ++stats.layers_missing_category;
        }
        if (! lt.extruders.empty())
            loaded = int(lt.extruders.back());
    }
    return stats;
}

// Collects everything down to info level logged while it is alive.
class LogCapture
{
public:
    using Sink = boost::log::sinks::synchronous_sink<boost::log::sinks::text_ostream_backend>;

    LogCapture() : m_level(get_logging_level()), m_stream(boost::make_shared<std::ostringstream>()), m_sink(boost::make_shared<Sink>())
    {
        m_sink->locked_backend()->add_stream(m_stream);
        m_sink->locked_backend()->auto_flush(true);
        boost::log::core::get()->add_sink(m_sink);
        set_logging_level(3);
    }
    ~LogCapture()
    {
        set_logging_level(m_level);
        boost::log::core::get()->remove_sink(m_sink);
    }

    std::string text() const { return m_stream->str(); }

private:
    unsigned                              m_level;
    boost::shared_ptr<std::ostringstream> m_stream;
    boost::shared_ptr<Sink>               m_sink;
};

bool reports_separation_failure(const std::string &log)
{
    for (const char *marker : {"despite prime_tower_separate_materials", "is left empty", "gets another material"})
        if (log.find(marker) != std::string::npos)
            return true;
    return false;
}

TowerStats process_and_collect(bool separate, bool skip_sparse_layers)
{
    Print      print;
    Model      model;
    LogCapture log;
    init_print(print, model, make_separation_config(separate, skip_sparse_layers));
    REQUIRE(print.has_wipe_tower());
    print.process();
    REQUIRE(print.wipe_tower_data().number_of_toolchanges > 0);
    if (separate) {
        INFO(log.text());
        REQUIRE(log.text().find("material separation added") != std::string::npos);
        REQUIRE_FALSE(reports_separation_failure(log.text()));
    }
    return tower_stats(print, skip_sparse_layers);
}

} // namespace

TEST_CASE("prime_tower_separate_materials: every tower layer is visited by every material", "[PrimeTowerSeparateMaterials]")
{
    const TowerStats mixed     = process_and_collect(false, false);
    const TowerStats separated = process_and_collect(true, false);

    // Without the option the PVA section stands on PETG fill below 10 mm.
    REQUIRE(mixed.layers_missing_category > 0);
    REQUIRE(separated.layers_missing_category == 0);
    REQUIRE(separated.tower_layers == mixed.tower_layers);
    REQUIRE(separated.tool_changes > mixed.tool_changes);
}

TEST_CASE("prime_tower_separate_materials: no sparse layers keeps the tower height", "[PrimeTowerSeparateMaterials]")
{
    const TowerStats mixed     = process_and_collect(false, true);
    const TowerStats separated = process_and_collect(true, true);

    REQUIRE(mixed.layers_missing_category > 0);
    REQUIRE(separated.layers_missing_category == 0);
    // The extra visits split changes the layers already have, so no tower layer is added.
    REQUIRE(separated.tower_layers == mixed.tower_layers);
    REQUIRE(separated.tool_changes > mixed.tool_changes);
}

TEST_CASE("prime_tower_separate_materials: ignored with smooth timelapse", "[PrimeTowerSeparateMaterials]")
{
    DynamicPrintConfig config = make_separation_config(true, false);
    config.set_key_value("timelapse_type", new ConfigOptionEnum<TimelapseType>(TimelapseType::tlSmooth));

    Print print;
    Model model;
    init_print(print, model, config);
    REQUIRE_FALSE(wipe_tower_materials_separated(print.config()));
}
