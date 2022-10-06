/*=====================================================================
MainWindow.h
------------
Copyright Glare Technologies Limited 2018 -
=====================================================================*/
#pragma once


#include "PhysicsWorld.h"
#include "ModelLoading.h"
#include "PlayerPhysics.h"
#include "ClientThread.h"
#include "WorldState.h"
#include "CameraController.h"
#include "ProximityLoader.h"
#include "AnimatedTextureManager.h"
#include "UndoBuffer.h"
#include "LogWindow.h"
#include "GestureUI.h"
#include "ObInfoUI.h"
#include "MiscInfoUI.h"
#include "DownloadingResourceQueue.h"
#include "LoadItemQueue.h"
#include "MeshManager.h"
#include "../opengl/OpenGLEngine.h"
#include "../opengl/TextureLoading.h"
//#include "../opengl/WGL.h"
#include "../shared/ResourceManager.h"
#include "../shared/WorldObject.h"
#include "../indigo/ThreadContext.h"
#include "../utils/ArgumentParser.h"
#include "../utils/Timer.h"
#include "../utils/TaskManager.h"
#include "../utils/StandardPrintOutput.h"
#include "../utils/CircularBuffer.h"
#include "../utils/ComObHandle.h"
#include "../maths/PCG32.h"
#include "../maths/LineSegment4f.h"
#include "../video/VideoReader.h"
#include "../audio/AudioEngine.h"
#include <QtCore/QEvent>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtWidgets/QMainWindow>
#include <string>
#include <fstream>
#include <unordered_set>
#include <unordered_map>
#include <deque>
class ArgumentParser;
namespace Ui { class MainWindow; }
class TextureServer;
class QSettings;
class UserDetailsWidget;
class URLWidget;
class QLabel;
class ModelLoadedThreadMessage;
class TextureLoadedThreadMessage;
struct tls_config;
class SubstrataVideoReaderCallback;
struct CreateVidReaderTask;
class BiomeManager;
class ScriptLoadedThreadMessage;
namespace glare { class PoolAllocator; }

struct ID3D11Device;
struct IMFDXGIDeviceManager;


struct DownloadingResourceInfo
{
	DownloadingResourceInfo() : use_sRGB(true) {}

	bool use_sRGB; // For downloading textures.  We keep track of this so we can load e.g. metallic-roughness textures into the OpenGL engine without sRGB.

	Vec3d pos; // Position of object using the resource
	float size_factor;
};


class MainWindow : public QMainWindow, public ObLoadingCallbacks, public PrintOutput, public GLUITextRendererCallback // , public PhysicsWorldBodyActivationCallbacks
{
	Q_OBJECT
public:
	MainWindow(const std::string& base_dir_path, const std::string& appdata_path, const ArgumentParser& args,
		QWidget *parent = 0);
	~MainWindow();

	friend struct AnimatedTexObData;

	void initialise();

	void connectToServer(const std::string& URL/*const std::string& hostname, const std::string& worldname*/); // Disconnect from any current server, connect to new server

	void afterGLInitInitialise();

	void updateGroundPlane();

	void logMessage(const std::string& msg); // Appends to LogWindow log display.
	void logAndConPrintMessage(const std::string& msg); // Print to console, and appends to LogWindow log display.

	// PrintOutput interface
	virtual void print(const std::string& s); // Print a message and a newline character.
	virtual void printStr(const std::string& s); // Print a message without a newline character.


