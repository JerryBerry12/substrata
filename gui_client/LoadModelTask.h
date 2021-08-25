/*=====================================================================
LoadModelTask.h
---------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include "../shared/WorldObject.h"
#include "../shared/Avatar.h"
#include "PhysicsObject.h"
#include <opengl/OpenGLEngine.h>
#include <Task.h>
#include <ThreadMessage.h>
#include <string>
class MainWindow;
class OpenGLEngine;
class MeshManager;
class ResourceManager;


class ModelLoadedThreadMessage : public ThreadMessage
{
public:
	// Results of the task:
	GLObjectRef opengl_ob;
	PhysicsObjectRef physics_ob;
	
	std::string base_model_url;
	int model_lod_level;
	std::string lod_model_url; // May differ from ob->model_url for LOD levels != 0.
	bool loaded_voxels;

	WorldObjectRef ob; // Used for when loading voxels.
};


/*=====================================================================
LoadModelTask
-------------
For the WorldObject ob, 
Builds the OpenGL mesh and Physics mesh for it.

Once it's done, sends a ModelLoadedThreadMessage back to the main window
via main_window->msg_queue.

Note for making the OpenGL Mesh, data isn't actually loaded into OpenGL in this task,
since that needs to be done on the main thread.
=====================================================================*/
class LoadModelTask : public glare::Task
{
public:
	LoadModelTask();
	virtual ~LoadModelTask();

	virtual void run(size_t thread_index);

	std::string base_model_url;
	int model_lod_level;
	std::string lod_model_url;
	WorldObjectRef ob; // Either ob or avatar will be non-null
	AvatarRef avatar;
	Reference<OpenGLEngine> opengl_engine;
	MainWindow* main_window;
	MeshManager* mesh_manager;
	Reference<ResourceManager> resource_manager;
	glare::TaskManager* model_building_task_manager;
};
