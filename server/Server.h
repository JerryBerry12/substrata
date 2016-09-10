/*=====================================================================
Server.h
----------------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#pragma once


#include "ServerWorldState.h"
#include "ThreadManager.h"
#include "../shared/ResourceManager.h"


/*=====================================================================
Server
--------------------
=====================================================================*/
class Server
{
public:
	Server();

	Reference<ServerWorldState> world_state;

	// Connected client worker threads
	ThreadManager worker_thread_manager;

	Reference<ResourceManager> resource_manager;
};