	// Semicolon is for intellisense, see http://www.qtsoftware.com/developer/faqs/faq.2007-08-23.5900165993
signals:;
	void resolutionChanged(int, int);

private slots:;
	void on_actionAvatarSettings_triggered();
	void on_actionAddObject_triggered();
	void on_actionAddHypercard_triggered();
	void on_actionAdd_Voxels_triggered();
	void on_actionAdd_Spotlight_triggered();
	void on_actionAdd_Web_View_triggered();
	void on_actionAdd_Audio_Source_triggered();
	void on_actionCopy_Object_triggered();
	void on_actionPaste_Object_triggered();
	void on_actionCloneObject_triggered();
	void on_actionDeleteObject_triggered();
	void on_actionReset_Layout_triggered();
	void on_actionLogIn_triggered();
	void on_actionSignUp_triggered();
	void on_actionLogOut_triggered();
	void on_actionShow_Parcels_triggered();
	void on_actionFly_Mode_triggered();
	void on_actionThird_Person_Camera_triggered();
	void on_actionGoToMainWorld_triggered();
	void on_actionGoToPersonalWorld_triggered();
	void on_actionGo_to_CryptoVoxels_World_triggered();
	void on_actionGo_to_Parcel_triggered();
	void on_actionFind_Object_triggered();
	void on_actionList_Objects_Nearby_triggered();
	void on_actionExport_view_to_Indigo_triggered();
	void on_actionTake_Screenshot_triggered();
	void on_actionShow_Screenshot_Folder_triggered();
	void on_actionAbout_Substrata_triggered();
	void on_actionOptions_triggered();
	void on_actionUndo_triggered();
	void on_actionRedo_triggered();
	void on_actionShow_Log_triggered();
	void on_actionBake_Lightmaps_fast_for_all_objects_in_parcel_triggered();
	void on_actionBake_lightmaps_high_quality_for_all_objects_in_parcel_triggered();

	void applyUndoOrRedoObject(const WorldObjectRef& ob);

	void sendChatMessageSlot();

	void glWidgetMouseClicked(QMouseEvent* e);
	void glWidgetMousePressed(QMouseEvent* e);
	void glWidgetMouseDoubleClicked(QMouseEvent* e);
	void glWidgetMouseMoved(QMouseEvent* e);
	void glWidgetKeyPressed(QKeyEvent* e);
	void glWidgetkeyReleased(QKeyEvent* e);
	void glWidgetMouseWheelEvent(QWheelEvent* e);
	void glWidgetViewportResized(int w, int h);
	void cameraUpdated();
	void playerMoveKeyPressed();
	void onIndigoViewDockWidgetVisibilityChanged(bool v);

	void objectEditedSlot();
	void parcelEditedSlot();
	void bakeObjectLightmapSlot(); // Bake the currently selected object lightmap
	void bakeObjectLightmapHighQualSlot(); // Bake the currently selected object lightmap
	void removeLightmapSignalSlot();
	void posAndRot3DControlsToggledSlot();
	void URLChangedSlot();
	void materialSelectedInBrowser(const std::string& path);
	void sendLightmapNeededFlagsSlot();
	void updateObjectEditorObTransformSlot();
	void handleURL(const QUrl& url);
public:
	void webViewDataLinkHovered(const QString& url);
	void webViewMouseDoubleClicked(QMouseEvent* e);
private:
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	bool nativeEvent(const QByteArray& event_type, void* message, qintptr* result);
#else
	bool nativeEvent(const QByteArray& event_type, void* message, long* result);
#endif
	void closeEvent(QCloseEvent* event);
	virtual void timerEvent(QTimerEvent* event);
	void rotateObject(WorldObjectRef ob, const Vec4f& axis, float angle);
	void selectObject(const WorldObjectRef& ob, int selected_tri_index);
	void deleteSelectedObject();
	void deselectObject();
	void deselectParcel();
	void visitSubURL(const std::string& URL); // Visit a substrata 'sub://' URL.  Checks hostname and only reconnects if the hostname is different from the current one.
	GLObjectRef makeNameTagGLObject(const std::string& nametag);
	void doObjectSelectionTraceForMouseEvent(QMouseEvent* e);
public:
	virtual OpenGLTextureRef makeToolTipTexture(const std::string& tooltip_text);
private:
	void loadModelForObject(WorldObject* ob);
	void loadModelForAvatar(Avatar* ob);
	void loadScriptForObject(WorldObject* ob);
	void handleScriptLoadedForObUsingScript(ScriptLoadedThreadMessage* loaded_msg, WorldObject* ob);
	void doBiomeScatteringForObject(WorldObject* ob);
	void loadAudioForObject(WorldObject* ob);
	void showErrorNotification(const std::string& message);
	void showInfoNotification(const std::string& message);
	void startDownloadingResourcesForObject(WorldObject* ob, int ob_lod_level);
	void startDownloadingResourcesForAvatar(Avatar* ob, int ob_lod_level);
	void startDownloadingResource(const std::string& url, const Vec4f& pos_ws, const js::AABBox& ob_aabb_ws, DownloadingResourceInfo& resouce_info); // For every resource that the object uses (model, textures etc..), if the resource is not present locally, start downloading it.
	void evalObjectScript(WorldObject* ob, float use_global_time, Matrix4f& ob_to_world_out);
	void updateStatusBar();
	bool haveParcelObjectCreatePermissions(const Vec3d& new_ob_pos, bool& in_parcel_out);
	bool haveObjectWritePermissions(const js::AABBox& new_aabb_ws, bool& ob_pos_in_parcel_out);
	void addParcelObjects();
	void removeParcelObjects();
	void recolourParcelsForLoggedInState();
	void updateSelectedObjectPlacementBeam();
	void updateInstancedCopiesOfObject(WorldObject* ob);
	void removeInstancesOfObject(WorldObject* ob);
	void bakeLightmapsForAllObjectsInParcel(uint32 lightmap_flag);
	
