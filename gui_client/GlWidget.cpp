/*=====================================================================
GlWidget.cpp
------------
Copyright Glare Technologies Limited 2018 -
=====================================================================*/
#include "GlWidget.h"


#include "PlayerPhysics.h"
#include "../dll/include/IndigoMesh.h"
#include "../indigo/TextureServer.h"
#include "../indigo/globals.h"
#include "../graphics/Map2D.h"
#include "../graphics/imformatdecoder.h"
#include "../graphics/ImageMap.h"
#include "../maths/vec3.h"
#include "../maths/GeometrySampling.h"
#include "../utils/Lock.h"
#include "../utils/Mutex.h"
#include "../utils/Clock.h"
#include "../utils/Timer.h"
#include "../utils/Platform.h"
#include "../utils/FileUtils.h"
#include "../utils/Reference.h"
#include "../utils/StringUtils.h"
#include "../utils/CameraController.h"
#include "../utils/TaskManager.h"
#include <QtGui/QMouseEvent>
#include <set>
#include <stack>
#include <algorithm>


// https://wiki.qt.io/How_to_use_OpenGL_Core_Profile_with_Qt
// https://developer.apple.com/opengl/capabilities/GLInfo_1085_Core.html
static QGLFormat makeFormat()
{
	// We need to request a 'core' profile.  Otherwise on OS X, we get an OpenGL 2.1 interface, whereas we require a v3+ interface.
	QGLFormat format;
	// We need to request version 3.2 (or above?) on OS X, otherwise we get legacy version 2.
#ifdef OSX
	format.setVersion(3, 2);
#endif
	format.setProfile(QGLFormat::CoreProfile);
	format.setSampleBuffers(true);
	return format;
}


GlWidget::GlWidget(QWidget *parent)
:	QGLWidget(makeFormat(), parent),
	cam_controller(NULL),
	current_time(0.f)
{
	viewport_aspect_ratio = 1;

	OpenGLEngineSettings settings;
	settings.shadow_mapping = true;
	settings.compress_textures = true;
	opengl_engine = new OpenGLEngine(settings);

	SHIFT_down = false;
	W_down = false;
	A_down = false;
	S_down = false;
	D_down = false;

	viewport_w = viewport_h = 100;

	// Needed to get keyboard events.
	setFocusPolicy(Qt::StrongFocus);
}


GlWidget::~GlWidget()
{
	opengl_engine = NULL;
}


void GlWidget::setCameraController(CameraController* cam_controller_)
{
	cam_controller = cam_controller_;
}


void GlWidget::setPlayerPhysics(PlayerPhysics* player_physics_)
{
	player_physics = player_physics_;
}


void GlWidget::resizeGL(int width_, int height_)
{
	viewport_w = width_;
	viewport_h = height_;

	glViewport(0, 0, width_, height_);

	viewport_aspect_ratio = (double)width_ / (double)height_;

	this->opengl_engine->viewportChanged(viewport_w, viewport_h);
}


void GlWidget::initializeGL()
{
	assert(this->texture_server_ptr);

	opengl_engine->initialise(
		//"o:/indigo/trunk/opengl", // data dir
		base_dir_path + "/data", // data dir (should contain 'shaders' and 'gl_data')
		this->texture_server_ptr
	);
	if(!opengl_engine->initSucceeded())
	{
		conPrint("opengl_engine init failed: " + opengl_engine->getInitialisationErrorMsg());
	}
}


