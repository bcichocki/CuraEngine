//Copyright (c) 2018 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#include "GCodePath.h"
#include "../GCodePathConfig.h"
#include <string>
#include <cassert>

namespace cura
{
GCodePath::GCodePath(const GCodePathConfig& config, const std::string& mesh_id, const SpaceFillType space_fill_type, const Ratio flow, size_t group_id, const Ratio speed_factor) :
	config(&config),
	mesh_id(mesh_id),
	space_fill_type(space_fill_type),
	flow(flow),
	group_id(group_id),
	speed_factor(speed_factor),
	retract(false),
	perform_z_hop(false),
	perform_prime(false),
	skip_agressive_merge_hint(false),
	points(std::vector<Point>()),
	done(false),
	fan_speed(GCodePathConfig::FAN_SPEED_DEFAULT),
	estimates(TimeMaterialEstimates())
{
	static constexpr size_t ID_DOWNSKIN = 0;
	static constexpr size_t ID_UPSKIN = 1;
	static constexpr size_t ID_LAYER_0 = 2;

	if(group_id > ID_LAYER_0)
		this->flow = flow * (1 << (group_id - ID_LAYER_0));

	//if(group_id > 3)
	//	this->flow = flow * (1 << (group_id - 3));
}

bool GCodePath::isTravelPath() const
{
    return config->isTravelPath();
}

double GCodePath::getExtrusionMM3perMM() const
{
    return flow * config->getExtrusionMM3perMM();
}

coord_t GCodePath::getLineWidthForLayerView() const
{
    return flow * config->getLineWidth() * config->getFlowRatio();
}

void GCodePath::setFanSpeed(double fan_speed)
{
    this->fan_speed = fan_speed;
}

double GCodePath::getFanSpeed() const
{
    return (fan_speed >= 0 && fan_speed <= 100) ? fan_speed : config->getFanSpeed();
}

}
