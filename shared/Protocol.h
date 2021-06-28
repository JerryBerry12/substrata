/*=====================================================================
Protocol.h
----------
Copyright Glare Technologies Limited 2017 -
=====================================================================*/
#pragma once


#include "utils/Platform.h"

/*
CyberspaceProtocolVersion
20: Added lightmap_url to WorldObject.
21: Added parcel auction stuff
22: Added grid object querying stuff
23: Added ObjectModelURLChanged
24: Added material emission_lum_flux
25: Added AvatarIsHere message, Added anim_state to avatar
*/
namespace Protocol
{


const uint32 CyberspaceHello = 1357924680;
const uint32 CyberspaceProtocolVersion = 25;
const uint32 ClientProtocolOK		= 10000;
const uint32 ClientProtocolTooOld	= 10001;
const uint32 ClientProtocolTooNew	= 10002;

const uint32 ConnectionTypeUpdates				= 500;
const uint32 ConnectionTypeUploadResource		= 501;
const uint32 ConnectionTypeDownloadResources	= 502;
const uint32 ConnectionTypeWebsite				= 503; // A connection from the webserver.
const uint32 ConnectionTypeScreenshotBot		= 504; // A connection from the screenshot bot.
const uint32 ConnectionTypeEthBot				= 505; // A connection from the Ethereum bot.


const uint32 AvatarCreated			= 1000;
const uint32 AvatarDestroyed		= 1001;
const uint32 AvatarTransformUpdate	= 1002;
const uint32 AvatarFullUpdate		= 1003;
const uint32 CreateAvatar			= 1004;
const uint32 AvatarIsHere			= 1005;

const uint32 ChatMessageID			= 2000;

const uint32 ObjectCreated			= 3000;
const uint32 ObjectDestroyed		= 3001;
const uint32 ObjectTransformUpdate	= 3002;
const uint32 ObjectFullUpdate		= 3003;
const uint32 ObjectLightmapURLChanged		= 3010; // The object's lightmap URL changed.
const uint32 ObjectFlagsChanged		= 3011;
const uint32 ObjectModelURLChanged	= 3012;

const uint32 CreateObject			= 3004; // Client wants to create an object.
const uint32 DestroyObject			= 3005; // Client wants to destroy an object.

const uint32 QueryObjects			= 3020; // Client wants to query objects in certain grid cells
const uint32 ObjectInitialSend		= 3021;


const uint32 ParcelCreated			= 3100;
const uint32 ParcelDestroyed		= 3101;
const uint32 ParcelFullUpdate		= 3103;

const uint32 QueryParcels			= 3150;
const uint32 ParcelList				= 3160;

const uint32 GetAllObjects			= 3600; // Client wants to get all objects from server
const uint32 AllObjectsSent			= 3601; // Server has sent all objects


//TEMP HACK move elsewhere
const uint32 GetFile				= 4000;
const uint32 GetFiles				= 4001; // Client wants to download multiple resources from the server.

const uint32 NewResourceOnServer	= 4100; // A file has been uploaded to the server



											//TEMP HACK move elsewhere
//const uint32 UploadResource			= 5000;

const uint32 UploadAllowed			= 5100;
const uint32 LogInFailure			= 5101;
const uint32 InvalidFileSize		= 5102;
const uint32 NoWritePermissions		= 5103;


//TEMP HACK move elsewhere
const uint32 UserSelectedObject		= 6000;
const uint32 UserDeselectedObject	= 6001;

const uint32 InfoMessageID			= 7001;
const uint32 ErrorMessageID			= 7002;

const uint32 LogInMessage			= 8000;
const uint32 LogOutMessage			= 8001;
const uint32 SignUpMessage			= 8002;
const uint32 LoggedInMessageID		= 8003;
const uint32 LoggedOutMessageID		= 8004;
const uint32 SignedUpMessageID		= 8005;

const uint32 RequestPasswordReset	= 8010; // Client wants to reset the password for a given email address.
const uint32 ChangePasswordWithResetToken = 8011; // Client is sending the password reset token, email address, and the new password.

const uint32 TimeSyncMessage		= 9000; // Sends the current time

const uint32 ScreenShotRequest		= 11001;
const uint32 ScreenShotSucceeded	= 11002;


const uint32 SubmitEthTransactionRequest		= 12001;
const uint32 EthTransactionSubmitted			= 12002;
const uint32 EthTransactionSubmissionFailed		= 12003;


} // end namespace Protocol
