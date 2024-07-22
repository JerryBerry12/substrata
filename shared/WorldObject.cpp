/*=====================================================================
WorldObject.cpp
---------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#include "WorldObject.h"


#include <Exception.h>
#include <StringUtils.h>
#include <FileUtils.h>
#include <ConPrint.h>
#include <FileChecksum.h>
#include <Sort.h>
#include <BufferInStream.h>
#include <PoolAllocator.h>
#include <RandomAccessOutStream.h>
#if GUI_CLIENT
#include "opengl/OpenGLEngine.h"
#include "opengl/OpenGLMeshRenderData.h"
#include "../audio/AudioEngine.h"
//#include <SceneNodeModel.h>
#include "../gui_client/WinterShaderEvaluator.h"
#include "../gui_client/WebViewData.h"
#include "../gui_client/BrowserVidPlayer.h"
#include "../gui_client/AnimatedTextureManager.h"
#include "../gui_client/MeshManager.h"
#include <graphics/ImageMap.h>
#include "../gui_client/PhysicsObject.h"
#include "../gui_client/Scripting.h"
#include <opengl/ui/GLUITextView.h>
#endif // GUI_CLIENT
#include "../shared/ResourceManager.h"
#include "../shared/LuaScriptEvaluator.h"
#include "../shared/ObjectEventHandlers.h"
#include <zstd.h>


InstanceInfo::~InstanceInfo()
{
#if GUI_CLIENT
	assert(physics_object.isNull());
#endif
}


WorldObject::WorldObject() noexcept
{
	static_assert(offsetof(WorldObject, centroid_ws) == 0);
	static_assert(offsetof(WorldObject, aabb_ws_longest_len) == 16);
	static_assert(offsetof(WorldObject, aabb_os) == 32);

	creator_id = UserID::invalidUserID();
	flags = COLLIDABLE_FLAG;
	
	object_type = ObjectType_Generic;
	from_remote_transform_dirty = false;
	from_remote_physics_transform_dirty = false;
	from_remote_summoned_dirty = false;
	from_remote_other_dirty = false;
	from_remote_lightmap_url_dirty = false;
	from_remote_model_url_dirty = false;
	from_remote_content_dirty = false;
	from_remote_flags_dirty = false;
	from_remote_physics_ownership_dirty = false;
	from_local_transform_dirty = false;
	from_local_other_dirty = false;
	from_local_physics_dirty = false;
	changed_flags = 0;
	using_placeholder_model = false;

#if GUI_CLIENT
	is_selected = false;
	in_proximity = false;
	in_script_proximity = false;
	lightmap_baking = false;
	current_lod_level = 0;
	loading_or_loaded_model_lod_level = -10;
	loading_or_loaded_lod_level = -10;
	is_path_controlled = false;
	use_materialise_effect_on_load = false;
	materialise_effect_start_time = -1000.f;

	waypoint_index = 0;
	dist_along_segment = 0;
	last_touch_event_time = -1000.f;
#endif
	next_snapshot_i = 0;
	next_insertable_snapshot_i = 0;
	snapshots_are_physics_snapshots = false;
	//last_snapshot_time = 0;
	aabb_ws_longest_len = -1;
	biased_aabb_len = -1;

	translation = Vec4f(0.f);

	max_load_dist2 = 1000000000;

	aabb_os = js::AABBox::emptyAABBox();

	max_model_lod_level = 0;

	mass = 50.f;
	friction = 0.5f;
	restitution = 0.2f;
	centre_of_mass_offset_os = Vec3f(0.f);

	audio_volume = 1;

	allocator = NULL;
	refcount = 0;

	physics_owner_id = std::numeric_limits<uint32>::max();
	last_physics_ownership_change_global_time = 0;

	transmission_time_offset = 0;

}


WorldObject::~WorldObject()
{
#if GUI_CLIENT
	assert(physics_object.isNull());
#endif
}


std::string WorldObject::getLODModelURLForLevel(const std::string& base_model_url, int level)
{
	if(level <= 0)
		return base_model_url;
	else
	{
		if(hasPrefix(base_model_url, "http:") || hasPrefix(base_model_url, "https:"))
			return base_model_url;

		if(level == 1)
			return removeDotAndExtension(base_model_url) + "_lod1.bmesh"; // LOD models are always saved in BatchedMesh (bmesh) format.
		else
			return removeDotAndExtension(base_model_url) + "_lod2.bmesh";
	}
}


int WorldObject::getLODLevelForURL(const std::string& URL) // Identifies _lod1 etc. suffix.
{
	const std::string base = removeDotAndExtension(URL);

	if(hasSuffix(base, "_lod1"))
		return 1;
	else if(hasSuffix(base, "_lod2"))
		return 2;
	else
		return 0;
}


int WorldObject::getModelLODLevel(const Vec3d& campos) const // getLODLevel() clamped to max_model_lod_level
{
	if(max_model_lod_level == 0)
		return 0;

	return myMax(0, getLODLevel(campos));
}


int WorldObject::getModelLODLevelForObLODLevel(int ob_lod_level) const
{
	return myClamp<int>(ob_lod_level, 0, this->max_model_lod_level);
}


std::string WorldObject::getLODModelURL(const Vec3d& campos) const
{
	// Early-out for max_model_lod_level == 0: avoid computing LOD
	if(this->max_model_lod_level == 0)
		return this->model_url;

	const int ob_lod_level = getLODLevel(campos);
	const int ob_model_lod_level = myClamp(ob_lod_level, 0, this->max_model_lod_level);
	return getLODModelURLForLevel(this->model_url, ob_model_lod_level);
}


std::string WorldObject::getLODLightmapURL(const std::string& base_lightmap_url, int level)
{
	assert(level >= -1 && level <= 2);
	if(level <= 0)
		return base_lightmap_url;
	else if(level == 1)
		return removeDotAndExtension(base_lightmap_url) + "_lod1." + getExtension(base_lightmap_url);
	else
		return removeDotAndExtension(base_lightmap_url) + "_lod2." + getExtension(base_lightmap_url);
}


void WorldObject::appendDependencyURLs(int ob_lod_level, const GetDependencyOptions& options, std::vector<DependencyURL>& URLs_out) const
{
	if(!model_url.empty())
	{
		const int ob_model_lod_level =  myClamp(ob_lod_level, 0, this->max_model_lod_level);
		URLs_out.push_back(DependencyURL(getLODModelURLForLevel(model_url, ob_model_lod_level)));
	}

	if(options.include_lightmaps && !lightmap_url.empty())
	{
		DependencyURL dependency_url(getLODLightmapURL(lightmap_url, ob_lod_level));
		dependency_url.is_lightmap = true;
		URLs_out.push_back(dependency_url);
	}

	for(size_t i=0; i<materials.size(); ++i)
		materials[i]->appendDependencyURLs(ob_lod_level, URLs_out);

	if(!audio_source_url.empty())
		URLs_out.push_back(DependencyURL(audio_source_url));
}


void WorldObject::appendDependencyURLsForAllLODLevels(std::vector<DependencyURL>& URLs_out) const
{
	if(!model_url.empty())
	{
		URLs_out.push_back(DependencyURL(model_url));
		if(max_model_lod_level > 0)
		{
			URLs_out.push_back(DependencyURL(getLODModelURLForLevel(model_url, 1)));
			URLs_out.push_back(DependencyURL(getLODModelURLForLevel(model_url, 2)));
		}
	}

	if(!lightmap_url.empty())
		for(int lvl=0; lvl<=2; ++lvl)
		{
			DependencyURL dependency_url(getLODLightmapURL(lightmap_url, lvl));
			dependency_url.is_lightmap = true;
			URLs_out.push_back(dependency_url);
		}

	for(size_t i=0; i<materials.size(); ++i)
		materials[i]->appendDependencyURLsAllLODLevels(URLs_out);

	if(!audio_source_url.empty())
		URLs_out.push_back(DependencyURL(audio_source_url));
}


void WorldObject::appendDependencyURLsBaseLevel(const GetDependencyOptions& options, std::vector<DependencyURL>& URLs_out) const
{
	if(!model_url.empty())
		URLs_out.push_back(DependencyURL(model_url));

	if(options.include_lightmaps && !lightmap_url.empty())
	{
		DependencyURL dependency_url(lightmap_url);
		dependency_url.is_lightmap = true;
		URLs_out.push_back(dependency_url);
	}

	for(size_t i=0; i<materials.size(); ++i)
		materials[i]->appendDependencyURLsBaseLevel(URLs_out);

	if(!audio_source_url.empty())
		URLs_out.push_back(DependencyURL(audio_source_url));
}


void WorldObject::getDependencyURLSet(int ob_lod_level, const GetDependencyOptions& options, std::set<DependencyURL>& URLS_out) const
{
	std::vector<DependencyURL> URLs;
	this->appendDependencyURLs(ob_lod_level, options, URLs);

	URLS_out = std::set<DependencyURL>(URLs.begin(), URLs.end());
}


void WorldObject::getDependencyURLSetForAllLODLevels(std::set<DependencyURL>& URLS_out) const
{
	std::vector<DependencyURL> URLs;
	this->appendDependencyURLsForAllLODLevels(URLs);

	URLS_out = std::set<DependencyURL>(URLs.begin(), URLs.end());
}


void WorldObject::getDependencyURLSetBaseLevel(const GetDependencyOptions& options, std::set<DependencyURL>& URLS_out) const
{
	std::vector<DependencyURL> URLs;
	this->appendDependencyURLsBaseLevel(options, URLs);

	URLS_out = std::set<DependencyURL>(URLs.begin(), URLs.end());
}


void WorldObject::convertLocalPathsToURLS(ResourceManager& resource_manager)
{
	if(FileUtils::fileExists(this->model_url)) // If the URL is a local path:
		this->model_url = resource_manager.URLForPathAndHash(this->model_url, FileChecksum::fileChecksum(this->model_url));

	for(size_t i=0; i<materials.size(); ++i)
		materials[i]->convertLocalPathsToURLS(resource_manager);

	if(FileUtils::fileExists(this->lightmap_url)) // If the URL is a local path:
		this->lightmap_url = resource_manager.URLForPathAndHash(this->lightmap_url, FileChecksum::fileChecksum(this->lightmap_url));

	if(FileUtils::fileExists(this->audio_source_url)) // If the URL is a local path:
		this->audio_source_url = resource_manager.URLForPathAndHash(this->audio_source_url, FileChecksum::fileChecksum(this->audio_source_url));
}


void WorldObject::setTransformAndHistory(const Vec3d& pos_, const Vec3f& axis_, float angle_)
{
	assert(isFinite(angle_));

	pos = pos_;
	axis = axis_;
	angle = angle_;

	const Quatf rot_quat = Quatf::fromAxisAndAngle(normalise(axis_), angle_);

	for(int i=0; i<HISTORY_BUF_SIZE; ++i)
		snapshots[i] = Snapshot({pos_.toVec4fPoint(), rot_quat, /*linear vel=*/Vec4f(0.f), /*angular_vel=*/Vec4f(0.f), /*client time=*/0.0, /*local time=*/0.0});
}