void GlWidget::paintGL()
{
	if(cam_controller)
	{
		// Work out current camera transform
		Vec3d cam_pos, up, forwards, right;
		cam_pos = cam_controller->getPosition();
		cam_controller->getBasis(right, up, forwards);

		const Matrix4f rot = Matrix4f(right.toVec4fVector(), forwards.toVec4fVector(), up.toVec4fVector(), Vec4f(0,0,0,1)).getTranspose();
		
		Matrix4f world_to_camera_space_matrix;
		rot.rightMultiplyAffine3WithTranslationMatrix(-cam_pos.toVec4fVector(), /*result=*/world_to_camera_space_matrix);

		const float sensor_width = sensorWidth();
		const float lens_sensor_dist = lensSensorDist();
		const float render_aspect_ratio = viewport_aspect_ratio;
		opengl_engine->setViewportAspectRatio(viewport_aspect_ratio, viewport_w, viewport_h);
		opengl_engine->setMaxDrawDistance(1000.f);
		opengl_engine->setPerspectiveCameraTransform(world_to_camera_space_matrix, sensor_width, lens_sensor_dist, render_aspect_ratio, /*lens shift up=*/0.f, /*lens shift right=*/0.f);
		opengl_engine->setCurrentTime(current_time);
		opengl_engine->draw();
	}
	
	//conPrint("FPS: " + doubleToStringNSigFigs(1 / timer.elapsed(), 1));
	//timer.reset();
}


void GlWidget::addObject(const Reference<GLObject>& object)
{
	this->makeCurrent();

	opengl_engine->addObject(object);
}


void GlWidget::removeObject(const Reference<GLObject>& object)
{
	this->makeCurrent();

	opengl_engine->removeObject(object);
}


void GlWidget::addOverlayObject(const Reference<OverlayObject>& object)
{
	this->makeCurrent();

	opengl_engine->addOverlayObject(object);
}


void GlWidget::setEnvMat(OpenGLMaterial& mat)
{
	this->makeCurrent();

	opengl_engine->setEnvMat(mat);
}


void GlWidget::keyPressEvent(QKeyEvent* e)
{
	if(this->player_physics)
	{
		SHIFT_down = (e->modifiers() & Qt::ShiftModifier);

		if(e->key() == Qt::Key::Key_Space)
		{
			this->player_physics->processJump(*this->cam_controller);
		}
		if(e->key() == Qt::Key::Key_W)
		{
			W_down = true;
		}
		if(e->key() == Qt::Key::Key_S)
		{
			S_down = true;
		}
		if(e->key() == Qt::Key::Key_A)
		{
			A_down = true;
		}
		if(e->key() == Qt::Key::Key_D)
		{
			D_down = true;
		}
	}

	emit keyPressed(e);
}


void GlWidget::keyReleaseEvent(QKeyEvent* e)
{
	if(this->player_physics)
	{
		SHIFT_down = (e->modifiers() & Qt::ShiftModifier);

		if(e->key() == Qt::Key::Key_W)
		{
			W_down = false;
		}
		if(e->key() == Qt::Key::Key_S)
		{
			S_down = false;
		}
		if(e->key() == Qt::Key::Key_A)
		{
			A_down = false;
		}
		if(e->key() == Qt::Key::Key_D)
		{
			D_down = false;
		}
	}

	emit keyReleased(e);
}


void GlWidget::playerPhyicsThink(float dt)
{
	// On Windows we will use GetAsyncKeyState() to test if a key is down.
	// On Mac OS / Linux we will use our W_down etc.. state.
	// This isn't as good because if we miss the keyReleaseEvent due to not having focus when the key is released, the key will act as if it's stuck down.
	// TODO: Find an equivalent solution to GetAsyncKeyState on Mac/Linux.
#ifdef _WIN32
	if(hasFocus())
	{
		SHIFT_down = GetAsyncKeyState(VK_SHIFT);

		if(GetAsyncKeyState('W') || GetAsyncKeyState(VK_UP))
			this->player_physics->processMoveForwards(1.f, SHIFT_down, *this->cam_controller);
		if(GetAsyncKeyState('S') || GetAsyncKeyState(VK_DOWN))
			this->player_physics->processMoveForwards(-1.f, SHIFT_down, *this->cam_controller);
		if(GetAsyncKeyState('A'))
			this->player_physics->processStrafeRight(-1.f, SHIFT_down, *this->cam_controller);
		if(GetAsyncKeyState('D'))
			this->player_physics->processStrafeRight(1.f, SHIFT_down, *this->cam_controller);

		// Move vertically up or down in flymode.
		if(GetAsyncKeyState(' '))
			this->player_physics->processMoveUp(1.f, SHIFT_down, *this->cam_controller);
		if(GetAsyncKeyState('C'))
			this->player_physics->processMoveUp(-1.f, SHIFT_down, *this->cam_controller);

		// Turn left or right
		const float base_rotate_speed = 200;
		if(GetAsyncKeyState(VK_LEFT))
			this->cam_controller->update(/*pos delta=*/Vec3d(0.0), Vec2d(0, dt * -base_rotate_speed * (SHIFT_down ? 3.0 : 1.0)));
		if(GetAsyncKeyState(VK_RIGHT))
			this->cam_controller->update(/*pos delta=*/Vec3d(0.0), Vec2d(0, dt *  base_rotate_speed * (SHIFT_down ? 3.0 : 1.0)));
	}
#else
	if(W_down)
		this->player_physics->processMoveForwards(1.f, SHIFT_down, *this->cam_controller);
	if(S_down)
		this->player_physics->processMoveForwards(-1.f, SHIFT_down, *this->cam_controller);
	if(A_down)
		this->player_physics->processStrafeRight(-1.f, SHIFT_down, *this->cam_controller);
	if(D_down)
		this->player_physics->processStrafeRight(1.f, SHIFT_down, *this->cam_controller);
#endif
}