	bool objectModificationAllowed(const WorldObject& ob);
	bool objectModificationAllowedWithMsg(const WorldObject& ob, const std::string& action); // Also shows error notifications if modification is not allowed.
	// Action will be printed in error message, could be "modify" or "delete"
	bool objectIsInParcelForWhichLoggedInUserHasWritePerms(const WorldObject& ob);

	void updateOnlineUsersList(); // Works off world state avatars.
	bool areEditingVoxels();
	Vec4f getDirForPixelTrace(int pixel_pos_x, int pixel_pos_y);

	bool getPixelForPoint(const Vec4f& point_ws, Vec2f& pixel_coords_out); // Returns true if point is visible from camera.
	Vec4f pointOnLineWorldSpace(const Vec4f& p_a_ws, const Vec4f& p_b_ws, const Vec2f& pixel_coords);

	void updateVoxelEditMarkers();
	void pickUpSelectedObject();
	void dropSelectedObject();
	void setUIForSelectedObject(); // Enable/disable delete object action etc..

	void checkForLODChanges();
	void checkForAudioRangeChanges();

	int mouseOverAxisArrowOrRotArc(const Vec2f& pixel_coords, Vec4f& closest_seg_point_ws_out); // Returns closest axis arrow or -1 if no close.

	struct EdgeMarker
	{
		EdgeMarker(const Vec4f& p, const Vec4f& n, float scale_) : pos(p), normal(n), scale(scale_) {}
		EdgeMarker() {}
		Vec4f pos;
		Vec4f normal;
		float scale;
	};
	
	// If the object was not in a parcel with write permissions at all, returns false.
	// If the object can not be made to fit in the current parcel, returns false.
	// new_ob_pos_out is set to new, clamped position.
	bool clampObjectPositionToParcelForNewTransform(GLObjectRef& opengl_ob, const Vec3d& old_ob_pos,
		const Matrix4f& tentative_to_world_matrix, js::Vector<EdgeMarker, 16>& edge_markers_out, Vec3d& new_ob_pos_out);
public:
	bool checkAddTextureToProcessedSet(const std::string& path); // returns true if was not in processed set (and hence this call added it), false if it was.
	bool checkAddModelToProcessedSet(const std::string& url); // returns true if was not in processed set (and hence this call added it), false if it was.
	bool checkAddAudioToProcessedSet(const std::string& url); // returns true if was not in processed set (and hence this call added it), false if it was.
	bool checkAddScriptToProcessedSet(const std::string& script_content); // returns true if was not in processed set (and hence this call added it), false if it was.


	void startLoadingTextureForObject(const Vec3d& pos, const js::AABBox& aabb_ws, const WorldMaterial& world_mat, int ob_lod_level, const std::string& texture_url, bool tex_has_alpha, bool use_sRGB);
	void startLoadingTexturesForObject(const WorldObject& ob, int ob_lod_level);
	void startLoadingTexturesForAvatar(const Avatar& ob, int ob_lod_level);
	void removeAndDeleteGLAndPhysicsObjectsForOb(WorldObject& ob);
	void removeAndDeleteGLObjectForAvatar(Avatar& ob);
	void addPlaceholderObjectsForOb(WorldObject& ob);
	void setUpForScreenshot();
	void saveScreenshot(); // Throws glare::Exception on failure

	// ObLoadingCallbacks interface
	virtual void loadObject(WorldObjectRef ob);
	virtual void unloadObject(WorldObjectRef ob);
	virtual void newCellInProximity(const Vec3<int>& cell_coords);