void WorldObject::setPosAndHistory(const Vec3d& pos_)
{
	pos = pos_;
#if GUI_CLIENT
	//last_pos = pos_;
#endif

	for(int i=0; i<HISTORY_BUF_SIZE; ++i)
		snapshots[i].pos = pos_.toVec4fPoint();
}


void WorldObject::getInterpolatedTransform(double cur_time, Vec3d& pos_out, Quatf& rot_out) const
{
	/*
	Timeline: check marks are snapshots received:

	|---------------|----------------|---------------|----------------|
	                                                                       ^
	                                                                      cur_time
	                                                                  ^
	                                               ^                last snapshot
	                                             cur_time - send_period * delay_factor


	When 'frac' gets > 1:
	|---------------|----------------|---------------|----------------x
	                                                                       ^
	                                                                      cur_time
	                                                 ^
	                                     ^         last snapshot
	                                   cur_time - send_period * delay_factor


	 So should be alright as long as it doesn't exceed 2.
	
	|---------------|----------------|---------------|---------------------------|
	                                                                       ^
	                                                                      cur_time
	                                                 ^
	                                     ^         last snapshot
	                                   cur_time - send_period * delay_factor

	
	*/


	const double send_period = 0.1; // Time between update messages from server
	const double delay = send_period * 2.0; // Objects are rendered using the interpolated state at this past time.

	const double delayed_time = cur_time - delay;
	// Search through history for first snapshot with time > delayed_time
	int begin = 0;
	for(int i=(int)next_snapshot_i-HISTORY_BUF_SIZE; i<(int)next_snapshot_i; ++i)
	{
		const int modi = Maths::intMod(i, HISTORY_BUF_SIZE);
		if(snapshots[modi].local_time > delayed_time)
		{
			begin = Maths::intMod(modi - 1, HISTORY_BUF_SIZE);
			break;
		}
	}

	const int end = Maths::intMod(begin + 1, HISTORY_BUF_SIZE);

	// Snapshot times may be the same if we haven't received updates for this object yet.
	const float t  = myClamp<float>(
		(snapshots[end].local_time == snapshots[begin].local_time) ? 0.f : (float)((delayed_time - snapshots[begin].local_time) / (snapshots[end].local_time - snapshots[begin].local_time)),
		0.f, 1.f); // Interpolation fraction

	pos_out = Vec3d(Maths::uncheckedLerp(snapshots[begin].pos, snapshots[end].pos, t));
	rot_out = Quatf::nlerp(snapshots[begin].rotation, snapshots[end].rotation, t);
}


