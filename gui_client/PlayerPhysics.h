/*=====================================================================
PlayerPhysics.h
---------------
Copyright Glare Technologies Limited 2021 -
File created by ClassTemplate on Mon Sep 23 15:14:04 2002
=====================================================================*/
#pragma once


#include "../physics/jscol_boundingsphere.h"
#include "../maths/Vec4f.h"
#include "../maths/vec3.h"
#include <vector>
class CameraController;
class PhysicsWorld;
class ThreadContext;


struct SpringSphereSet
{
	std::vector<Vec4f> collpoints;
	js::BoundingSphere sphere;
};

struct UpdateEvents
{
	UpdateEvents() : jumped(false) {}
	bool jumped;
};


/*=====================================================================
PlayerPhysics
-------------

=====================================================================*/
class PlayerPhysics
{
public:
	PlayerPhysics();
	~PlayerPhysics();

	void processMoveForwards(float factor, bool runpressed, CameraController& cam); // factor should be -1 for move backwards, 1 otherwise.
	void processStrafeRight(float factor, bool runpressed, CameraController& cam);
	void processMoveUp(float factor, bool runpressed, CameraController& cam);
	void processJump(CameraController& cam);

	UpdateEvents update(PhysicsWorld& physics_world, float dtime, ThreadContext& thread_context, Vec4f& campos_in_out);

	void setFlyModeEnabled(bool enabled);
	bool flyModeEnabled() const { return flymode; }
	
	bool onGroundRecently() const { return time_since_on_ground < 0.02f; }
	bool isRunPressed() const { return last_runpressed; }
private:
	Vec3f vel;
	Vec3f lastvel;

	Vec3f moveimpulse;
	Vec3f lastgroundnormal;

	//AGENTREF lastgroundagent;
	//Agent* lastgroundagent;

	float jumptimeremaining;
	bool onground;
	bool last_runpressed;

	bool flymode;

	float time_since_on_ground;
	

	std::vector<SpringSphereSet> springspheresets;
};
