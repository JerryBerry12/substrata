/*=====================================================================
LoadTextureTask.cpp
-------------------
Copyright Glare Technologies Limited 2019 -
=====================================================================*/
#include "LoadTextureTask.h"


#include "ThreadMessages.h"
#include "../shared/ImageDecoding.h"
#include <indigo/TextureServer.h>
#include <graphics/ImageMapSequence.h>
#include <graphics/GifDecoder.h>
#include <graphics/imformatdecoder.h> // For ImFormatExcep
#include <graphics/TextureProcessing.h>
#include <opengl/OpenGLEngine.h>
#include <ConPrint.h>
#include <PlatformUtils.h>
#include <IncludeHalf.h>


LoadTextureTask::LoadTextureTask(const Reference<OpenGLEngine>& opengl_engine_, TextureServer* texture_server_, ThreadSafeQueue<Reference<ThreadMessage> >* result_msg_queue_, const std::string& path_, bool use_sRGB_)
:	opengl_engine(opengl_engine_), texture_server(texture_server_), result_msg_queue(result_msg_queue_), path(path_), use_sRGB(use_sRGB_)
{}


void LoadTextureTask::run(size_t thread_index)
{
	try
	{
		// conPrint("LoadTextureTask: processing texture '" + path + "'");

		const std::string key = texture_server->keyForPath(path); // Get canonical path.  May throw TextureServerExcep

		if(texture_server->isTextureLoadedForRawName(key)) // If this texture is already loaded, return.
			return;

		// Load texture from disk and decode it.
		Reference<Map2D> map;
		if(hasExtension(key, "gif"))
			map = GIFDecoder::decodeImageSequence(key);
		else
			map = ImageDecoding::decodeImage(".", key);

		const bool allow_compression = opengl_engine->textureCompressionSupportedAndEnabled();
		Reference<TextureData> texture_data = TextureProcessing::buildTextureData(map.ptr(), opengl_engine->mem_allocator.ptr(), &opengl_engine->getTaskManager(), allow_compression);

		if(hasExtension(key, "gif") && texture_data->compressedSizeBytes() > 100000000)
		{
			conPrint("Large gif texture data: " + toString(texture_data->compressedSizeBytes()) + " B, " + key);
		}


		// Send a message to MainWindow with the loaded texture data.
		Reference<TextureLoadedThreadMessage> msg = new TextureLoadedThreadMessage();
		msg->tex_path = path;
		msg->tex_key = key;
		msg->use_sRGB = use_sRGB;
		msg->texture_data = texture_data;
		result_msg_queue->enqueue(msg);
	}
	catch(TextureServerExcep& e)
	{
		conPrint("Warning: failed to get canonical key for path '" + path + "': " + e.what());
	}
	catch(ImFormatExcep& )
	{
		//conPrint("Warning: failed to decode texture '" + path + "': " + e.what());
	}
	catch(glare::Exception& e)
	{
		result_msg_queue->enqueue(new LogMessage("Failed to load texture '" + path + "': " + e.what()));
	}
}
