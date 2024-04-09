//Copyright (c) 2018 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#ifndef APPLICATION_H
#define APPLICATION_H

#include "utils/NoCopy.h"
#include "Scene.h"

namespace cura
{
/*!
 * A singleton class that serves as the starting point for all slicing.
 *
 * The application provides a starting point for the slicing engine. It
 * maintains communication with other applications and uses that to schedule
 * slices.
 */
class Application : NoCopy
{
public:

	Application(const size_t num_mesh_groups);
	~Application();

	Scene scene;

    static Application& getInstance();
};

} //Cura namespace.

#endif //APPLICATION_H