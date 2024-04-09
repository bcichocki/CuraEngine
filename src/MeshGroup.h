//Copyright (C) 2018 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#ifndef MESH_GROUP_H
#define MESH_GROUP_H

#include "mesh.h"
#include "utils/NoCopy.h"

namespace cura
{

class FMatrix3x3;

/*!
 * A MeshGroup is a collection with 1 or more 3D meshes.
 * 
 * One MeshGroup is a whole which is printed at once.
 * Generally there is one single MeshGroup, though when using one-at-a-time printing, multiple MeshGroups are processed consecutively.
 */
class MeshGroup : public NoCopy
{
public:
    std::vector<Mesh> meshes;
    Settings settings;
    Tina3D::LayerGroupSettings layer_group0;

    Point3 min() const; //! minimal corner of bounding box
    Point3 max() const; //! maximal corner of bounding box

    void clear();

    void finalize();
};

} //namespace cura

#endif //MESH_GROUP_H
