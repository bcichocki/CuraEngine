//Copyright (c) 2022 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#ifdef VTK_OUTPUT

#include "VtkCommunication.h" //The class we're implementing.

namespace cura
{

//These are not applicable to VTK output.
void VtkCommunication::beginGCode() { }
void VtkCommunication::flushGCode() { }
bool VtkCommunication::isSequential() const { return false; }
bool VtkCommunication::hasSlice() const { return false; } //Never provides a slice.
void VtkCommunication::sendCurrentPosition(const Point&) { }
void VtkCommunication::sendFinishedSlicing() const { }
void VtkCommunication::sendGCodePrefix(const std::string&) const { }
void VtkCommunication::sendLayerComplete(const LayerIndex&, const coord_t&, const coord_t&) { }
void VtkCommunication::sendOptimizedLayerData() { }
void VtkCommunication::sendPrintTimeMaterialEstimates() const { }
void VtkCommunication::sendProgress(const float&) const { }
void VtkCommunication::sliceNext() { } //Never provides a slice.

void VtkCommunication::setExtruderForSend(const ExtruderTrain& extruder)
{
    //TODO: We might want to store which extruder is being processed, if we want to use the extruder information in sendLineTo, sendPolygon or sendPolygons.
}

void VtkCommunication::setLayerForSend(const LayerIndex& layer_nr)
{
    //TODO: We might want to store which layer is being processed, if we want to use the layer index in sendLineTo, sendPolygon or sendPolygons.
}

void sendLineTo(const PrintFeatureType& feature_type, const Point& to, const coord_t& line_width, const coord_t& line_thickness, const Velocity& velocity)
{
    //TODO: Output printed lines to VTK file?
}

void sendPolygon(const PrintFeatureType& feature_type, const ConstPolygonRef& polygon, const coord_t& line_width, const coord_t& line_thickness, const Velocity& velocity)
{
    //TODO: Output printed polygons to VTK file?
}

void sendPolygons(const PrintFeatureType& feature_type, const Polygons& polygons, const coord_t& line_width, const coord_t& line_thickness, const Velocity& velocity)
{
    //TODO: Output printed polygons's to VTK file?
}

}

#endif //VTK_OUTPUT