std::string WorldObject::objectTypeString(ObjectType t)
{
	switch(t)
	{
	case ObjectType_Generic: return "generic";
	case ObjectType_Hypercard: return "hypercard";
	case ObjectType_VoxelGroup: return "voxel group";
	case ObjectType_Spotlight: return "spotlight";
	case ObjectType_WebView: return "web view";
	case ObjectType_Video: return "video";
	case ObjectType_Text: return "text";
	default: return "Unknown";
	}
}


static const uint32 WORLD_OBJECT_SERIALISATION_VERSION = 20;
/*
Version history:
9: introduced voxels
10: changed script_url to script
11: Added flags
12: Added compressed voxel field.
13: Added lightmap URL
14: Added aabb_ws
15: Added max_lod_level
16: Added audio_source_url, audio_volume
17: Added mass, friction, restitution
18: Storing aabb_os instead of aabb_ws.
19: Added last_modified_time
20: Added centre_of_mass_offset_os
*/


static_assert(sizeof(Voxel) == sizeof(int)*4, "sizeof(Voxel) == sizeof(int)*4");


template <class T>
struct ScopedRefIncrementor
{
	ScopedRefIncrementor(T& ob_) : ob(ob_)
	{
		ob.incRefCount();
	}
	~ScopedRefIncrementor()
	{
		ob.decRefCount();
	}
	T& ob;
};



void WorldObject::writeToStream(RandomAccessOutStream& stream) const
{
	// Write version
	stream.writeUInt32(WORLD_OBJECT_SERIALISATION_VERSION);

	::writeToStream(uid, stream);
	stream.writeUInt32((uint32)object_type);
	stream.writeStringLengthFirst(model_url);

	// Write materials
	stream.writeUInt32((uint32)materials.size());
	for(size_t i=0; i<materials.size(); ++i)
		::writeWorldMaterialToStream(*materials[i], stream);

	stream.writeStringLengthFirst(lightmap_url); // new in v13

	stream.writeStringLengthFirst(script);
	stream.writeStringLengthFirst(content);
	stream.writeStringLengthFirst(target_url);
	stream.writeStringLengthFirst(audio_source_url);
	stream.writeFloat(audio_volume);

	::writeToStream(pos, stream);
	::writeToStream(axis, stream);
	stream.writeFloat(angle);
	::writeToStream(scale, stream);

	created_time.writeToStream(stream); // new in v5
	last_modified_time.writeToStream(stream); // new in v19
	::writeToStream(creator_id, stream); // new in v5

	stream.writeUInt32(flags); // new in v11

	stream.writeData(aabb_os.min_.x, sizeof(float) * 3); // new in v14.  v18: write aabb_os instead of aabb_ws.
	stream.writeData(aabb_os.max_.x, sizeof(float) * 3);

	stream.writeInt32(max_model_lod_level); // new in v15

	if(object_type == WorldObject::ObjectType_VoxelGroup)
	{
		// Write compressed voxel data
		stream.writeUInt32((uint32)compressed_voxels.size());
		if(compressed_voxels.size() > 0)
			stream.writeData(compressed_voxels.data(), compressed_voxels.dataSizeBytes());
	}

	// New in v17:
	stream.writeFloat(mass);
	stream.writeFloat(friction);
	stream.writeFloat(restitution);
	::writeToStream(centre_of_mass_offset_os, stream); // New in v20
}


