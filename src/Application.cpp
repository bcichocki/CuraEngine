//Copyright (c) 2018 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#include "Application.h"

namespace cura
{

static Application* instance_ptr; //Constructs using the default constructor.

Application::Application(const size_t num_mesh_groups) : scene(num_mesh_groups)
{
	instance_ptr = this;
}

Application::~Application()
{
	instance_ptr = nullptr;
}

Application& Application::getInstance()
{
	return *instance_ptr;
}

} //Cura namespace.
