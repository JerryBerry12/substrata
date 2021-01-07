/*=====================================================================
DownloadResourcesThread.cpp
-------------------
Copyright Glare Technologies Limited 2016 -
Generated at 2016-01-16 22:59:23 +1300
=====================================================================*/
#include "DownloadResourcesThread.h"


#include "../shared/Protocol.h"
#include "MySocket.h"
#include <ConPrint.h>
#include <vec3.h>
#include <Exception.h>
#include <StringUtils.h>
#include <MemMappedFile.h>
#include <FileUtils.h>
#include <KillThreadMessage.h>
#include <PlatformUtils.h>


DownloadResourcesThread::DownloadResourcesThread(ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue_, Reference<ResourceManager> resource_manager_, const std::string& hostname_, int port_, 
	IndigoAtomic* num_resources_downloading_)
:	out_msg_queue(out_msg_queue_),
	hostname(hostname_),
	//resources_dir(resources_dir_),
	resource_manager(resource_manager_),
	port(port_),
	num_resources_downloading(num_resources_downloading_)
{}


DownloadResourcesThread::~DownloadResourcesThread()
{}


// Returns true if kill message received
static bool checkMessageQueue(ThreadSafeQueue<Reference<ThreadMessage> >& queue, std::set<std::string>& URLs_to_get)
{
	// Get any more messages from the queue while we're woken up.
	Lock lock(queue.getMutex());
	while(!queue.unlockedEmpty())
	{
		ThreadMessageRef msg;
		queue.unlockedDequeue(msg);

		if(dynamic_cast<DownloadResourceMessage*>(msg.getPointer()))
		{
			URLs_to_get.insert(msg.downcastToPtr<DownloadResourceMessage>()->URL);
		}
		else if(dynamic_cast<KillThreadMessage*>(msg.getPointer()))
		{
			return true;
		}
	}
	return false;
}


// Make sure num_resources_downloading gets decremented even in the presence of exceptions.
struct NumNonNetResourcesDownloadingDecrementor
{
	~NumNonNetResourcesDownloadingDecrementor() { (*num_resources_downloading)--; }
	IndigoAtomic* num_resources_downloading;
};


void DownloadResourcesThread::doRun()
{
	PlatformUtils::setCurrentThreadNameIfTestsEnabled("DownloadResourcesThread");

	try
	{
		conPrint("DownloadResourcesThread: Connecting to " + hostname + ":" + toString(port) + "...");

		MySocketRef socket = new MySocket(hostname, port);
		socket->setUseNetworkByteOrder(false);

		conPrint("DownloadResourcesThread: Connected to " + hostname + ":" + toString(port) + "!");

		socket->writeUInt32(Protocol::CyberspaceHello); // Write hello
		socket->writeUInt32(Protocol::CyberspaceProtocolVersion); // Write protocol version
		socket->writeUInt32(Protocol::ConnectionTypeDownloadResources); // Write connection type

		// Read hello response from server
		const uint32 hello_response = socket->readUInt32();
		if(hello_response != Protocol::CyberspaceHello)
			throw Indigo::Exception("Invalid hello from server: " + toString(hello_response));

		const int MAX_STRING_LEN = 10000;

		// Read protocol version response from server
		const uint32 protocol_response = socket->readUInt32();
		if(protocol_response == Protocol::ClientProtocolTooOld)
		{
			const std::string msg = socket->readStringLengthFirst(MAX_STRING_LEN);
			throw Indigo::Exception(msg);
		}
		else if(protocol_response == Protocol::ClientProtocolOK)
		{}
		else
			throw Indigo::Exception("Invalid protocol version response from server: " + toString(protocol_response));

		std::set<std::string> URLs_to_get; // Set of URLs that this thread will get from the server.

		while(1)
		{
			if(URLs_to_get.empty())
			{
				// Wait on the message queue until we have something to download, or we get a kill-thread message.
				ThreadMessageRef msg;
				getMessageQueue().dequeue(msg);

				if(dynamic_cast<DownloadResourceMessage*>(msg.getPointer()))
					URLs_to_get.insert(msg.downcastToPtr<DownloadResourceMessage>()->URL);
				else if(dynamic_cast<KillThreadMessage*>(msg.getPointer()))
					return;

				// Get any more messages from the queue while we're woken up.
				if(checkMessageQueue(getMessageQueue(), URLs_to_get))
					return;
			}
			else
			{
				for(auto it = URLs_to_get.begin(); it != URLs_to_get.end(); )
				{
					ResourceRef resource = resource_manager->getResourceForURL(*it);
					if(resource->getState() != Resource::State_NotPresent)
					{
						//conPrint("Already have file or downloading file '" + *it + "', not downloading.");
						auto to_remove = it++;
						URLs_to_get.erase(to_remove);
					}
					else
					{
						resource->setState(Resource::State_Transferring);
						++it;
					}
				}

				*this->num_resources_downloading = URLs_to_get.size();

				if(!URLs_to_get.empty())
				{
					socket->writeUInt32(Protocol::GetFiles);
					socket->writeUInt64(URLs_to_get.size()); // Write number of files to get

					for(auto it = URLs_to_get.begin(); it != URLs_to_get.end(); ++it)
					{
						const std::string URL = *it;

						// conPrint("DownloadResourcesThread: Querying server for file '" + URL + "'...");
						socket->writeStringLengthFirst(URL);
					}

					// Read reply, which has an error code for each resource download.
					for(auto it = URLs_to_get.begin(); it != URLs_to_get.end(); ++it)
					{
						NumNonNetResourcesDownloadingDecrementor d;
						d.num_resources_downloading = this->num_resources_downloading;

						const std::string URL = *it;
						ResourceRef resource = resource_manager->getResourceForURL(URL);

						const uint32 result = socket->readUInt32();
						if(result == 0) // If OK:
						{
							// Download resource
							const uint64 file_len = socket->readUInt64();
							if(file_len > 0)
							{
								// TODO: cap length in a better way
								if(file_len > 1000000000)
									throw Indigo::Exception("downloaded file too large (len=" + toString(file_len) + ").");

								std::vector<uint8> buf(file_len);
								socket->readData(buf.data(), file_len);

								// Save to disk
								const std::string path = resource_manager->pathForURL(URL);
								try
								{
									FileUtils::writeEntireFile(path, (const char*)buf.data(), buf.size());

									conPrint("DownloadResourcesThread: Wrote downloaded file to '" + path + "'. (len=" + toString(file_len) + ") ");

									resource->setState(Resource::State_Present);

									out_msg_queue->enqueue(new ResourceDownloadedMessage(URL));
								}
								catch(FileUtils::FileUtilsExcep& e)
								{
									resource->setState(Resource::State_NotPresent);
									conPrint("DownloadResourcesThread: Error while writing file to disk: " + e.what());
								}
							}

							conPrint("DownloadResourcesThread: Got file.");
						}
						else
						{
							resource->setState(Resource::State_NotPresent);
							conPrint("DownloadResourcesThread: Server couldn't send file. (Result=" + toString(result) + ")");
						}
					}
				}

				URLs_to_get.clear();
				*this->num_resources_downloading = URLs_to_get.size();
			}
		}
	}
	catch(MySocketExcep& e)
	{
		conPrint("DownloadResourcesThread Socket error: " + e.what());
	}
	catch(Indigo::Exception& e)
	{
		conPrint("DownloadResourcesThread Indigo::Exception: " + e.what());
	}
}