void readWorldObjectFromStream(RandomAccessInStream& stream, WorldObject& ob)
{
	// Read version
	const uint32 v = stream.readUInt32();
	if(v > WORLD_OBJECT_SERIALISATION_VERSION)
		throw glare::Exception("Unsupported version " + toString(v) + ", expected " + toString(WORLD_OBJECT_SERIALISATION_VERSION) + ".");

	ob.uid = readUIDFromStream(stream);

	if(v >= 7)
		ob.object_type = (WorldObject::ObjectType)stream.readUInt32(); // TODO: handle invalid values?

	ob.model_url = stream.readStringLengthFirst(10000);
	//if(v >= 2)
	//	ob.material_url = stream.readStringLengthFirst(10000);
	if(v >= 2)
	{
		const size_t num_mats = stream.readUInt32();
		if(num_mats > WorldObject::maxNumMaterials())
			throw glare::Exception("Too many materials: " + toString(num_mats));
		ob.materials.resize(num_mats);
		for(size_t i=0; i<ob.materials.size(); ++i)
		{
			if(ob.materials[i].isNull())
				ob.materials[i] = new WorldMaterial();
			::readWorldMaterialFromStream(stream, *ob.materials[i]);
		}
	}

	if(v >= 13)
	{
		ob.lightmap_url = stream.readStringLengthFirst(10000);
		//conPrint("readFromStream: read lightmap_url: " + ob.lightmap_url);
	}

	if(v >= 4 && v < 10)
	{
		stream.readStringLengthFirst(10000); // read and discard script URL
	}
	else if(v >= 10)
	{
		ob.script = stream.readStringLengthFirst(10000);
	}

	if(v >= 6)
		ob.content = stream.readStringLengthFirst(WorldObject::MAX_CONTENT_SIZE);

	if(v >= 8)
		ob.target_url = stream.readStringLengthFirst(10000);
	
	if(v >= 16)
	{
		ob.audio_source_url = stream.readStringLengthFirst(10000);
		ob.audio_volume = stream.readFloat();
	}

	ob.pos = readVec3FromStream<double>(stream);
	ob.axis = readVec3FromStream<float>(stream);
	ob.angle = stream.readFloat();

	if(v >= 3)
		ob.scale = readVec3FromStream<float>(stream);
	else
		ob.scale = Vec3f(1.f);

	if(v >= 5)
	{
		ob.created_time.readFromStream(stream);
		if(v >= 19)
			ob.last_modified_time.readFromStream(stream);
		else
			ob.last_modified_time = ob.created_time;
		ob.creator_id = readUserIDFromStream(stream);
	}
	else
	{
		ob.created_time = TimeStamp::currentTime();
		ob.creator_id = UserID::invalidUserID();
	}

	if(v >= 11)
		ob.flags = stream.readUInt32();

	if(v >= 14)
	{
		js::AABBox aabb;
		stream.readData(aabb.min_.x, sizeof(float) * 3);
		aabb.min_.x[3] = 1.f;
		stream.readData(aabb.max_.x, sizeof(float) * 3);
		aabb.max_.x[3] = 1.f;

		if(!aabb.min_.isFinite() || !aabb.max_.isFinite())
			aabb = js::AABBox(Vec4f(0,0,0,1), Vec4f(0,0,0,1));


		// Some code for allowing fuzzing to proceed without hitting asserts:
		/*if(ob.axis.length2() < Maths::square(1.0e-10f))
		{
			ob.axis = Vec3f(0,0,1);
			ob.angle = 0;
		}

		if(!(ob.axis.isFinite() && isFinite(ob.angle) && ob.scale.isFinite() && ob.translation.isFinite()))
		{
			ob.axis = Vec3f(0,0,1);
			ob.angle = 0;
			ob.scale = Vec3f(1.f);
			ob.translation = Vec4f(0,0,0,1);
		}*/


		if(v >= 18)
		{
			ob.setAABBOS(aabb);
		}
		else
		{
			if(ob.axis.isFinite() && isFinite(ob.angle) && ob.scale.isFinite() && ob.translation.isFinite())
			{
				// Get approx aabb os from aabb ws.
				const js::AABBox aabb_os = aabb.transformedAABBFast(ob.worldToObMatrix());
				ob.setAABBOS(aabb_os);
			}
			else
			{
				// Can't call setAABBOS() because it will hit an assert in transformChanged() when computing the transformation matrix.
				// conPrint("Invalid transform");
				ob.zeroAABBOS();
			}
		}
	}

	if(v >= 15)
		ob.max_model_lod_level = stream.readInt32();
	
	if(v >= 9 && ob.object_type == WorldObject::ObjectType_VoxelGroup)
	{
		if(v <= 11)
		{
			// Read num voxels
			const uint32 num_voxels = stream.readUInt32();
			if(num_voxels > 1000000)
				throw glare::Exception("Invalid num voxels: " + toString(num_voxels));

			ob.getDecompressedVoxels().resize(num_voxels);

			// Read voxel data
			if(num_voxels > 0)
				stream.readData(ob.getDecompressedVoxels().data(), sizeof(Voxel) * num_voxels);
		}
		else
		{
			// Read compressed voxel data
			const uint32 voxel_data_size = stream.readUInt32();
			if(voxel_data_size > 1000000)
				throw glare::Exception("Invalid voxel_data_size: " + toString(voxel_data_size));

			// Read voxel data
			ob.getCompressedVoxels().resize(voxel_data_size);
			if(voxel_data_size > 0)
				stream.readData(ob.getCompressedVoxels().data(), voxel_data_size);
		}
	}

	if(v >= 17)
	{
		ob.mass = stream.readFloat();
		ob.friction = stream.readFloat();
		ob.restitution = stream.readFloat();
	}

	if(v >= 20)
		ob.centre_of_mass_offset_os = readVec3FromStream<float>(stream);

	// Set ephemeral state
	ob.state = WorldObject::State_Alive;
}


void WorldObject::writeToNetworkStream(RandomAccessOutStream& stream) const // Write without version
{
	::writeToStream(uid, stream);
	stream.writeUInt32((uint32)object_type);
	stream.writeStringLengthFirst(model_url);

	// Write materials
	stream.writeUInt32((uint32)materials.size());
	for(size_t i=0; i<materials.size(); ++i)
		::writeWorldMaterialToStream(*materials[i], stream);

	stream.writeStringLengthFirst(lightmap_url); // new in v13

	stream.writeStringLengthFirst(script);
	stream.writeStringLengthFirst(content);
	stream.writeStringLengthFirst(target_url);
	stream.writeStringLengthFirst(audio_source_url);
	stream.writeFloat(audio_volume);

	::writeToStream(pos, stream);
	::writeToStream(axis, stream);
	stream.writeFloat(angle);
	::writeToStream(scale, stream);

	created_time.writeToStream(stream); // new in v5
	last_modified_time.writeToStream(stream); // new in v19
	::writeToStream(creator_id, stream); // new in v5

	stream.writeUInt32(flags); // new in v11

	stream.writeStringLengthFirst(creator_name);

	stream.writeData(aabb_os.min_.x, sizeof(float) * 3); // new in v14
	stream.writeData(aabb_os.max_.x, sizeof(float) * 3);

	stream.writeInt32(max_model_lod_level); // new in v15

	if(object_type == WorldObject::ObjectType_VoxelGroup)
	{
		// Write compressed voxel data
		stream.writeUInt32((uint32)compressed_voxels.size());
		if(compressed_voxels.size() > 0)
			stream.writeData(compressed_voxels.data(), compressed_voxels.dataSizeBytes());
	}

	// New in v17:
	stream.writeFloat(mass);
	stream.writeFloat(friction);
	stream.writeFloat(restitution);

	// Physics owner id has to be transmitted, or a new client joining will not be aware of who the physics owner of a moving object is, and will incorrectly take ownership of it.
	stream.writeUInt32(physics_owner_id);
	stream.writeDouble(last_physics_ownership_change_global_time);

	// New in v20:
	::writeToStream(centre_of_mass_offset_os, stream);
}


