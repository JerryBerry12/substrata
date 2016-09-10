/*=====================================================================
Server.cpp
---------------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#include "Server.h"


#include "ListenerThread.h"
#include <ThreadManager.h>
#include <PlatformUtils.h>
#include "../networking/networking.h"
#include <Clock.h>
#include <Timer.h>
#include <FileUtils.h>
#include <ConPrint.h>
#include <Exception.h>
#include <Parser.h>
#include <Base64.h>
#include <ImmutableVector.h>
#include <ArgumentParser.h>
#include <SocketBufferOutStream.h>
#include "WorkerThread.h"


int main(int argc, char *argv[])
{
	Clock::init();
	Networking::createInstance();
	PlatformUtils::ignoreUnixSignals();

	try
	{
		//---------------------- Parse and process comment line arguments -------------------------
		std::map<std::string, std::vector<ArgumentParser::ArgumentType> > syntax;
		syntax["--src_resource_dir"] = std::vector<ArgumentParser::ArgumentType>(1, ArgumentParser::ArgumentType_string); // One string arg

		std::vector<std::string> args;
		for(int i=0; i<argc; ++i)
			args.push_back(argv[i]);

		ArgumentParser parsed_args(args, syntax);

		// src_resource_dir can be set to something like C:\programming\chat_site\trunk to read e.g. script.js directly from trunk
		std::string src_resource_dir = "./";
		if(parsed_args.isArgPresent("--src_resource_dir"))
			src_resource_dir = parsed_args.getArgStringValue("--src_resource_dir");

		conPrint("src_resource_dir: '" + src_resource_dir + "'");

		// Run tests if --test is present.
		if(parsed_args.isArgPresent("--test") || parsed_args.getUnnamedArg() == "--test")
		{
#if BUILD_TESTS
			Base64::test();
			Parser::doUnitTests();
			conPrint("----Finished tests----");
#endif
			return 0;
		}
		//-----------------------------------------------------------------------------------------

		const int listen_port = 7654;
		conPrint("listen port: " + toString(listen_port));


		const std::string server_resource_dir = "D:/cyberspace_server_state/server_resources";
		conPrint("server_resource_dir: " + server_resource_dir);

		FileUtils::createDirIfDoesNotExist(server_resource_dir);
		
		Server server;
		server.resource_manager = new ResourceManager(server_resource_dir);

		const std::string server_state_path = "D:/cyberspace_server_state/server_state.bin";
		if(FileUtils::fileExists(server_state_path))
			server.world_state->readFromDisk(server_state_path);

		// Add a teapot object
		if(false)
		{
			const UID uid(6000);
			WorldObjectRef world_ob = new WorldObject();
			world_ob->state = WorldObject::State_Alive;
			world_ob->uid = uid;
			world_ob->pos = Vec3d(3, 0, 1);
			world_ob->angle = 0;
			world_ob->axis = Vec3f(1,0,0);
			world_ob->model_url = "teapot.obj";
			server.world_state->objects[uid] = world_ob;
		}

		ThreadManager thread_manager;
		thread_manager.addThread(new ListenerThread(listen_port, &server));
		//thread_manager.addThread(new DataStoreSavingThread(data_store));

		Timer save_state_timer;
		bool world_state_changed = false; // Has changed since server state or since last save.

		// Main server loop
		while(1)
		{
			PlatformUtils::Sleep(100);

			std::vector<std::string> broadcast_packets;

			Lock lock(server.world_state->mutex);

			// Generate packets for avatar changes
			for(auto i = server.world_state->avatars.begin(); i != server.world_state->avatars.end();)
			{
				Avatar* avatar = i->second.getPointer();
				if(avatar->dirty)
				{
					if(avatar->state == Avatar::State_Alive)
					{
						// Send AvatarTransformUpdate packet
						SocketBufferOutStream packet;
						packet.writeUInt32(AvatarTransformUpdate);
						writeToStream(avatar->uid, packet);
						writeToStream(avatar->pos, packet);
						writeToStream(avatar->axis, packet);
						packet.writeFloat(avatar->angle);

						std::string packet_string(packet.buf.size(), '\0');
						std::memcpy(&packet_string[0], packet.buf.data(), packet.buf.size());

						broadcast_packets.push_back(packet_string);

						avatar->dirty = false;
						i++;
					}
					else if(avatar->state == Avatar::State_JustCreated)
					{
						// Send AvatarCreated packet
						SocketBufferOutStream packet;
						packet.writeUInt32(AvatarCreated);
						writeToStream(avatar->uid, packet);
						packet.writeStringLengthFirst(avatar->name);
						packet.writeStringLengthFirst(avatar->model_url);
						writeToStream(avatar->pos, packet);
						writeToStream(avatar->axis, packet);
						packet.writeFloat(avatar->angle);

						std::string packet_string(packet.buf.size(), '\0');
						std::memcpy(&packet_string[0], packet.buf.data(), packet.buf.size());

						broadcast_packets.push_back(packet_string);

						avatar->state = Avatar::State_Alive;
						avatar->dirty = false;

						i++;
					}
					else if(avatar->state == Avatar::State_Dead)
					{
						// Send AvatarDestroyed packet
						SocketBufferOutStream packet;
						packet.writeUInt32(AvatarDestroyed);
						writeToStream(avatar->uid, packet);

						std::string packet_string(packet.buf.size(), '\0');
						std::memcpy(&packet_string[0], packet.buf.data(), packet.buf.size());

						broadcast_packets.push_back(packet_string);

						// Remove avatar from avatar map
						auto old_avatar_iterator = i;
						i++;
						server.world_state->avatars.erase(old_avatar_iterator);

						conPrint("Removed avatar from world_state->avatars");
					}
					else
					{
						assert(0);
					}
				}
				else
				{
					i++;
				}
			}



			// Generate packets for object changes
			for(auto i = server.world_state->objects.begin(); i != server.world_state->objects.end();)
			{
				WorldObject* ob = i->second.getPointer();
				if(ob->from_remote_dirty)
				{
					conPrint("Object dirty, sending update");

					if(ob->state == WorldObject::State_Alive)
					{
						// Send ObjectTransformUpdate packet
						SocketBufferOutStream packet;
						packet.writeUInt32(ObjectTransformUpdate);
						writeToStream(ob->uid, packet);
						writeToStream(ob->pos, packet);
						writeToStream(ob->axis, packet);
						packet.writeFloat(ob->angle);

						std::string packet_string(packet.buf.size(), '\0');
						std::memcpy(&packet_string[0], packet.buf.data(), packet.buf.size());

						broadcast_packets.push_back(packet_string);

						ob->from_remote_dirty = false;
						world_state_changed = true;
						i++;
					}
					else if(ob->state == WorldObject::State_JustCreated)
					{
						// Send ObjectCreated packet
						SocketBufferOutStream packet;
						packet.writeUInt32(ObjectCreated);
						writeToStream(ob->uid, packet);
						//packet.writeStringLengthFirst(ob->name);
						packet.writeStringLengthFirst(ob->model_url);
						writeToStream(ob->pos, packet);
						writeToStream(ob->axis, packet);
						packet.writeFloat(ob->angle);

						std::string packet_string(packet.buf.size(), '\0');
						std::memcpy(&packet_string[0], packet.buf.data(), packet.buf.size());

						broadcast_packets.push_back(packet_string);

						ob->state = WorldObject::State_Alive;
						ob->from_remote_dirty = false;
						world_state_changed = true;
						i++;
					}
					else if(ob->state == WorldObject::State_Dead)
					{
						// Send ObjectDestroyed packet
						SocketBufferOutStream packet;
						packet.writeUInt32(ObjectDestroyed);
						writeToStream(ob->uid, packet);

						std::string packet_string(packet.buf.size(), '\0');
						std::memcpy(&packet_string[0], packet.buf.data(), packet.buf.size());

						broadcast_packets.push_back(packet_string);

						// Remove ob from object map
						auto old_ob_iterator = i;
						i++;
						server.world_state->objects.erase(old_ob_iterator);

						conPrint("Removed object from world_state->objects");
						world_state_changed = true;
					}
					else
					{
						conPrint("ERROR: invalid object state.");
						assert(0);
					}
				}
				else
				{
					i++;
				}
			}

			// Enqueue packets to worker threads to send
			{
				Lock lock(server.worker_thread_manager.getMutex());
				for(auto i = server.worker_thread_manager.getThreads().begin(); i != server.worker_thread_manager.getThreads().end(); ++i)
				{
					for(size_t z=0; z<broadcast_packets.size(); ++z)
					{
						assert(dynamic_cast<WorkerThread*>(i->getPointer()));
						static_cast<WorkerThread*>(i->getPointer())->enqueueDataToSend(broadcast_packets[z]);
					}
				}
			}


			if(world_state_changed && (save_state_timer.elapsed() > 5.0))
			{
				try
				{
					// Save world state to disk
					Lock lock(server.world_state->mutex);

					server.world_state->serialiseToDisk(server_state_path);

					world_state_changed = false;
					save_state_timer.reset();
				}
				catch(Indigo::Exception& e)
				{
					conPrint("Warning: saving world state to disk failed: " + e.what());
					save_state_timer.reset(); // Reset timer so we don't try again straight away.
				}
			}

		} // End of main server loop
	}
	catch(ArgumentParserExcep& e)
	{
		conPrint("ArgumentParserExcep: " + e.what());
		return 1;
	}
	catch(Indigo::Exception& e)
	{
		conPrint("Indigo::Exception: " + e.what());
		return 1;
	}

	Networking::destroyInstance();
	return 0;
}


Server::Server()
{
	world_state = new ServerWorldState();
}
