/*=====================================================================
PlayerPhysics.h
---------------
Copyright Glare Technologies Limited 2016 -
File created by ClassTemplate on Mon Sep 23 15:14:04 2002
=====================================================================*/
#pragma once


//class Camera;
#include "../maths/Vec4f.h"
#include "../maths/vec3.h"
#include <vector>
//#include "agent.h"
class CameraController;
class PhysicsWorld;


/*=====================================================================
PlayerPhysics
-------------

=====================================================================*/
class PlayerPhysics
{
public:
	PlayerPhysics();
	~PlayerPhysics();

	//void preDestroy();//releases refs to agents

	void processMoveForwards(float factor, bool runpressed, CameraController& cam);
	void processStrafeRight(float factor, bool runpressed, CameraController& cam);
	void processMoveUp(float factor, bool runpressed, CameraController& cam);
	void processJump(CameraController& cam);

	void update(PhysicsWorld& physics_world, float dtime, Vec4f& campos_out);
	
private:
	Vec3f vel;
	Vec3f lastvel;

	Vec3f moveimpulse;
	Vec3f lastgroundnormal;

	//AGENTREF lastgroundagent;
	//Agent* lastgroundagent;

	float jumptimeremaining;
	bool onground;
};


