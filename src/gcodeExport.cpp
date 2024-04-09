//Copyright (c) 2019 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#include <assert.h>
#include <cmath>
#include <iomanip>
#include <stdarg.h>
#include <common/stringutils.h>

#include "Application.h" //To send layer view data.
#include "ExtruderTrain.h"
#include "gcodeExport.h"
#include "PrintFeature.h"
#include "RetractionConfig.h"
#include "settings/types/LayerIndex.h"
#include "utils/Date.h"
#include "utils/logoutput.h"
#include "utils/string.h" // MMtoStream, PrecisionedDouble
#include "WipeScriptConfig.h"

namespace cura {

std::string transliterate(const std::string& text)
{
	// For now, just replace all non-ascii characters with '?'.
	// This function can be expaned if we need more complex transliteration.

	std::ostringstream stream;

	for(const char& c : text)
		stream << static_cast<char>((c >= 0) ? c : '?');

	return stream.str();
}

GCodeExport::GCodeExport() : output_stream(&std::cout), currentPosition(0, 0, MM2INT(20)), layer_nr(0)
{
	*output_stream << std::fixed;

	current_e_value = 0;
	current_extruder = 0;
	current_fan_speed = -1;

	total_print_times = std::vector<Duration>(static_cast<unsigned char>(PrintFeatureType::NumPrintFeatureTypes), 0.0);

	currentSpeed = 1;
	current_print_acceleration = -1;
	current_travel_acceleration = -1;
	current_jerk = -1;

	is_z_hopped = 0;
	initial_bed_temp = 0;
	build_volume_temperature = 0;
	machine_heated_build_volume = false;

	fan_number = 0;
	use_extruder_offset_to_offset_coords = false;

	total_bounding_box = AABB3D();

	const Scene& scene = Application::getInstance().scene;
	auto& settings = scene.current_mesh_group->settings;

	extruder_base = settings.get<size_t>("machine_extruder_base", std::to_string(EXTRUDER_BASE));

	extruder_char = settings.get<char>("machine_extruder_char", "A");

	extruder_info = settings.get<bool>("machine_extruder_info", "false");

	for(int n = 0; n < MAX_EXTRUDERS; n++)
		extruder_attr[n].extruderCharacter = extruder_char; // extruder_char + n
}

GCodeExport::~GCodeExport()
{
}

void GCodeExport::preSetup(const size_t start_extruder)
{
	current_extruder = start_extruder;

	const Scene& scene = Application::getInstance().scene;
	std::vector<MeshGroup>::iterator mesh_group = scene.current_mesh_group;

	use_extruder_offset_to_offset_coords = mesh_group->settings.get<bool>("machine_use_extruder_offset_to_offset_coords");
	const size_t extruder_count = Application::getInstance().scene.extruders.size();

	for(size_t extruder_nr = 0; extruder_nr < extruder_count; extruder_nr++)
	{
		const ExtruderTrain& train = scene.extruders[extruder_nr];
		setFilamentDiameter(extruder_nr, train.settings.get<coord_t>("material_diameter"));

		extruder_attr[extruder_nr].last_retraction_prime_speed = train.settings.get<Velocity>("retraction_prime_speed"); // the alternative would be switch_extruder_prime_speed, but dual extrusion might not even be configured...
		extruder_attr[extruder_nr].fan_number = train.settings.get<size_t>("machine_extruder_cooling_fan_number");
	}

	estimateCalculator.setFirmwareDefaults(mesh_group->settings);
}

void GCodeExport::setInitialAndBuildVolumeTemps(const unsigned int start_extruder_nr)
{
	const Scene& scene = Application::getInstance().scene;
	const size_t extruder_count = Application::getInstance().scene.extruders.size();

	for(size_t extruder_nr = 0; extruder_nr < extruder_count; extruder_nr++)
	{
		const ExtruderTrain& train = scene.extruders[extruder_nr];

		const Temperature print_temp_0 = train.settings.get<Temperature>("material_print_temperature_layer_0");
		const Temperature print_temp_here = (print_temp_0 != 0) ? print_temp_0 : train.settings.get<Temperature>("material_print_temperature");
		const Temperature temp = (extruder_nr == start_extruder_nr) ? print_temp_here : train.settings.get<Temperature>("material_standby_temperature");

		setInitialTemp(extruder_nr, temp);
	}

	initial_bed_temp = scene.current_mesh_group->settings.get<Temperature>("material_bed_temperature_layer_0");
	machine_heated_build_volume = scene.current_mesh_group->settings.get<bool>("machine_heated_build_volume");
	build_volume_temperature = machine_heated_build_volume ? scene.current_mesh_group->settings.get<Temperature>("build_volume_temperature") : Temperature(0);
}

void GCodeExport::setInitialTemp(int extruder_nr, double temp)
{
	extruder_attr[extruder_nr].initial_temp = temp;
}

const std::string GCodeExport::flavorToString(const EGCodeFlavor& flavor) const
{
	return "Tina3D";
}

std::string GCodeExport::getFileHeader(const std::vector<bool>& extruder_is_used, const Duration* print_time, const std::vector<double>& filament_used, const std::vector<std::string>& mat_ids)
{
	std::ostringstream prefix;

	const size_t extruder_count = Application::getInstance().scene.extruders.size();

	prefix << ";Generated with CuraEngine for Tina3D " VERSION << new_line;
	prefix << ";FLAVOR:" << flavorToString(flavor) << new_line;
	prefix << ";TIME:" << ((print_time) ? static_cast<int>(*print_time) : 6666) << new_line;

	prefix << ";Filament used: ";

	if(filament_used.size() > 0)
	{
		for(unsigned i = 0; i < filament_used.size(); ++i)
		{
			if(i > 0)
				prefix << ", ";

			prefix << filament_used[i] / (1000 * extruder_attr[i].filament_area) << "m";
		}
	}
	else
	{
		prefix << "0m";
	}

	prefix << new_line;
	prefix << ";Layer height: " << Application::getInstance().scene.current_mesh_group->settings.get<double>("layer_height") << new_line;

	prefix << ";MINX:" << INT2MM(total_bounding_box.min.x) << new_line;
	prefix << ";MINY:" << INT2MM(total_bounding_box.min.y) << new_line;
	prefix << ";MINZ:" << INT2MM(total_bounding_box.min.z) << new_line;
	prefix << ";MAXX:" << INT2MM(total_bounding_box.max.x) << new_line;
	prefix << ";MAXY:" << INT2MM(total_bounding_box.max.y) << new_line;
	prefix << ";MAXZ:" << INT2MM(total_bounding_box.max.z) << new_line;

	return prefix.str();
}

void GCodeExport::setLayerNr(unsigned int layer_nr_)
{
	layer_nr = layer_nr_;
}

void GCodeExport::setOutputStream(std::ostream* stream)
{
	output_stream = stream;
	*output_stream << std::fixed;
}

bool GCodeExport::getExtruderIsUsed(const int extruder_nr) const
{
	assert(extruder_nr >= 0);
	assert(extruder_nr < MAX_EXTRUDERS);

	return extruder_attr[extruder_nr].is_used;
}

Point GCodeExport::getGcodePos(const coord_t x, const coord_t y, const int extruder_train) const
{
	if(use_extruder_offset_to_offset_coords)
	{
		const Settings& extruder_settings = Application::getInstance().scene.extruders[extruder_train].settings;
		return Point(x - extruder_settings.get<coord_t>("machine_nozzle_offset_x"), y - extruder_settings.get<coord_t>("machine_nozzle_offset_y"));
	}

	return Point(x, y);
}

void GCodeExport::setZ(int z)
{
	current_layer_z = z;
}

void GCodeExport::setFlowRateExtrusionSettings(double max_extrusion_offset, double extrusion_offset_factor)
{
	this->max_extrusion_offset = max_extrusion_offset;
	this->extrusion_offset_factor = extrusion_offset_factor;
}

Point3 GCodeExport::getPosition() const
{
	return currentPosition;
}
	
Point GCodeExport::getPositionXY() const
{
	return Point(currentPosition.x, currentPosition.y);
}

int GCodeExport::getPositionZ() const
{
	return currentPosition.z;
}

int GCodeExport::getExtruderNr() const
{
	return current_extruder;
}

void GCodeExport::setFilamentDiameter(const size_t extruder, const coord_t diameter)
{
	const double r = INT2MM(diameter) / 2.0;
	const double area = M_PI * r * r;
	extruder_attr[extruder].filament_area = area;
}

double GCodeExport::getCurrentExtrudedVolume() const
{
	double extrusion_amount = current_e_value;
	const Settings& extruder_settings = Application::getInstance().scene.extruders[current_extruder].settings;

	if(!extruder_settings.get<bool>("machine_firmware_retract"))
	{
		// no E values are changed to perform a retraction
		extrusion_amount -= extruder_attr[current_extruder].retraction_e_amount_at_e_start; // subtract the increment in E which was used for the first unretraction instead of extrusion
		extrusion_amount += extruder_attr[current_extruder].retraction_e_amount_current; // add the decrement in E which the filament is behind on extrusion due to the last retraction
	}

	if(is_volumetric)
		return extrusion_amount;
	else
		return extrusion_amount * extruder_attr[current_extruder].filament_area;
}

double GCodeExport::eToMm(double e)
{
	if(is_volumetric)
		return e / extruder_attr[current_extruder].filament_area;
	else
		return e;
}

double GCodeExport::mm3ToE(double mm3)
{
	if(is_volumetric)
		return mm3;
	else
		return mm3 / extruder_attr[current_extruder].filament_area;
}

double GCodeExport::mmToE(double mm)
{
	if(is_volumetric)
		return mm * extruder_attr[current_extruder].filament_area;
	else
		return mm;
}

double GCodeExport::eToMm3(double e, size_t extruder)
{
	if(is_volumetric)
		return e;
	else
		return e * extruder_attr[extruder].filament_area;
}

double GCodeExport::getTotalFilamentUsed(size_t extruder_nr)
{
	if(extruder_nr == current_extruder)
		return extruder_attr[extruder_nr].totalFilament + getCurrentExtrudedVolume();

	return extruder_attr[extruder_nr].totalFilament;
}

std::vector<Duration> GCodeExport::getTotalPrintTimePerFeature()
{
	return total_print_times;
}

double GCodeExport::getSumTotalPrintTimes()
{
	double sum = 0.0;

	for(double item : getTotalPrintTimePerFeature())
		sum += item;

	return sum;
}

void GCodeExport::resetTotalPrintTimeAndFilament()
{
	for(auto& t : total_print_times)
		t = 0.0;

	for(auto& e : extruder_attr)
	{
		e.totalFilament = 0.0;
		e.currentTemperature = 0;
		e.waited_for_temperature = false;
	}

	current_e_value = 0.0;
	estimateCalculator.reset();
}

void GCodeExport::updateTotalPrintTime()
{
	std::vector<Duration> estimates = estimateCalculator.calculate();

	for(size_t i = 0; i < estimates.size(); i++)
		total_print_times[i] += estimates[i];

	estimateCalculator.reset();
	writeTimeComment(getSumTotalPrintTimes());
}

void GCodeExport::writeComment(const std::string& unsanitized_comment)
{
	const std::string comment = transliterate(unsanitized_comment);

	*output_stream << ";";

	for(const char c : comment)
	{
		if(c == '\n')
			*output_stream << new_line << ";";
		else
			*output_stream << c;
	}

	*output_stream << new_line;
}

void GCodeExport::writeTimeComment(const Duration time)
{
	*output_stream << ";TIME_ELAPSED:" << time << new_line;
}

void GCodeExport::writeTypeComment(const PrintFeatureType& type)
{
	switch(type)
	{
	case PrintFeatureType::OuterWall:
		*output_stream << ";TYPE:WALL-OUTER" << new_line;
		break;

	case PrintFeatureType::InnerWall:
		*output_stream << ";TYPE:WALL-INNER" << new_line;
		break;

	case PrintFeatureType::Skin:
		*output_stream << ";TYPE:SKIN" << new_line;
		break;

	case PrintFeatureType::Support:
		*output_stream << ";TYPE:SUPPORT" << new_line;
		break;

	case PrintFeatureType::SkirtBrim:
		*output_stream << ";TYPE:SKIRT" << new_line;
		break;

	case PrintFeatureType::Infill:
		*output_stream << ";TYPE:FILL" << new_line;
		break;

	case PrintFeatureType::SupportInfill:
		*output_stream << ";TYPE:SUPPORT" << new_line;
		break;

	case PrintFeatureType::SupportInterface:
		*output_stream << ";TYPE:SUPPORT-INTERFACE" << new_line;
		break;

	case PrintFeatureType::PrimeTower:
		*output_stream << ";TYPE:PRIME-TOWER" << new_line;
		break;

	case PrintFeatureType::MoveCombing:
	case PrintFeatureType::MoveRetraction:
	case PrintFeatureType::NoneType:
	case PrintFeatureType::NumPrintFeatureTypes:
		// do nothing
		break;
	}
}

void GCodeExport::writeLayerComment(const LayerIndex layer_nr)
{
	*output_stream << ";LAYER:" << layer_nr << new_line;
}

void GCodeExport::writeLayerCountComment(const size_t layer_count)
{
	*output_stream << ";LAYER_COUNT:" << layer_count << new_line;
}

void GCodeExport::resetExtrusionValue()
{
	if(!relative_extrusion)
		*output_stream << "G92 " << extruder_attr[current_extruder].extruderCharacter << "0" << new_line;

	double current_extruded_volume = getCurrentExtrudedVolume();
	extruder_attr[current_extruder].totalFilament += current_extruded_volume;

	for(double& extruded_volume_at_retraction : extruder_attr[current_extruder].extruded_volume_at_previous_n_retractions)
	{
		// update the extruded_volume_at_previous_n_retractions only of the current extruder, since other extruders don't extrude the current volume
		extruded_volume_at_retraction -= current_extruded_volume;
	}

	current_e_value = 0.0;
	extruder_attr[current_extruder].retraction_e_amount_at_e_start = extruder_attr[current_extruder].retraction_e_amount_current;
}

void GCodeExport::writeDelay(const Duration& time_amount)
{
	*output_stream << "G4 P" << int(time_amount * 1000) << new_line;
	estimateCalculator.addTime(time_amount);
}

void GCodeExport::writeTravel(const Point& p, const Velocity& speed)
{
	writeTravel(Point3(p.X, p.Y, current_layer_z), speed);
}

void GCodeExport::writeExtrusion(const Point& p, const Velocity& speed, double extrusion_mm3_per_mm, PrintFeatureType feature, bool update_extrusion_offset)
{
	writeExtrusion(Point3(p.X, p.Y, current_layer_z), speed, extrusion_mm3_per_mm, feature, update_extrusion_offset);
}

void GCodeExport::writeTravel(const Point3& p, const Velocity& speed)
{
	writeTravel(p.x, p.y, p.z + is_z_hopped, speed);
}

void GCodeExport::writeExtrusion(const Point3& p, const Velocity& speed, double extrusion_mm3_per_mm, PrintFeatureType feature, bool update_extrusion_offset)
{
	writeExtrusion(p.x, p.y, p.z, speed, extrusion_mm3_per_mm, feature, update_extrusion_offset);
}

void GCodeExport::writeTravel(const coord_t& x, const coord_t& y, const coord_t& z, const Velocity& speed)
{
	if(currentPosition.x == x && currentPosition.y == y && currentPosition.z == z)
		return;

	const PrintFeatureType travel_move_type = extruder_attr[current_extruder].retraction_e_amount_current ? PrintFeatureType::MoveRetraction : PrintFeatureType::MoveCombing;
	const int display_width = extruder_attr[current_extruder].retraction_e_amount_current ? MM2INT(0.2) : MM2INT(0.1);
	const double layer_height = Application::getInstance().scene.current_mesh_group->settings.get<double>("layer_height");

	*output_stream << "G0";
	writeFXYZE(speed, x, y, z, current_e_value, travel_move_type);
	*output_stream << new_line;
}

void GCodeExport::writeExtrusion(const int x, const int y, const int z, const Velocity& speed, const double extrusion_mm3_per_mm, const PrintFeatureType& feature, const bool update_extrusion_offset)
{
	if(currentPosition.x == x && currentPosition.y == y && currentPosition.z == z)
		return;

	if(std::isinf(extrusion_mm3_per_mm))
	{
		logError("Extrusion rate is infinite!");
		assert(false && "Infinite extrusion move!");
		throw(1);
	}

	if(std::isnan(extrusion_mm3_per_mm))
	{
		logError("Extrusion rate is not a number!");
		assert(false && "NaN extrusion move!");
		throw(1);
	}

	if(extrusion_mm3_per_mm < 0.0)
	{
		logWarning("Warning! Negative extrusion move!\n");
	}

	const double extrusion_per_mm = mm3ToE(extrusion_mm3_per_mm);

	if(is_z_hopped > 0)
		writeZhopEnd();

	Point3 diff = Point3(x, y, z) - currentPosition;

	writeUnretractionAndPrime();

	//flow rate compensation
	double extrusion_offset = 0;

	if(diff.vSizeMM())
	{
		extrusion_offset = speed * extrusion_mm3_per_mm * extrusion_offset_factor;

		if(extrusion_offset > max_extrusion_offset)
			extrusion_offset = max_extrusion_offset;
	}

	// write new value of extrusion_offset, which will be remembered.
	if(update_extrusion_offset && (extrusion_offset != current_e_offset))
	{
		current_e_offset = extrusion_offset;
		*output_stream << ";FLOW_RATE_COMPENSATED_OFFSET = " << current_e_offset << new_line;
	}

	extruder_attr[current_extruder].last_e_value_after_wipe += extrusion_per_mm * diff.vSizeMM();

	double delta = extrusion_per_mm * diff.vSizeMM();
	double new_e_value = current_e_value + delta;

	*output_stream << "G1";
	writeFXYZE(speed, x, y, z, new_e_value, feature);

	if(extruder_info)
		*output_stream << fmt::format("\t;EXTRUSION: {:.3f} ({:.3f}/mm {:.3f}mm3/mm)", delta, extrusion_per_mm, extrusion_mm3_per_mm);

	*output_stream << new_line;
}

void GCodeExport::writeFXYZE(const Velocity& speed, const int x, const int y, const int z, const double e, const PrintFeatureType& feature)
{
	if(currentSpeed != speed)
	{
		*output_stream << " F" << PrecisionedDouble{1, speed * 60};
		currentSpeed = speed;
	}

	Point gcode_pos = getGcodePos(x, y, current_extruder);
	total_bounding_box.include(Point3(gcode_pos.X, gcode_pos.Y, z));

	*output_stream << " X" << MMtoStream{gcode_pos.X} << " Y" << MMtoStream{gcode_pos.Y};

	if(z != currentPosition.z)
		*output_stream << " Z" << MMtoStream{z};

	if(e + current_e_offset != current_e_value)
	{
		const double output_e = (relative_extrusion) ? e + current_e_offset - current_e_value : e + current_e_offset;
		*output_stream << " " << extruder_attr[current_extruder].extruderCharacter << PrecisionedDouble{5, output_e};
	}

	currentPosition = Point3(x, y, z);
	current_e_value = e;
	estimateCalculator.plan(TimeEstimateCalculator::Position(INT2MM(x), INT2MM(y), INT2MM(z), eToMm(e)), speed, feature);
}

void GCodeExport::writeUnretractionAndPrime()
{
	const double prime_volume = extruder_attr[current_extruder].prime_volume;
	const double prime_volume_e = mm3ToE(prime_volume);
	current_e_value += prime_volume_e;

	if(extruder_attr[current_extruder].retraction_e_amount_current)
	{
		const Settings& extruder_settings = Application::getInstance().scene.extruders[current_extruder].settings;
		
		if(extruder_settings.get<bool>("machine_firmware_retract"))
		{
			// note that BFB is handled differently
			*output_stream << "G11" << new_line;

			//Assume default UM2 retraction settings.
			if(prime_volume != 0)
			{
				const double output_e = (relative_extrusion) ? prime_volume_e : current_e_value;
				*output_stream << "G1 F" << PrecisionedDouble{1, extruder_attr[current_extruder].last_retraction_prime_speed * 60} << " " << extruder_attr[current_extruder].extruderCharacter << PrecisionedDouble{5, output_e} << new_line;
				currentSpeed = extruder_attr[current_extruder].last_retraction_prime_speed;
			}
		
			estimateCalculator.plan(TimeEstimateCalculator::Position(INT2MM(currentPosition.x), INT2MM(currentPosition.y), INT2MM(currentPosition.z), eToMm(current_e_value)), 25.0, PrintFeatureType::MoveRetraction);
		}
		else
		{
			current_e_value += extruder_attr[current_extruder].retraction_e_amount_current;
			const double output_e = (relative_extrusion) ? extruder_attr[current_extruder].retraction_e_amount_current + prime_volume_e : current_e_value;
			*output_stream << "G1 F" << PrecisionedDouble{1, extruder_attr[current_extruder].last_retraction_prime_speed * 60} << " " << extruder_attr[current_extruder].extruderCharacter << PrecisionedDouble{5, output_e} << new_line;
			currentSpeed = extruder_attr[current_extruder].last_retraction_prime_speed;
			estimateCalculator.plan(TimeEstimateCalculator::Position(INT2MM(currentPosition.x), INT2MM(currentPosition.y), INT2MM(currentPosition.z), eToMm(current_e_value)), currentSpeed, PrintFeatureType::MoveRetraction);
		}
	}
	else if(prime_volume != 0.0)
	{
		const double output_e = (relative_extrusion) ? prime_volume_e : current_e_value;
		*output_stream << "G1 F" << PrecisionedDouble{1, extruder_attr[current_extruder].last_retraction_prime_speed * 60} << " " << extruder_attr[current_extruder].extruderCharacter;
		*output_stream << PrecisionedDouble{5, output_e} << new_line;
		currentSpeed = extruder_attr[current_extruder].last_retraction_prime_speed;
		estimateCalculator.plan(TimeEstimateCalculator::Position(INT2MM(currentPosition.x), INT2MM(currentPosition.y), INT2MM(currentPosition.z), eToMm(current_e_value)), currentSpeed, PrintFeatureType::NoneType);
	}

	extruder_attr[current_extruder].prime_volume = 0.0;

	if(getCurrentExtrudedVolume() > 10000.0) //According to https://github.com/Ultimaker/CuraEngine/issues/14 having more then 21m of extrusion causes inaccuracies. So reset it every 10m, just to be sure.
		resetExtrusionValue();

	if(extruder_attr[current_extruder].retraction_e_amount_current)
		extruder_attr[current_extruder].retraction_e_amount_current = 0.0;
}

void GCodeExport::writeRetraction(const RetractionConfig& config, bool force, bool extruder_switch)
{
	ExtruderTrainAttributes& extr_attr = extruder_attr[current_extruder];

	double old_retraction_e_amount = extr_attr.retraction_e_amount_current;
	double new_retraction_e_amount = mmToE(config.distance);
	double retraction_diff_e_amount = old_retraction_e_amount - new_retraction_e_amount;

	if(std::abs(retraction_diff_e_amount) < 0.000001)
		return;

	{
		// handle retraction limitation
		double current_extruded_volume = getCurrentExtrudedVolume();
		std::deque<double>& extruded_volume_at_previous_n_retractions = extr_attr.extruded_volume_at_previous_n_retractions;
	
		while(extruded_volume_at_previous_n_retractions.size() > config.retraction_count_max && !extruded_volume_at_previous_n_retractions.empty())
		{
			// extruder switch could have introduced data which falls outside the retraction window
			// also the retraction_count_max could have changed between the last retraction and this
			extruded_volume_at_previous_n_retractions.pop_back();
		}
		
		if(!force && config.retraction_count_max <= 0)
			return;
		
		if(!force && extruded_volume_at_previous_n_retractions.size() == config.retraction_count_max
			&& current_extruded_volume < extruded_volume_at_previous_n_retractions.back() + config.retraction_extrusion_window * extr_attr.filament_area)
				return;
		
		extruded_volume_at_previous_n_retractions.push_front(current_extruded_volume);
		
		if(extruded_volume_at_previous_n_retractions.size() == config.retraction_count_max + 1)
			extruded_volume_at_previous_n_retractions.pop_back();
	}

	const Settings& extruder_settings = Application::getInstance().scene.extruders[current_extruder].settings;

	if(extruder_settings.get<bool>("machine_firmware_retract"))
	{
		if(extruder_switch && extr_attr.retraction_e_amount_current)
			return;
		
		*output_stream << "G10" << new_line;
		
		//Assume default UM2 retraction settings.
		estimateCalculator.plan(TimeEstimateCalculator::Position(INT2MM(currentPosition.x), INT2MM(currentPosition.y), INT2MM(currentPosition.z), eToMm(current_e_value + retraction_diff_e_amount)), 25, PrintFeatureType::MoveRetraction); // TODO: hardcoded values!
	}
	else
	{
		double speed = ((retraction_diff_e_amount < 0.0) ? config.speed : extr_attr.last_retraction_prime_speed) * 60;
		current_e_value += retraction_diff_e_amount;
		const double output_e = (relative_extrusion) ? retraction_diff_e_amount : current_e_value;

		*output_stream << "G1 F" << PrecisionedDouble{1, speed} << " " << extr_attr.extruderCharacter << PrecisionedDouble{5, output_e} << new_line;
		
		currentSpeed = speed;
		estimateCalculator.plan(TimeEstimateCalculator::Position(INT2MM(currentPosition.x), INT2MM(currentPosition.y), INT2MM(currentPosition.z), eToMm(current_e_value)), currentSpeed, PrintFeatureType::MoveRetraction);
		extr_attr.last_retraction_prime_speed = config.primeSpeed;
	}

	extr_attr.retraction_e_amount_current = new_retraction_e_amount; // suppose that for UM2 the retraction amount in the firmware is equal to the provided amount
	extr_attr.prime_volume += config.prime_volume;
}

void GCodeExport::writeZhopStart(const coord_t hop_height, Velocity speed/*= 0*/)
{
	if(hop_height > 0)
	{
		if(speed == 0)
		{
			const ExtruderTrain& extruder = Application::getInstance().scene.extruders[current_extruder];
			speed = extruder.settings.get<Velocity>("speed_z_hop");
		}

		is_z_hopped = hop_height;
		currentSpeed = speed;
		*output_stream << "G1 F" << PrecisionedDouble{1, speed * 60} << " Z" << MMtoStream{current_layer_z + is_z_hopped} << new_line;
		total_bounding_box.includeZ(current_layer_z + is_z_hopped);
		assert(speed > 0.0 && "Z hop speed should be positive.");
	}
}

void GCodeExport::writeZhopEnd(Velocity speed/*= 0*/)
{
	if(is_z_hopped)
	{
		if(speed == 0)
		{
			const ExtruderTrain& extruder = Application::getInstance().scene.extruders[current_extruder];
			speed = extruder.settings.get<Velocity>("speed_z_hop");
		}

		is_z_hopped = 0;
		currentPosition.z = current_layer_z;
		currentSpeed = speed;
		*output_stream << "G1 F" << PrecisionedDouble{1, speed * 60} << " Z" << MMtoStream{current_layer_z} << new_line;
		assert(speed > 0.0 && "Z hop speed should be positive.");
	}
}

void GCodeExport::startExtruder(const size_t new_extruder)
{
	extruder_attr[new_extruder].is_used = true;

	if(new_extruder != current_extruder) // wouldn't be the case on the very first extruder start if it's extruder 0
		writeExtruderSelect(new_extruder);

	current_extruder = new_extruder;

	assert(getCurrentExtrudedVolume() == 0.0 && "Just after an extruder switch we haven't extruded anything yet!");
	resetExtrusionValue(); // zero the E value on the new extruder, just to be sure

	const std::string start_code = Application::getInstance().scene.extruders[new_extruder].settings.get<std::string>("machine_extruder_start_code");

	if(!start_code.empty())
		writeCode(start_code);

	//Change the Z position so it gets re-written again. We do not know if the switch code modified the Z position.
	currentPosition.z += 1;

	setExtruderFanNumber(new_extruder);
}

void GCodeExport::switchExtruder(size_t new_extruder, const RetractionConfig& retraction_config_old_extruder, coord_t perform_z_hop /*= 0*/)
{
	if(current_extruder == new_extruder)
		return;

	const Settings& old_extruder_settings = Application::getInstance().scene.extruders[current_extruder].settings;

	if(old_extruder_settings.get<bool>("retraction_enable"))
	{
		constexpr bool force = true;
		constexpr bool extruder_switch = true;
		writeRetraction(retraction_config_old_extruder, force, extruder_switch);
	}

	if(perform_z_hop > 0)
		writeZhopStart(perform_z_hop);

	resetExtrusionValue(); // zero the E value on the old extruder, so that the current_e_value is registered on the old extruder

	const std::string end_code = old_extruder_settings.get<std::string>("machine_extruder_end_code");

	if(!end_code.empty())
		writeCode(end_code);

	startExtruder(new_extruder);
}

void GCodeExport::writeExtruderSelect(size_t extruder)
{
	*output_stream << "T" << extruder + extruder_base << new_line;
}
	
void GCodeExport::writeCode(const char* str)
{
	*output_stream << str << new_line;
}

void GCodeExport::writeCode(const std::string& str)
{
	*output_stream << str << new_line;
}

void GCodeExport::writePrimeTrain(const Velocity& travel_speed)
{
	if(extruder_attr[current_extruder].is_primed)
	{
		// extruder is already primed once!
		return;
	}

	const Settings& extruder_settings = Application::getInstance().scene.extruders[current_extruder].settings;

	if(extruder_settings.get<bool>("prime_blob_enable"))
	{
		// only move to prime position if we do a blob/poop
		// ideally the prime position would be respected whether we do a blob or not,
		// but the frontend currently doesn't support a value function of an extruder setting depending on an fdmprinter setting,
		// which is needed to automatically ignore the prime position for the printer when blob is disabled
		
		Point3 prime_pos(extruder_settings.get<coord_t>("extruder_prime_pos_x"), extruder_settings.get<coord_t>("extruder_prime_pos_y"), extruder_settings.get<coord_t>("extruder_prime_pos_z"));
		
		if(!extruder_settings.get<bool>("extruder_prime_pos_abs"))
		{
			// currentPosition.z can be already z hopped
			prime_pos += Point3(currentPosition.x, currentPosition.y, current_layer_z);
		}
		
		writeTravel(prime_pos, travel_speed);
	}

	extruder_attr[current_extruder].is_primed = true;
}

void GCodeExport::setExtruderFanNumber(int extruder)
{
	if(extruder_attr[extruder].fan_number != fan_number)
	{
		fan_number = extruder_attr[extruder].fan_number;
		current_fan_speed = -1; // ensure fan speed gcode gets output for this fan
	}
}

void GCodeExport::writeFanCommand(double speed)
{
	if(std::abs(current_fan_speed - speed) < 0.1)
		return;

	current_fan_speed = speed;
}

void GCodeExport::writeTemperatureCommand(const size_t extruder, const Temperature& temperature, const bool wait)
{
	const ExtruderTrain& extruder_train = Application::getInstance().scene.extruders[extruder];

	if(!extruder_train.settings.get<bool>("machine_nozzle_temp_enabled"))
		return;

	if(extruder_train.settings.get<bool>("machine_extruders_share_heater"))
	{
		// extruders share a single heater
		if(extruder != current_extruder)
		{
			// ignore all changes to the non-current extruder
			return;
		}

		// sync all extruders with the change to the current extruder
		const size_t extruder_count = Application::getInstance().scene.extruders.size();

		for(size_t extruder_nr = 0; extruder_nr < extruder_count; extruder_nr++)
		{
			if(extruder_nr != extruder)
			{
				extruder_attr[extruder_nr].waited_for_temperature = wait;
				extruder_attr[extruder_nr].currentTemperature = temperature;
			}
		}
	}

	if((!wait || extruder_attr[extruder].waited_for_temperature) && extruder_attr[extruder].currentTemperature == temperature)
		return;

	if(wait)
	{
		extruder_attr[extruder].waited_for_temperature = true;
	}
	else
	{
		extruder_attr[extruder].waited_for_temperature = false;
	}

	extruder_attr[extruder].currentTemperature = temperature;
}

void GCodeExport::writeBedTemperatureCommand(const Temperature& temperature, const bool wait)
{

}

void GCodeExport::writeBuildVolumeTemperatureCommand(const Temperature& temperature, const bool wait)
{

}

void GCodeExport::writePrintAcceleration(const Acceleration& acceleration)
{
	current_print_acceleration = acceleration;
	estimateCalculator.setAcceleration(acceleration);
}

void GCodeExport::writeTravelAcceleration(const Acceleration& acceleration)
{
	writePrintAcceleration(acceleration);

	current_travel_acceleration = acceleration;
	estimateCalculator.setAcceleration(acceleration);
}

void GCodeExport::writeJerk(const Velocity& jerk)
{
	if(current_jerk != jerk)
	{
		current_jerk = jerk;
		estimateCalculator.setMaxXyJerk(jerk);
	}
}

void GCodeExport::finalize(const std::string& endCode)
{
	writeFanCommand(0);
	writeCode(endCode);

	int64_t print_time = getSumTotalPrintTimes();
	int mat_0 = getTotalFilamentUsed(0);

	log("Print time (s): %d\n", print_time);
	log("Print time (hr|min|s): %dh %dm %ds\n", print_time / 60 / 60, (print_time / 60) % 60, print_time % 60);
	log("Filament (mm^3): %d\n", mat_0);

	for(int n = 1; n < MAX_EXTRUDERS; n++)
	{
		if(getTotalFilamentUsed(n) > 0)
			log("Filament%d: %d\n", n + 1, int(getTotalFilamentUsed(n)));
	}
	
	output_stream->flush();
}

double GCodeExport::getExtrudedVolumeAfterLastWipe(size_t extruder)
{
	return eToMm3(extruder_attr[extruder].last_e_value_after_wipe, extruder);
}

void GCodeExport::ResetLastEValueAfterWipe(size_t extruder)
{
	extruder_attr[extruder].last_e_value_after_wipe = 0;
}

void GCodeExport::insertWipeScript(const WipeScriptConfig& wipe_config)
{
	Point3 prev_position = currentPosition;
	
	writeComment("WIPE_SCRIPT_BEGIN");

	if(wipe_config.retraction_enable)
		writeRetraction(wipe_config.retraction_config);

	if(wipe_config.hop_enable)
		writeZhopStart(wipe_config.hop_amount, wipe_config.hop_speed);

	writeTravel(Point(wipe_config.brush_pos_x, currentPosition.y), wipe_config.move_speed);

	for(size_t i = 0; i < wipe_config.repeat_count; ++i)
	{
		coord_t x = currentPosition.x + (i % 2 ? -wipe_config.move_distance : wipe_config.move_distance);
		writeTravel(Point(x, currentPosition.y), wipe_config.move_speed);
	}

	writeTravel(prev_position, wipe_config.move_speed);

	if(wipe_config.hop_enable)
		writeZhopEnd(wipe_config.hop_speed);

	if(wipe_config.retraction_enable)
		writeUnretractionAndPrime();

	if(wipe_config.pause > 0)
		writeDelay(wipe_config.pause);

	writeComment("WIPE_SCRIPT_END");
}

} //namespace cura