void WorldObject::copyNetworkStateFrom(const WorldObject& other)
{
	// NOTE: The data in here needs to match that in readFromNetworkStreamGivenUID()
	object_type = other.object_type;
	model_url = other.model_url;
	materials = other.materials;

	lightmap_url = other.lightmap_url;

	script = other.script;
	content = other.content;
	target_url = other.target_url;
	audio_source_url = other.audio_source_url;
	audio_volume = other.audio_volume;

	pos = other.pos;
	axis = other.axis;
	angle = other.angle;

	scale = other.scale;

	created_time = other.created_time;
	last_modified_time = other.last_modified_time;
	creator_id = other.creator_id;

	flags = other.flags;

	creator_name = other.creator_name;

	compressed_voxels = other.compressed_voxels;

	aabb_os = other.aabb_os;

	max_model_lod_level = other.max_model_lod_level;

	mass = other.mass;
	friction = other.friction;
	restitution = other.restitution;
	centre_of_mass_offset_os = other.centre_of_mass_offset_os;

	physics_owner_id = other.physics_owner_id;
	last_physics_ownership_change_global_time = other.last_physics_ownership_change_global_time;
}


void readWorldObjectFromNetworkStreamGivenUID(RandomAccessInStream& stream, WorldObject& ob) // UID will have been read already
{
	// NOTE: The data in here needs to match that in copyNetworkStateFrom()

	ob.object_type = (WorldObject::ObjectType)stream.readUInt32(); // TODO: handle invalid values?
	ob.model_url = stream.readStringLengthFirst(10000);
	//if(v >= 2)
	{
		const size_t num_mats = stream.readUInt32();
		if(num_mats > WorldObject::maxNumMaterials())
			throw glare::Exception("Too many materials: " + toString(num_mats));
		ob.materials.resize(num_mats);
		for(size_t i=0; i<ob.materials.size(); ++i)
		{
			if(ob.materials[i].isNull())
				ob.materials[i] = new WorldMaterial();
			readWorldMaterialFromStream(stream, *ob.materials[i]);
		}
	}

	ob.lightmap_url = stream.readStringLengthFirst(10000);

	const std::string new_script = stream.readStringLengthFirst(10000);
	if(ob.script != new_script)
		ob.changed_flags |= WorldObject::SCRIPT_CHANGED;
	ob.script = new_script;

	if(ob.content.empty())
	{
		ob.content = stream.readStringLengthFirst(WorldObject::MAX_CONTENT_SIZE);
		if(!ob.content.empty())
			ob.changed_flags |= WorldObject::CONTENT_CHANGED;
	}
	else
	{
		const std::string new_content = stream.readStringLengthFirst(WorldObject::MAX_CONTENT_SIZE);
		if(ob.content != new_content)
			ob.changed_flags |= WorldObject::CONTENT_CHANGED;
		ob.content = new_content;
	}

	ob.target_url = stream.readStringLengthFirst(10000);

	const std::string new_audio_source_url = stream.readStringLengthFirst(10000);
	if(ob.audio_source_url != new_audio_source_url)
		ob.changed_flags |= WorldObject::AUDIO_SOURCE_URL_CHANGED;
	ob.audio_source_url = new_audio_source_url;
	ob.audio_volume = stream.readFloat();

	ob.pos = readVec3FromStream<double>(stream);
	ob.axis = readVec3FromStream<float>(stream);
	ob.angle = stream.readFloat();

	if(!ob.pos.isFinite())
		ob.pos = Vec3d(0,0,0);
	if(!ob.axis.isFinite())
		ob.axis = Vec3f(1,0,0);
	if(!isFinite(ob.angle))
		ob.angle = 0;

	//if(v >= 3)
	ob.scale = readVec3FromStream<float>(stream);

	ob.created_time.readFromStream(stream);
	ob.last_modified_time.readFromStream(stream);
	ob.creator_id = readUserIDFromStream(stream);

	ob.flags = stream.readUInt32();

	ob.creator_name = stream.readStringLengthFirst(10000);

	js::AABBox aabb;
	stream.readData(aabb.min_.x, sizeof(float) * 3);
	aabb.min_.x[3] = 1.f;
	stream.readData(aabb.max_.x, sizeof(float) * 3);
	aabb.max_.x[3] = 1.f;

	if(!aabb.min_.isFinite() || !aabb.max_.isFinite())
		aabb = js::AABBox(Vec4f(0,0,0,1), Vec4f(0,0,0,1));

	ob.setAABBOS(aabb);

	ob.max_model_lod_level = stream.readInt32();

	if(ob.object_type == WorldObject::ObjectType_VoxelGroup)
	{
		// Read compressed voxel data
		const uint32 voxel_data_size = stream.readUInt32();
		if(voxel_data_size > 1000000)
			throw glare::Exception("Invalid voxel_data_size (too large): " + toString(voxel_data_size));

		// Read voxel data
		ob.getCompressedVoxels().resize(voxel_data_size);
		if(voxel_data_size > 0)
			stream.readData(ob.getCompressedVoxels().data(), voxel_data_size);
	}

	// New in v17:
	if(!stream.endOfStream())
	{
		ob.mass = stream.readFloat();
		ob.friction = stream.readFloat();
		ob.restitution = stream.readFloat();
	}

	if(!stream.endOfStream())
	{
		const uint32 new_physics_owner_id = stream.readUInt32();
		if(new_physics_owner_id != ob.physics_owner_id)
			ob.changed_flags |= WorldObject::PHYSICS_OWNER_CHANGED;
		ob.physics_owner_id = new_physics_owner_id;
	}
	
	if(!stream.endOfStream())
		ob.last_physics_ownership_change_global_time = stream.readDouble();

	if(!stream.endOfStream())
		ob.centre_of_mass_offset_os = readVec3FromStream<float>(stream);

	// Set ephemeral state
	//ob.state = WorldObject::State_Alive;
}


