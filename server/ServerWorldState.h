/*=====================================================================
ServerWorldState.h
------------------
Copyright Glare Technologies Limited 2021 -
Generated at 2016-01-12 12:22:34 +1300
=====================================================================*/
#pragma once


#include "../shared/ResourceManager.h"
#include "../shared/Avatar.h"
#include "../shared/WorldObject.h"
#include "../shared/Parcel.h"
#include "User.h"
#include "Order.h"
#include "UserWebSession.h"
#include "ParcelAuction.h"
#include "Screenshot.h"
#include "SubEthTransaction.h"
#include <ThreadSafeRefCounted.h>
#include <Platform.h>
#include <Mutex.h>
#include <map>
#include <unordered_set>


class ServerWorldState : public ThreadSafeRefCounted
{
public:

	std::map<UID, Reference<Avatar>> avatars;

	std::map<UID, WorldObjectRef> objects;
	std::unordered_set<WorldObjectRef, WorldObjectRefHash> dirty_from_remote_objects;

	std::map<ParcelID, ParcelRef> parcels;
};


struct OpenSeaParcelListing
{
	ParcelID parcel_id;
};


struct TileInfo
{
	ScreenshotRef cur_tile_screenshot;
	ScreenshotRef prev_tile_screenshot;
};


/*=====================================================================
ServerWorldState
----------------

=====================================================================*/
class ServerAllWorldsState : public ThreadSafeRefCounted
{
public:
	ServerAllWorldsState();
	~ServerAllWorldsState();

	void readFromDisk(const std::string& path);
	void serialiseToDisk(const std::string& path);
	void denormaliseData(); // Build/update cached/denormalised fields like creator_name.  Mutex should be locked already.

	UID getNextObjectUID(); // Gets and then increments next_object_uid
	UID getNextAvatarUID(); // Gets and then increments next_avatar_uid.  Locks mutex.
	uint64 getNextOrderUID(); // Gets and then increments next_order_uid.  Locks mutex.
	uint64 getNextSubEthTransactionUID();

	void markAsChanged() { changed = 1; }
	void clearChangedFlag() { changed = 0; }
	bool hasChanged() const { return changed != 0; }

	void setUserWebMessage(const UserID& user_id, const std::string& s);
	std::string getAndRemoveUserWebMessage(const UserID& user_id); // returns empty string if no message or user

	Reference<ServerWorldState> getRootWorldState() { return world_states[""]; } // Guaranteed to be return a non-null reference

	Reference<ResourceManager> resource_manager;

	std::map<UserID, Reference<User>> user_id_to_users;  // User id to user
	std::map<std::string, Reference<User>> name_to_users; // Username to user

	std::map<uint64, OrderRef> orders; // Order ID to order

	std::map<std::string, Reference<ServerWorldState> > world_states;

	std::map<std::string, UserWebSessionRef> user_web_sessions; // Map from key to UserWebSession
	
	std::map<uint32, ParcelAuctionRef> parcel_auctions; // ParcelAuction id to ParcelAuction

	std::map<uint64, ScreenshotRef> screenshots;// Screenshot id to ScreenshotRef

	std::map<uint64, SubEthTransactionRef> sub_eth_transactions; // SubEthTransaction id to SubEthTransaction

	int min_next_nonce;

	// For the map:
	std::map<Vec3<int>, TileInfo> map_tile_info;

	int last_parcel_sale_update_hour;
	int last_parcel_sale_update_day;
	int last_parcel_sale_update_year;


	// Ephemeral state that is not serialised to disk.  Set by CoinbasePollerThread.
	double BTC_per_EUR;
	double ETH_per_EUR;

	// Ephemeral state that is not serialised to disk.  Set by OpenSeaPollerThread.
	std::vector<OpenSeaParcelListing> opensea_parcel_listings;

	TimeStamp last_screenshot_bot_contact_time;
	TimeStamp last_lightmapper_bot_contact_time;
	TimeStamp last_eth_bot_contact_time;

	std::map<UserID, std::string> user_web_messages; // For displaying an informational or error message on the next webpage served to a user.

	::Mutex mutex;
private:
	GLARE_DISABLE_COPY(ServerAllWorldsState);

	glare::AtomicInt changed;

	UID next_object_uid;
	UID next_avatar_uid;
	uint64 next_order_uid;
	uint64 next_sub_eth_transaction_uid;
};