	void tryToMoveObject(/*const Matrix4f& tentative_new_to_world*/const Vec4f& desired_new_ob_pos);

	void updateObjectModelForChangedDecompressedVoxels(WorldObjectRef& ob);

	void performGestureClicked(const std::string& gesture_name, bool animate_head, bool loop_anim);
	void stopGestureClicked(const std::string& gesture_name);
	void stopGesture();
	void setSelfieModeEnabled(bool enabled);

	QPoint getGlWidgetPosInGlobalSpace(); // Get top left of the GLWidget in global screen coordinates.

	void createObject(const std::string& mesh_path, BatchedMeshRef loaded_mesh, bool loaded_mesh_is_image_cube,
		const js::Vector<Voxel, 16>& decompressed_voxels, const Vec3d& ob_pos, const Vec3f& scale, const Vec3f& axis, float angle, const std::vector<WorldMaterialRef>& materials);
	void createImageObject(const std::string& local_image_path);
	void createModelObject(const std::string& local_model_path);
	void createImageObjectForWidthAndHeight(const std::string& local_image_path, int w, int h, bool has_alpha);

	virtual void dragEnterEvent(QDragEnterEvent* event);
	virtual void dropEvent(QDropEvent* event);

	void handlePasteOrDropMimeData(const QMimeData* mime_data);

	void disconnectFromServerAndClearAllObjects(); // Remove any WorldObjectRefs held by MainWindow.


	// PhysicsWorldBodyActivationCallbacks interface:
	//virtual void onBodyActivated(PhysicsObject& ob) override;
	//virtual void onBodyDeactivated(PhysicsObject& ob) override;

	//BuildUInt8MapTextureDataScratchState build_uint8_map_scratch_state;

	std::string base_dir_path;
	std::string appdata_path;
private:
	ArgumentParser parsed_args;

	Timer total_timer;

public:
	Ui::MainWindow* ui;

	CameraController cam_controller;

	Reference<PhysicsWorld> physics_world;

	PlayerPhysics player_physics;

	Timer time_since_last_timer_ev;
	Timer time_since_update_packet_sent;

	Reference<ClientThread> client_thread;
	ThreadManager client_thread_manager;
	ThreadManager resource_upload_thread_manager;
	ThreadManager resource_download_thread_manager;
	ThreadManager net_resource_download_thread_manager;
	ThreadManager save_resources_db_thread_manager;

	glare::AtomicInt num_non_net_resources_downloading;
	glare::AtomicInt num_net_resources_downloading;
	glare::AtomicInt num_resources_uploading;

	Reference<WorldState> world_state;

	//std::map<const Avatar*, GLObjectRef> avatar_gl_objects;
	//std::map<GLObjectRef*, bool> avatars_

	TextureServer* texture_server;

	QSettings* settings;

	ThreadSafeQueue<Reference<ThreadMessage> > msg_queue; // for messages from ClientThread etc.. to this object.

	WorldObjectRef selected_ob;
	Vec4f selection_vec_cs; // Vector from camera to selected point on object, in camera space
	Vec4f selection_point_os; // Point on selected object where selection ray hit, in object space.
	bool selected_ob_picked_up; // Is selected object 'picked up' e.g. being moved?

	ParcelRef selected_parcel;

	std::string resources_dir;
	Reference<ResourceManager> resource_manager;

	// NOTE: these object sets need to be cleared in connectToServer(), also when removing a dead object in ob->state == WorldObject::State_Dead case in timerEvent, the object needs to be removed
	// from any of these sets it is in.

	// NOTE: Use std::set instead of unordered_set, so that iteration over objects is in memory order.
	std::set<WorldObjectRef> active_objects; // Objects that have moved recently and so need interpolation done on them.
	std::set<WorldObjectRef> obs_with_animated_tex; // Objects with animated textures (e.g. gifs or mp4s)
	std::set<WorldObjectRef> web_view_obs;
	std::set<WorldObjectRef> obs_with_scripts; // Objects with non-null script_evaluator
	//std::set<WorldObjectRef> physics_activated_obs;

	//std::ofstream logfile;

	UserDetailsWidget* user_details;
	URLWidget* url_widget;