const Matrix4f obToWorldMatrix(const WorldObject& ob)
{
	const Vec4f pos((float)ob.pos.x, (float)ob.pos.y, (float)ob.pos.z, 1.f);

	// Don't use a zero scale component, because it makes the matrix uninvertible, which breaks various things, including picking and normals.
	Vec3f use_scale = ob.scale;
	if(use_scale.x == 0) use_scale.x = 1.0e-6f;
	if(use_scale.y == 0) use_scale.y = 1.0e-6f;
	if(use_scale.z == 0) use_scale.z = 1.0e-6f;

	// Equivalent to
	//return Matrix4f::translationMatrix(pos + ob.translation) *
	//	Matrix4f::rotationMatrix(normalise(ob.axis.toVec4fVector()), ob.angle) *
	//	Matrix4f::scaleMatrix(use_scale.x, use_scale.y, use_scale.z));

	Matrix4f rot = Matrix4f::rotationMatrix(normalise(ob.axis.toVec4fVector()), ob.angle);
	rot.setColumn(0, rot.getColumn(0) * use_scale.x);
	rot.setColumn(1, rot.getColumn(1) * use_scale.y);
	rot.setColumn(2, rot.getColumn(2) * use_scale.z);
	rot.setColumn(3, pos + ob.translation);
	return rot;
}


const Vec3f useScaleForWorldOb(const Vec3f& scale)
{
	// Don't use a zero scale component, because it makes the matrix uninvertible, which breaks various things, including picking and normals.
	Vec3f use_scale = scale;
	if(use_scale.x == 0) use_scale.x = 1.0e-6f;
	if(use_scale.y == 0) use_scale.y = 1.0e-6f;
	if(use_scale.z == 0) use_scale.z = 1.0e-6f;
	return use_scale;
}


const Matrix4f worldToObMatrix(const WorldObject& ob)
{
	const Vec4f pos((float)ob.pos.x, (float)ob.pos.y, (float)ob.pos.z, 1.f);

	return Matrix4f::scaleMatrix(1/ob.scale.x, 1/ob.scale.y, 1/ob.scale.z) *
		Matrix4f::rotationMatrix(normalise(ob.axis.toVec4fVector()), -ob.angle) *
		Matrix4f::translationMatrix(-pos - ob.translation);
}


size_t WorldObject::getTotalMemUsage() const
{
	return sizeof(WorldObject) + 
		compressed_voxels.capacitySizeBytes() + (voxel_group.voxels.capacity() * sizeof(Voxel));
}


int WorldObject::getLightMapSideResForAABBWS(const js::AABBox& aabb_ws)
{
	const float A = aabb_ws.getSurfaceArea();

	// We want an object with maximally sized AABB, similar to that of a parcel, to have the max allowable res (e.g. 512*512)
	// And use a proportionally smaller number of pixels based on a smaller area.

	const float parcel_W = 20;
	const float parcel_H = 10;
	const float parcel_A = parcel_W * parcel_W * 2 + parcel_W * parcel_H * 4;

	const float frac = A / parcel_A;

	const float full_num_px = Maths::square(2048.f);

	const float use_num_px = frac * full_num_px;

	const float use_side_res = std::sqrt(use_num_px);

	const int use_side_res_rounded = Maths::roundUpToMultipleOfPowerOf2((int)use_side_res, (int)4);

	// Clamp to min and max allowable lightmap resolutions
	const int clamped_side_res = myClamp(use_side_res_rounded, 64, 2048);

	return clamped_side_res;
}


struct GetMatIndex
{
	size_t operator() (const Voxel& v)
	{
		return (size_t)v.mat_index;
	}
};


void WorldObject::compressVoxelGroup(const VoxelGroup& group, js::Vector<uint8, 16>& compressed_data_out)
{
	size_t max_bucket = 0;
	for(size_t i=0; i<group.voxels.size(); ++i)
		max_bucket = myMax<size_t>(max_bucket, group.voxels[i].mat_index);

	const size_t num_buckets = max_bucket + 1;

	// Step 1: sort by materials
	glare::AllocatorVector<Voxel, 16> sorted_voxels(group.voxels.size());
	Sort::serialCountingSortWithNumBuckets(group.voxels.data(), sorted_voxels.data(), group.voxels.size(), num_buckets, GetMatIndex());

	//std::vector<Voxel> sorted_voxels = group.voxels;
	//std::sort(sorted_voxels.begin(), sorted_voxels.end(), VoxelComparator());


	// Count num items in each bucket
	std::vector<size_t> counts(num_buckets, 0);
	for(size_t i=0; i<group.voxels.size(); ++i)
		counts[group.voxels[i].mat_index]++;

	Vec3<int> current_pos(0, 0, 0);
	int v_i = 0;

	js::Vector<int, 16> data(1 + (int)counts.size() + group.voxels.size() * 3);
	size_t write_i = 0;

	data[write_i++] = (int)counts.size(); // Write num materials

	for(size_t z=0; z<counts.size(); ++z)
	{
		const size_t count = counts[z];
		data[write_i++] = (int)count; // Write count of voxels with that material

		for(size_t i=0; i<count; ++i)
		{
			Vec3<int> relative_pos = sorted_voxels[v_i].pos - current_pos;
			//conPrint("relative_pos: " + relative_pos.toString());

			data[write_i++] = relative_pos.x;
			data[write_i++] = relative_pos.y;
			data[write_i++] = relative_pos.z;

			current_pos = sorted_voxels[v_i].pos;

			v_i++;
		}
	}

	assert(write_i == data.size());

	const size_t compressed_bound = ZSTD_compressBound(data.size() * sizeof(int));

	compressed_data_out.resizeNoCopy(compressed_bound);
	
	const size_t compressed_size = ZSTD_compress(compressed_data_out.data(), compressed_data_out.size(), data.data(), data.dataSizeBytes(),
		ZSTD_CLEVEL_DEFAULT // compression level
	);

	compressed_data_out.resize(compressed_size);

	// conPrint("uncompressed size:      " + toString(group.voxels.size() * sizeof(Voxel)) + " B");
	// conPrint("compressed_size:        " + toString(compressed_size) + " B");
	// const double ratio = (double)group.voxels.size() * sizeof(Voxel) / compressed_size;
	// conPrint("compression ratio: " + toString(ratio));

	//TEMP: decompress and check we get the same value
#ifndef NDEBUG
	VoxelGroup group2;
	decompressVoxelGroup(compressed_data_out.data(), compressed_data_out.size(), NULL, group2);
	assert(group2.voxels == sorted_voxels);
#endif
}