void GlWidget::mousePressEvent(QMouseEvent* e)
{
	//conPrint("mousePressEvent at " + toString(QCursor::pos().x()) + ", " + toString(QCursor::pos().y()));
	mouse_move_origin = QCursor::pos();
	last_mouse_press_pos = QCursor::pos();
}


void GlWidget::mouseReleaseEvent(QMouseEvent* e)
{
	//conPrint("mouseReleaseEvent at " + toString(QCursor::pos().x()) + ", " + toString(QCursor::pos().y()));

	//if((QCursor::pos() - last_mouse_press_pos).manhattanLength() < 4)
	{
		//conPrint("Click at " + toString(QCursor::pos().x()) + ", " + toString(QCursor::pos().y()));
		//conPrint("Click at " + toString(e->pos().x()) + ", " + toString(e->pos().y()));

		emit mouseClicked(e);
	}
}


void GlWidget::showEvent(QShowEvent* e)
{
	emit widgetShowSignal();
}


void GlWidget::mouseMoveEvent(QMouseEvent* e)
{
	if(cam_controller != NULL)// && (e->modifiers() & Qt::AltModifier))
	{
		Qt::MouseButtons mb = e->buttons();
		//double shift_scale = ((e->modifiers() & Qt::ShiftModifier) == 0) ? 1.0 : 0.35; // If shift is held, movement speed is roughly 1/3

		// Get new mouse position, movement vector and set previous mouse position to new.
		QPoint new_pos = QCursor::pos();
		QPoint delta(new_pos.x() - mouse_move_origin.x(),
					 mouse_move_origin.y() - new_pos.y()); // Y+ is down in screenspace, not up as desired.

		if(mb & Qt::LeftButton)  cam_controller->update(Vec3d(0, 0, 0), Vec2d(delta.y(), delta.x())/* * shift_scale*/);
		//if(mb & Qt::MidButton)   cam_controller->update(Vec3d(delta.x(), 0, delta.y()) * shift_scale, Vec2d(0, 0));
		//if(mb & Qt::RightButton) cam_controller->update(Vec3d(0, delta.y(), 0) * shift_scale, Vec2d(0, 0));

		if(mb & Qt::RightButton || mb & Qt::LeftButton || mb & Qt::MidButton)
			emit cameraUpdated();

		mouse_move_origin = new_pos;

		emit mouseMoved(e);
		
		//conPrint("mouseMoveEvent FPS: " + doubleToStringNSigFigs(1 / timer.elapsed(), 1));
		//timer.reset();
	}
	
	QGLWidget::mouseMoveEvent(e);
	
	//conPrint("mouseMoveEvent time since last event: " + doubleToStringNSigFigs(timer.elapsed(), 5));
	//timer.reset();
}


void GlWidget::wheelEvent(QWheelEvent* e)
{
	emit mouseWheelSignal(e);
}


void GlWidget::mouseDoubleClickEvent(QMouseEvent* e)
{
	emit mouseDoubleClickedSignal(e);
}