	enum ServerConnectionState
	{
		ServerConnectionState_NotConnected,
		ServerConnectionState_Connecting,
		ServerConnectionState_Connected
	};
	ServerConnectionState connection_state;

	UserID logged_in_user_id;
	std::string logged_in_user_name;

	bool shown_object_modification_error_msg;

	Reference<Indigo::Mesh> ground_quad_mesh;
	Reference<OpenGLMeshRenderData> ground_quad_mesh_opengl_data;
	Reference<RayMesh> ground_quad_raymesh;

	struct GroundQuad
	{
		GLObjectRef gl_ob;
		Reference<PhysicsObject> phy_ob;
	};
	std::map<Vec2i, GroundQuad> ground_quads;

	ProximityLoader proximity_loader;

	float load_distance, load_distance2;

	Reference<OpenGLMeshRenderData> hypercard_quad_opengl_mesh; // Also used for name tags.
	Reference<RayMesh> hypercard_quad_raymesh;

	Reference<OpenGLMeshRenderData> image_cube_opengl_mesh; // For images, web-views etc.
	Reference<RayMesh> image_cube_raymesh;

	Reference<OpenGLMeshRenderData> spotlight_opengl_mesh;
	Reference<RayMesh> spotlight_raymesh;


	Reference<RayMesh> unit_cube_raymesh;

	Reference<GLObject> ob_placement_beam;
	Reference<GLObject> ob_placement_marker;

	Reference<GLObject> voxel_edit_marker;
	bool voxel_edit_marker_in_engine;
	Reference<GLObject> voxel_edit_face_marker;
	bool voxel_edit_face_marker_in_engine;

	Reference<GLObject> ob_denied_move_marker; // Prototype object
	std::vector<Reference<GLObject> > ob_denied_move_markers;

	static const int NUM_AXIS_ARROWS = 3;
	LineSegment4f axis_arrow_segments[NUM_AXIS_ARROWS];
	GLObjectRef axis_arrow_objects[NUM_AXIS_ARROWS]; // For ob placement

	std::vector<LineSegment4f> rot_handle_lines[3];
	GLObjectRef rot_handle_arc_objects[3];

	bool axis_and_rot_obs_enabled; // Are the axis arrow objects and rotation arcs inserted into the opengl engine? (and grabbable)

	int grabbed_axis; // -1 if no axis grabbed, [0, 3) if grabbed a translation arrow, [3, 6) if grabbed a rotation arc.
	Vec4f grabbed_point_ws; // Approximate point on arrow line we grabbed, in world space.
	Vec4f ob_origin_at_grab;

	float grabbed_angle;
	float original_grabbed_angle;
	float grabbed_arc_angle_offset;

	Reference<OpenGLProgram> parcel_shader_prog;

	StandardPrintOutput print_output;
	glare::TaskManager task_manager; // General purpose task manager, for quick/blocking multithreaded builds of stuff.
	
	glare::TaskManager model_and_texture_loader_task_manager;
public:
private:
	glare::TaskManager model_building_subsidary_task_manager; // Just for use in ModelLoading::makeGLObjectForModelURLAndMaterials in LoadModelTask etc..
public:
	MeshManager mesh_manager;
private:
	struct Notification
	{
		double creation_time;
		QLabel* label;
	};

	std::list<Notification> notifications;

	bool need_help_info_dock_widget_position; // We may need to position the Help info dock widget to the bottom right of the GL view.
	// But we need to wait until the gl view has been reszied before we do this, so set this flag to do in a timer event.

	std::string server_hostname; // e.g. "substrata.info" or "localhost"
	std::string server_worldname; // e.g. "" or "ono-sendai"
	int url_parcel_uid; // Was there a parcel UID in the URL? e.g. was it like sub://localhost/parcel/200?  If so we want to move there when the parcels are loaded and we know where it is. 
	// -1 if no parcel UID in URL.

	Timer fps_display_timer;
	int num_frames_since_fps_timer_reset;
	double last_fps;

	// ModelLoadedThreadMessages that have been sent to this thread, but are still to be processed.
	std::deque<Reference<ModelLoadedThreadMessage> > model_loaded_messages_to_process;
	
	std::deque<Reference<TextureLoadedThreadMessage> > texture_loaded_messages_to_process;
	
	bool process_model_loaded_next;



