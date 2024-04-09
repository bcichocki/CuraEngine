//Copyright (c) 2018 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#include <clipper/clipper.hpp>

#include "Application.h" //To get settings.
#include "ExtruderTrain.h"
#include "raft.h"
#include "sliceDataStorage.h"
#include "support.h"
#include "settings/EnumSettings.h" //For EPlatformAdhesion.
#include "utils/math.h"

namespace cura
{

void Raft::generate(SliceDataStorage& storage)
{
    assert(storage.raftOutline.size() == 0 && "Raft polygon isn't generated yet, so should be empty!");

    const auto& mesh_layer_group0 = Application::getInstance().scene.current_mesh_group->layer_group0;
	const auto& settings = Application::getInstance().scene.current_mesh_group->settings.get<ExtruderTrain&>("adhesion_extruder_nr").settings;

    const coord_t distance = mesh_layer_group0.raft_margin;
    constexpr bool include_support = true;
    constexpr bool include_prime_tower = true;
    storage.raftOutline = storage.getLayerOutlines(0, include_support, include_prime_tower).offset(distance, ClipperLib::jtRound);
    const coord_t shield_line_width_layer0 = mesh_layer_group0.skirt_brim_line_width;

	if (storage.draft_protection_shield.size() > 0)
    {
        Polygons draft_shield_raft = storage.draft_protection_shield.offset(shield_line_width_layer0) // start half a line width outside shield
                                        .difference(storage.draft_protection_shield.offset(-distance - shield_line_width_layer0 / 2, ClipperLib::jtRound)); // end distance inside shield
        storage.raftOutline = storage.raftOutline.unionPolygons(draft_shield_raft);
    }

	const coord_t smoothing = mesh_layer_group0.raft_smoothing;
    storage.raftOutline = storage.raftOutline.offset(smoothing, ClipperLib::jtRound).offset(-smoothing, ClipperLib::jtRound); // remove small holes and smooth inward corners
}

coord_t Raft::getTotalThickness()
{
    const auto& mesh_layer_group0 = Application::getInstance().scene.current_mesh_group->layer_group0;

	return mesh_layer_group0.raft_base_thickness
        + mesh_layer_group0.raft_interface_thickness
        + mesh_layer_group0.raft_surface_layers * mesh_layer_group0.raft_surface_thickness;
}

coord_t Raft::getZdiffBetweenRaftAndLayer1()
{
    const auto& mesh_group_settings = Application::getInstance().scene.current_mesh_group->settings;
    const auto& mesh_layer_group0 = Application::getInstance().scene.current_mesh_group->layer_group0;

    const ExtruderTrain& train = mesh_group_settings.get<ExtruderTrain&>("adhesion_extruder_nr");

	if (mesh_group_settings.get<EPlatformAdhesion>("adhesion_type") != EPlatformAdhesion::RAFT)
        return 0;

	const coord_t airgap = std::max(coord_t(0), mesh_layer_group0.raft_airgap);
    const coord_t layer_0_overlap = mesh_group_settings.get<coord_t>("layer_0_z_overlap");
    const coord_t layer_height_0 = mesh_group_settings.get<coord_t>("layer_height_0");
    const coord_t z_diff_raft_to_bottom_of_layer_1 = std::max(coord_t(0), airgap + layer_height_0 - layer_0_overlap);

	return z_diff_raft_to_bottom_of_layer_1;
}

size_t Raft::getFillerLayerCount()
{
    const coord_t normal_layer_height = Application::getInstance().scene.current_mesh_group->settings.get<coord_t>("layer_height");
    return round_divide(getZdiffBetweenRaftAndLayer1(), normal_layer_height);
}

coord_t Raft::getFillerLayerHeight()
{
    const Settings& mesh_group_settings = Application::getInstance().scene.current_mesh_group->settings;

	if (mesh_group_settings.get<EPlatformAdhesion>("adhesion_type") != EPlatformAdhesion::RAFT)
    {
        const coord_t normal_layer_height = mesh_group_settings.get<coord_t>("layer_height");
        return normal_layer_height;
    }

	return round_divide(getZdiffBetweenRaftAndLayer1(), getFillerLayerCount());
}


size_t Raft::getTotalExtraLayers()
{
    const ExtruderTrain& train = Application::getInstance().scene.current_mesh_group->settings.get<ExtruderTrain&>("adhesion_extruder_nr");

	if (train.settings.get<EPlatformAdhesion>("adhesion_type") != EPlatformAdhesion::RAFT)
        return 0;

    const auto& mesh_layer_group0 = Application::getInstance().scene.current_mesh_group->layer_group0;

    return 2 + mesh_layer_group0.raft_surface_layers + getFillerLayerCount();
}


}//namespace cura
