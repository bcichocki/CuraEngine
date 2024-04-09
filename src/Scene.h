//Copyright (c) 2018 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#ifndef SCENE_H
#define SCENE_H

#include "ExtruderTrain.h" //To store the extruders in the scene.
#include "MeshGroup.h" //To store the mesh groups in the scene.
#include "settings/Settings.h" //To store the global settings.

namespace cura
{

/*
 * Represents a scene that should be sliced.
 */
class Scene
{
public:
    /*
     * \brief The global settings in the scene.
     */
    Settings settings;

    std::vector<Settings> groups_settings;
	
    /*
     * \brief Which extruder to evaluate each setting on, if different from the
     * normal extruder of the object it's evaluated for.
     */
    //std::unordered_map<std::string, ExtruderTrain*> limit_to_extruder;

    /*
     * \brief The mesh groups in the scene.
     */
    std::vector<MeshGroup> mesh_groups;

    /*
     * \brief The extruders in the scene.
     */
    std::vector<ExtruderTrain> extruders;

    /*
     * \brief The mesh group that is being processed right now.
     *
     * During initialisation this may be nullptr. For the most part, during the
     * slicing process, you can be assured that this will not be null so you can
     * safely dereference it.
     */
    std::vector<MeshGroup>::iterator current_mesh_group;

    /*
     * \brief Create an empty scene.
     *
     * This scene will have no models in it, no extruders, no settings, no
     * nothing.
     * \param num_mesh_groups The number of mesh groups to allocate for.
     */
    //Scene(const size_t num_mesh_groups);

	Scene(const size_t num_mesh_groups) : mesh_groups(num_mesh_groups) , current_mesh_group(mesh_groups.begin())
	{
		for(auto& mesh_group : mesh_groups)
			mesh_group.settings.setParent(&settings);
	}

private:
    /*
     * \brief You are not allowed to copy the scene.
     */
    Scene(const Scene&) = delete;

    /*
     * \brief You are not allowed to copy by assignment either.
     */
    Scene& operator =(const Scene&) = delete;
};

} //namespace cura

#endif //SCENE_H