//Copyright (c) 2018 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#include "layerPart.h"
#include "sliceDataStorage.h"
#include "slicer.h"
#include "settings/Settings.h"
#include "progress/Progress.h"

#include <engine/config.h>

#if (USE_TINA3D_ENGINE == 1)
#include <engine/core/tina3d.h>
#endif
/*
The layer-part creation step is the first step in creating actual useful data for 3D printing.
It takes the result of the Slice step, which is an unordered list of polygons, and makes groups of polygons,
each of these groups is called a "part", which sometimes are also known as "islands". These parts represent
isolated areas in the 2D layer with possible holes.

Creating "parts" is an important step, as all elements in a single part should be printed before going to another part.
And all every bit inside a single part can be printed without the nozzle leaving the boundary of this part.

It's also the first step that stores the result in the "data storage" so all other steps can access it.
*/

namespace cura {

///////////////////////////////////////////////////////////////////////////////////////////////////
#if (USE_TINA3D_ENGINE == 1)

static void Polygons_splitIntoParts_processPolyTreeNode(ClipperLib::PolyNode* node, std::vector<PolygonsPart>& ret)
{
	for(int n = 0; n < node->ChildCount(); n++)
	{
		ClipperLib::PolyNode* child = node->Childs[n];
		
		PolygonsPart part;
		part.add(child->Contour);
		
		for(int i = 0; i < child->ChildCount(); i++)
		{
			part.add(child->Childs[i]->Contour);
			
			Polygons_splitIntoParts_processPolyTreeNode(child->Childs[i], ret);
		}
		
		ret.push_back(part);
	}
}

static std::vector<PolygonsPart> Polygons_splitIntoParts(const ClipperLib::Paths& paths, bool unionAll)
{
	std::vector<PolygonsPart> ret;

	ClipperLib::Clipper clipper(clipper_init);
	clipper.AddPaths(paths, ClipperLib::ptSubject, true);

	ClipperLib::PolyTree resultPolyTree;

	if(unionAll)
		clipper.Execute(ClipperLib::ctUnion, resultPolyTree, ClipperLib::pftNonZero, ClipperLib::pftNonZero);
	else
		clipper.Execute(ClipperLib::ctUnion, resultPolyTree);

	Polygons_splitIntoParts_processPolyTreeNode(&resultPolyTree, ret);

	return ret;
}

#endif
///////////////////////////////////////////////////////////////////////////////////////////////////

void createLayerWithParts(const Settings& settings, SliceLayer& storageLayer, SlicerLayer* layer)
{
	const bool union_all_remove_holes = settings.get<bool>("meshfix_union_all_remove_holes");
	const bool union_layers = settings.get<bool>("meshfix_union_all");

	storageLayer.openPolyLines = layer->openPolylines;

	if(union_all_remove_holes)
	{
		for(unsigned int i = 0; i < layer->polygons.size(); i++)
		{
			if(layer->polygons[i].orientation())
				layer->polygons[i].reverse();
		}
	}

	std::vector<PolygonsPart> result;

	result = layer->polygons.splitIntoParts(union_layers || union_all_remove_holes);

	for(unsigned int i = 0; i < result.size(); i++)
	{
		storageLayer.parts.emplace_back();
		storageLayer.parts[i].outline = result[i];
		storageLayer.parts[i].boundaryBox.calculate(storageLayer.parts[i].outline);
	}
}

void createLayerParts(SliceMeshStorage& mesh, Slicer* slicer)
{
    const auto total_layers = slicer->layers.size();
    assert(mesh.layers.size() == total_layers);

#if (USE_TINA3D_ENGINE != 1)

#ifndef _DEBUG
    // OpenMP compatibility fix for GCC <= 8 and GCC >= 9
    // See https://www.gnu.org/software/gcc/gcc-9/porting_to.html, section "OpenMP data sharing"
#if defined(__GNUC__) && __GNUC__ <= 8
    #pragma omp parallel for default(none) shared(mesh, slicer) schedule(dynamic)
#else
    #pragma omp parallel for default(none) shared(mesh, slicer, total_layers) schedule(dynamic)
#endif // defined(__GNUC__) && __GNUC__ <= 8
    // Use a signed type for the loop counter so MSVC compiles (because it uses OpenMP 2.0, an old version).
#endif
	for(int layer_nr = 0; layer_nr < static_cast<int>(total_layers); layer_nr++)
	{
		SliceLayer& layer_storage = mesh.layers[layer_nr];
		SlicerLayer& slice_layer = slicer->layers[layer_nr];
		createLayerWithParts(mesh.settings, layer_storage, &slice_layer);
	}

	for(LayerIndex layer_nr = total_layers - 1; layer_nr >= 0; layer_nr--)
	{
		SliceLayer& layer_storage = mesh.layers[layer_nr];
		if(layer_storage.parts.size() > 0)
		{
			mesh.layer_nr_max_filled_layer = layer_nr; // last set by the highest non-empty layer
			break;
		}
	}
#else
	const Settings& settings = mesh.settings;

	size_t mesh_idx = mesh.layer_group.index;

	for(int layer_nr = 0; layer_nr < static_cast<int>(total_layers); layer_nr++)
	{
		SliceLayer& storageLayer = mesh.layers[layer_nr];
		SlicerLayer& slice_layer = slicer->layers[layer_nr];
		
		if(layer_nr < Engine::layers.size())
		{
			auto& layer = Engine::layers[layer_nr];

			for(const auto& group : layer.groups)
			{
				///////////////////////////////////////////////////////////////////////////////////

				size_t group_id = group.id < 2 ? 0 : group.id - 1;

				if(mesh_idx)
				{
					if(group_id != mesh_idx - 1)
						continue;
				}
				else
				{
					if(group_id > 0)
						continue;
				}

				///////////////////////////////////////////////////////////////////////////////////

				const ClipperLib::Paths& paths = group.contours;

				const bool union_all_remove_holes = settings.get<bool>("meshfix_union_all_remove_holes");
				const bool union_layers = settings.get<bool>("meshfix_union_all");

				storageLayer.openPolyLines = slice_layer.openPolylines;

				std::vector<PolygonsPart> result;
				result = Polygons_splitIntoParts(paths, union_layers || union_all_remove_holes);

				for(auto&& r : result)
				{
					storageLayer.parts.emplace_back();
					auto& part = storageLayer.parts.back();
					part.outline = r;
					part.boundaryBox.calculate(part.outline);
					part.group_id = group.id;
				}
			}
		}
	}

	for(LayerIndex layer_nr = total_layers - 1; layer_nr >= 0; layer_nr--)
	{
		SliceLayer& layer_storage = mesh.layers[layer_nr];
		
		if(layer_storage.parts.size() > 0)
		{
			mesh.layer_nr_max_filled_layer = layer_nr; // last set by the highest non-empty layer
			break;
		}
	}
#endif

}

}//namespace cura