void WorldObject::decompressVoxelGroup(const uint8* compressed_data, size_t compressed_data_len, glare::Allocator* mem_allocator, VoxelGroup& group_out)
{
	group_out.voxels.clear();

	const uint64 decompressed_size = ZSTD_getFrameContentSize(compressed_data, compressed_data_len);
	if(decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN || decompressed_size == ZSTD_CONTENTSIZE_ERROR)
		throw glare::Exception("Failed to get decompressed_size");

	BufferInStream instream;
	if(mem_allocator)
		instream.buf.setAllocator(mem_allocator);
	instream.buf.resizeNoCopy(decompressed_size);

	const size_t res = ZSTD_decompress(instream.buf.data(), decompressed_size, compressed_data, compressed_data_len);
	if(ZSTD_isError(res))
		throw glare::Exception("Decompression of buffer failed: " + toString(res));
	if(res < decompressed_size)
		throw glare::Exception("Decompression of buffer failed: not enough bytes in result");

	// Do a pass over the data to get the total number of voxels, so that we can resize group_out.voxels up-front.
	int total_num_voxels = 0;
	const int num_mats = instream.readInt32();
	for(int m=0; m<num_mats; ++m)
	{
		const int count = instream.readInt32(); // Number of voxels with this material.
		if(count < 0 || count > 64000000)
			throw glare::Exception("Voxel count is too large: " + toString(count));

		instream.advanceReadIndex(sizeof(Vec3<int>) * count); // Skip over voxel data.
		total_num_voxels += count;
	}

	// Reset stream read index to beginning.
	instream.setReadIndex(0);

	if(total_num_voxels > 64000000)
		throw glare::Exception("Voxel count is too large: " + toString(total_num_voxels));

	group_out.voxels.resizeNoCopy(total_num_voxels);

	Vec3<int> current_pos(0, 0, 0);

	instream.readInt32(); // Read num_mats again
	size_t write_i = 0;
	for(int m=0; m<num_mats; ++m)
	{
		const int count = instream.readInt32(); // Number of voxels with this material.
		if(count < 0 || count > 64000000)
			throw glare::Exception("Voxel count is too large: " + toString(count));

		const Vec3<int>* relative_positions = (const Vec3<int>*)instream.currentReadPtr(); // Pointer should be sufficiently aligned.

		instream.advanceReadIndex(sizeof(Vec3<int>) * count); // Advance past relative positions.  (Checks relative_positions pointer points to a valid range)

		for(int i=0; i<count; ++i)
		{
			const Vec3<int> pos = current_pos + relative_positions[i];

			group_out.voxels[write_i++] = Voxel(pos, m);

			current_pos = pos;
		}
	}

	if(!instream.endOfStream())
		throw glare::Exception("Didn't reach EOF while reading voxels.");
}


void WorldObject::compressVoxels()
{
	if(!this->voxel_group.voxels.empty())
	{
		compressVoxelGroup(this->voxel_group, this->compressed_voxels);
	}
	else
		this->compressed_voxels.clear();
}


void WorldObject::decompressVoxels()
{ 
	if(!this->compressed_voxels.empty()) // If there are compressed voxels:
		decompressVoxelGroup(this->compressed_voxels.data(), this->compressed_voxels.size(), /*mem allocator=*/NULL, this->voxel_group); // Decompress to voxel_group.
	else
		this->voxel_group.voxels.clear(); // Else there are no compressed voxels, so effectively decompress to zero voxels.

	// conPrint("decompressVoxels: decompressed to " + toString(this->voxel_group.voxels.size()) + " voxels.");
}


void WorldObject::clearDecompressedVoxels()
{
	this->voxel_group.voxels.clearAndFreeMem();
	//this->voxel_group.voxels = std::vector<Voxel>();
}


void WorldObject::setAABBOS(const js::AABBox& aabb_os_)
{
	this->aabb_os = aabb_os_;
	
	transformChanged();
}


void WorldObject::zeroAABBOS()
{
	this->aabb_os = js::AABBox(Vec4f(0,0,0,1), Vec4f(0,0,0,1));
	this->centroid_ws = Vec4f(0,0,0,1);
	this->aabb_ws_longest_len = 0;
	this->biased_aabb_len = 0;
}


void WorldObject::transformChanged() // Rebuild centroid_ws, biased_aabb_len
{
	const Matrix4f ob_to_world = this->obToWorldMatrix();

	doTransformChanged(ob_to_world, this->scale.toVec4fVector());
}


js::AABBox VoxelGroup::getAABB() const
{
	// Iterate over voxels and get voxel position bounds
	Vec3<int> minpos( 1000000000);
	Vec3<int> maxpos(-1000000000);
	for(size_t i=0; i<voxels.size(); ++i)
	{
		minpos = minpos.min(voxels[i].pos);
		maxpos = maxpos.max(voxels[i].pos);
	}
	return js::AABBox(Vec4f((float)minpos.x, (float)minpos.y, (float)minpos.z, 1), Vec4f((float)(maxpos.x + 1), (float)(maxpos.y + 1), (float)(maxpos.z + 1), 1));
}