	// Textures being loaded or already loaded.
	// We have this set so that we don't process the same texture from multiple LoadTextureTasks running in parallel.
	std::unordered_set<std::string> textures_processed;

	// Models being loaded or already loaded.
	// We have this set so that we don't process the same model from multiple LoadModelTasks running in parallel.
	std::unordered_set<std::string> models_processed;

	// Audio files being loaded or already loaded.
	// We have this set so that we don't process the same audio from multiple LoadAudioTasks running in parallel.
	std::unordered_set<std::string> audio_processed;

	std::unordered_set<std::string> script_content_processed;


	QTimer* update_ob_editor_transform_timer;

	QTimer* lightmap_flag_timer;

	std::set<WorldObjectRef> objs_with_lightmap_rebuild_needed;

	Timer stats_timer;

public:
	Vec3d screenshot_campos;
	Vec3d screenshot_camangles;
	int screenshot_width_px;
	float screenshot_ortho_sensor_width_m;
	int screenshot_highlight_parcel_id;
	std::string screenshot_output_path;
	bool done_screenshot_setup;
	bool run_as_screenshot_slave;
	bool taking_map_screenshot;
	bool test_screenshot_taking;
	MySocketRef screenshot_command_socket;
	Timer time_since_last_screenshot;
	Timer time_since_last_waiting_msg;

private:
	struct tls_config* client_tls_config;

	PCG32 rng;
#if defined(_WIN32)
	ComObHandle<ID3D11Device> d3d_device;
	ComObHandle<IMFDXGIDeviceManager> device_manager;
	//HANDLE interop_device_handle;

	//WGL wgl_funcs;
#endif

public:
	glare::AudioEngine audio_engine;
private:
	UndoBuffer undo_buffer;

	LogWindow* log_window;
public:
	std::vector<GLObjectRef> test_obs;
	std::vector<glare::AudioSourceRef> test_srcs;

	//std::vector<AudioSourceRef> footstep_sources;

	Timer last_footstep_timer;
	int last_foostep_side;

	double last_timerEvent_CPU_work_elapsed;
	double last_animated_tex_time;
	double last_model_and_tex_loading_time;
	int last_num_gif_textures_processed;
	int last_num_mp4_textures_processed;

	Timer time_since_object_edited; // For undo edit merging.
	bool force_new_undo_edit; // // Multiple edits using the object editor, in a short timespan, will be merged together, unless force_new_undo_edit is true (is set when undo or redo is issued).
	std::map<UID, UID> recreated_ob_uid; // Map from old object UID to recreated object UID when an object deletion is undone.

	UID last_restored_ob_uid_in_edit;


	GLUIRef gl_ui;
	GestureUI gesture_ui;
	ObInfoUI ob_info_ui; // For object info and hyperlinks etc.
	MiscInfoUI misc_info_ui; // For showing messages from the server etc.

	bool running_destructor;

	BiomeManager* biome_manager;

	DownloadingResourceQueue download_queue;
	Timer download_queue_sort_timer;
	Timer load_item_queue_sort_timer;

	LoadItemQueue load_item_queue;

	SocketBufferOutStream scratch_packet;

	js::Vector<Vec4f, 16> temp_av_positions;

	std::map<std::string, DownloadingResourceInfo> URL_to_downloading_info; // Map from URL to info about the resource, for currently downloading resources.

	std::map<std::string, std::set<UID>> loading_model_URL_to_world_ob_UID_map;
	std::map<std::string, std::set<UID>> loading_model_URL_to_avatar_UID_map;

	std::vector<Reference<GLObject> > player_phys_debug_spheres;

	QImage webview_qimage;
	Timer time_since_last_webview_display;

	Reference<GLObject> mouseover_selected_gl_ob;

	QPoint last_gl_widget_mouse_move_pos;

	MeshDataLoadingProgress mesh_data_loading_progress;
	Reference<OpenGLMeshRenderData> cur_loading_mesh_data;
	std::string cur_loading_lod_model_url;
	WorldObjectRef cur_loading_voxel_ob;
	int cur_loading_voxel_subsample_factor;
	Reference<RayMesh> cur_loading_raymesh;
	int cur_loading_voxel_ob_lod_level;

	bool in_CEF_message_loop;
	bool should_close;

	Reference<glare::PoolAllocator> world_ob_pool_allocator;

	uint64 frame_num;
};