// Throws glare::Exception if transform not OK, for example if any components are infinite or NaN. 
void checkTransformOK(const WorldObject* ob)
{
	// Sanity check position, axis, angle
	if(!ob->pos.isFinite())
		throw glare::Exception("Position had non-finite component.");
	if(!ob->axis.isFinite())
		throw glare::Exception("axis had non-finite component.");
	if(!::isFinite(ob->angle))
		throw glare::Exception("angle was non-finite.");

	const Matrix4f ob_to_world_matrix = obToWorldMatrix(*ob);

	// Sanity check ob_to_world_matrix matrix
	for(int i=0; i<16; ++i)
		if(!::isFinite(ob_to_world_matrix.e[i]))
			throw glare::Exception("ob_to_world_matrix had non-finite component.");

	Matrix4f world_to_ob;
	const bool ob_to_world_invertible = ob_to_world_matrix.getInverseForAffine3Matrix(world_to_ob);
	if(!ob_to_world_invertible)
		throw glare::Exception("ob_to_world_matrix was not invertible."); // TEMP: do we actually need this restriction?

	// Check world_to_ob matrix
	for(int i=0; i<16; ++i)
		if(!::isFinite(world_to_ob.e[i]))
			throw glare::Exception("world_to_ob had non-finite component.");
}


Reference<ObjectEventHandlers> WorldObject::getOrCreateEventHandlers()
{
	if(!event_handlers)
		event_handlers = new ObjectEventHandlers();
	return event_handlers;
}


void doDestroyOb(WorldObject* ob)
{
	if(ob->allocator)
	{
		glare::PoolAllocator* allocator = ob->allocator;
		const int allocation_index = ob->allocation_index;
		ob->~WorldObject(); // Call destructor on object
		allocator->free(allocation_index);
	}
	else
		delete ob;
}


#if BUILD_TESTS


#include <utils/BufferOutStream.h>
#include <utils/BufferViewInStream.h>
#include <utils/TestUtils.h>
#include <utils/FileOutStream.h>


#if 0
// Command line:
// C:\fuzz_corpus\worldobject N:\substrata\testfiles\fuzz_seeds\worldobject

extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	return 0;
}


extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	//TEMP: Write out an object with voxels for a fuzz seed
	/*{
		WorldObject ob;
		ob.object_type = WorldObject::ObjectType_VoxelGroup;
		ob.getDecompressedVoxels().push_back(Voxel(Vec3<int>(0,1,2), 0));
		ob.getDecompressedVoxels().push_back(Voxel(Vec3<int>(4,5,6), 1));
		ob.compressVoxels();

		{
			BufferOutStream outstream;
			ob.writeToStream(outstream);

			FileOutStream file("ob_with_voxels.worldobject");
			file.writeData(outstream.buf.data(), outstream.buf.size());
		}
	}*/


	try
	{
		BufferViewInStream stream(ArrayRef<uint8>(data, size));

		WorldObject ob;
		readWorldObjectFromStream(stream, ob);
	}
	catch(glare::Exception&)
	{
	}
	
	return 0;  // Non-zero return values are reserved for future use.
}
#endif


void WorldObject::test()
{
	conPrint("WorldObject::test()");
	try
	{

		{
			WorldObject ob;
			ob.pos = Vec3d(0.0);
			ob.axis = Vec3f(0,0,1);
			ob.angle = 0;
			ob.materials.push_back(new WorldMaterial());
			ob.materials.push_back(new WorldMaterial());
			ob.materials.push_back(new WorldMaterial());

			ob.script = "abc";

			BufferOutStream buf;
			ob.writeToStream(buf);

			// Test reading back object works
			BufferInStream instream(ArrayRef<uint8>(buf.buf.data(), buf.buf.size()));
			WorldObject ob2;
			readWorldObjectFromStream(instream, ob2);
			testAssert(ob2.materials.size() == ob.materials.size());
		}

		// Test with some large materials
		{
			WorldObject ob;
			ob.pos = Vec3d(0.0);
			ob.axis = Vec3f(0,0,1);
			ob.angle = 0;
			ob.materials.push_back(new WorldMaterial());
			ob.materials.back()->normal_map_url = std::string(8192, 'A');
			ob.materials.push_back(new WorldMaterial());
			ob.materials.back()->normal_map_url = std::string(8192, 'B');
			ob.materials.push_back(new WorldMaterial());

			ob.script = "abc";

			BufferOutStream buf;
			ob.writeToStream(buf);

			// Test reading back object works
			BufferInStream instream(ArrayRef<uint8>(buf.buf.data(), buf.buf.size()));
			WorldObject ob2;
			readWorldObjectFromStream(instream, ob2);
			testAssert(ob.materials.size() == ob2.materials.size());
			testAssert(ob.materials[0]->normal_map_url == ob2.materials[0]->normal_map_url);
			testAssert(ob.materials[1]->normal_map_url == ob2.materials[1]->normal_map_url);
		}

		// Test that an object with lots of materials serialises without using too much mem
		{
			WorldObject ob;
			ob.pos = Vec3d(0.0);
			ob.axis = Vec3f(0,0,1);
			ob.angle = 0;

			for(int i=0; i<1024; ++i)
				ob.materials.push_back(new WorldMaterial());

			ob.script = "abc";

			BufferOutStream buf;
			ob.writeToStream(buf);

			// Test reading back object works
			BufferInStream instream(ArrayRef<uint8>(buf.buf.data(), buf.buf.size()));
			WorldObject ob2;
			readWorldObjectFromStream(instream, ob2);
			testAssert(ob2.materials.size() == ob.materials.size());
		}
	}
	catch(glare::Exception& e)
	{
		failTest(e.what());
	}

	conPrint("WorldObject::test() done");
}

#endif // BUILD_TESTS
