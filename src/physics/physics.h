#include "util.h"
#include "graphics/mesh.h"
#include "lib/vec/vec.h"
#include "lib/map/map.h"
#include <stdint.h>
#include <ode/ode.h>

#pragma once

#define MAX_CONTACTS 4
#define MAX_TAGS 16
#define NO_TAG ~0

typedef enum {
  SHAPE_SPHERE,
  SHAPE_BOX,
  SHAPE_CAPSULE,
  SHAPE_CYLINDER,
  SHAPE_MESH
} ShapeType;

typedef enum {
  JOINT_BALL,
  JOINT_HINGE,
  JOINT_SLIDER
} JointType;

typedef struct {
  Ref ref;
  dWorldID id;
  dSpaceID space;
  dJointGroupID contactGroup;
  vec_void_t overlaps;
  map_int_t tags;
  uint16_t masks[MAX_TAGS];
} World;

typedef struct {
  Ref ref;
  dBodyID body;
  World* world;
  void* userdata;
  int tag;
  vec_void_t shapes;
  vec_void_t joints;
  float friction;
  float restitution;
} Collider;

typedef struct {
  Ref ref;
  ShapeType type;
  dGeomID id;
  Collider* collider;
  void* userdata;
} Shape;

typedef Shape SphereShape;
typedef Shape BoxShape;
typedef Shape CapsuleShape;
typedef Shape CylinderShape;

typedef struct {
  Shape shape;
  dTriMeshDataID data;
  float* vertices;
  float* normals;
  unsigned int* indices;
} MeshShape;

typedef struct {
  Ref ref;
  JointType type;
  dJointID id;
  void* userdata;
} Joint;

typedef Joint BallJoint;
typedef Joint HingeJoint;
typedef Joint SliderJoint;

typedef void (*CollisionResolver)(World* world, void* userdata);
typedef void (*RaycastCallback)(Shape* shape, float x, float y, float z, float nx, float ny, float nz, void* userdata);

typedef struct {
  RaycastCallback callback;
  void* userdata;
} RaycastData;

void lovrPhysicsInit();
void lovrPhysicsDestroy();

World* lovrWorldCreate(float xg, float yg, float zg, int allowSleep, const char** tags, int tagCount);
void lovrWorldDestroy(const Ref* ref);
void lovrWorldDestroyData(World* world);
void lovrWorldUpdate(World* world, float dt, CollisionResolver resolver, void* userdata);
void lovrWorldComputeOverlaps(World* world);
int lovrWorldGetNextOverlap(World* world, Shape** a, Shape** b);
int lovrWorldCollide(World* world, Shape* a, Shape* b, float friction, float restitution);
void lovrWorldGetGravity(World* world, float* x, float* y, float* z);
void lovrWorldSetGravity(World* world, float x, float y, float z);
void lovrWorldGetLinearDamping(World* world, float* damping, float* threshold);
void lovrWorldSetLinearDamping(World* world, float damping, float threshold);
void lovrWorldGetAngularDamping(World* world, float* damping, float* threshold);
void lovrWorldSetAngularDamping(World* world, float damping, float threshold);
int lovrWorldIsSleepingAllowed(World* world);
void lovrWorldSetSleepingAllowed(World* world, int allowed);
void lovrWorldRaycast(World* world, float x1, float y1, float z1, float x2, float y2, float z2, RaycastCallback callback, void* userdata);
const char* lovrWorldGetTagName(World* world, int tag);
int lovrWorldDisableCollisionBetween(World* world, const char* tag1, const char* tag2);
int lovrWorldEnableCollisionBetween(World* world, const char* tag1, const char* tag2);
int lovrWorldIsCollisionEnabledBetween(World* world, const char* tag1, const char* tag);

Collider* lovrColliderCreate();
void lovrColliderDestroy(const Ref* ref);
void lovrColliderDestroyData(Collider* collider);
World* lovrColliderGetWorld(Collider* collider);
void lovrColliderAddShape(Collider* collider, Shape* shape);
void lovrColliderRemoveShape(Collider* collider, Shape* shape);
vec_void_t* lovrColliderGetShapes(Collider* collider);
vec_void_t* lovrColliderGetJoints(Collider* collider);
void* lovrColliderGetUserData(Collider* collider);
void lovrColliderSetUserData(Collider* collider, void* data);
int lovrColliderIsKinematic(Collider* collider);
void lovrColliderSetKinematic(Collider* collider, int kinematic);
int lovrColliderIsGravityIgnored(Collider* collider);
void lovrColliderSetGravityIgnored(Collider* collider, int ignored);
int lovrColliderIsSleepingAllowed(Collider* collider);
void lovrColliderSetSleepingAllowed(Collider* collider, int allowed);
int lovrColliderIsAwake(Collider* collider);
void lovrColliderSetAwake(Collider* collider, int awake);
float lovrColliderGetMass(Collider* collider);
void lovrColliderSetMass(Collider* collider, float mass);
void lovrColliderGetMassData(Collider* collider, float* cx, float* cy, float* cz, float* mass, float inertia[6]);
void lovrColliderSetMassData(Collider* collider, float cx, float cy, float cz, float mass, float inertia[6]);
void lovrColliderGetPosition(Collider* collider, float* x, float* y, float* z);
void lovrColliderSetPosition(Collider* collider, float x, float y, float z);
void lovrColliderGetOrientation(Collider* collider, float* angle, float* x, float* y, float* z);
void lovrColliderSetOrientation(Collider* collider, float angle, float x, float y, float z);
void lovrColliderGetLinearVelocity(Collider* collider, float* x, float* y, float* z);
void lovrColliderSetLinearVelocity(Collider* collider, float x, float y, float z);
void lovrColliderGetAngularVelocity(Collider* collider, float* x, float* y, float* z);
void lovrColliderSetAngularVelocity(Collider* collider, float x, float y, float z);
void lovrColliderGetLinearDamping(Collider* collider, float* damping, float* threshold);
void lovrColliderSetLinearDamping(Collider* collider, float damping, float threshold);
void lovrColliderGetAngularDamping(Collider* collider, float* damping, float* threshold);
void lovrColliderSetAngularDamping(Collider* collider, float damping, float threshold);
void lovrColliderApplyForce(Collider* collider, float x, float y, float z);
void lovrColliderApplyForceAtPosition(Collider* collider, float x, float y, float z, float cx, float cy, float cz);
void lovrColliderApplyTorque(Collider* collider, float x, float y, float z);
void lovrColliderGetLocalCenter(Collider* collider, float* x, float* y, float* z);
void lovrColliderGetLocalPoint(Collider* collider, float wx, float wy, float wz, float* x, float* y, float* z);
void lovrColliderGetWorldPoint(Collider* collider, float x, float y, float z, float* wx, float* wy, float* wz);
void lovrColliderGetLocalVector(Collider* collider, float wx, float wy, float wz, float* x, float* y, float* z);
void lovrColliderGetWorldVector(Collider* collider, float x, float y, float z, float* wx, float* wy, float* wz);
void lovrColliderGetLinearVelocityFromLocalPoint(Collider* collider, float x, float y, float z, float* vx, float* vy, float* vz);
void lovrColliderGetLinearVelocityFromWorldPoint(Collider* collider, float wx, float wy, float wz, float* vx, float* vy, float* vz);
void lovrColliderGetAABB(Collider* collider, float aabb[6]);
float lovrColliderGetFriction(Collider* collider);
void lovrColliderSetFriction(Collider* collider, float friction);
float lovrColliderGetRestitution(Collider* collider);
void lovrColliderSetRestitution(Collider* collider, float restitution);
const char* lovrColliderGetTag(Collider* collider);
int lovrColliderSetTag(Collider* collider, const char* tag);

void lovrShapeDestroy(const Ref* ref);
void lovrShapeDestroyData(Shape* shape);
ShapeType lovrShapeGetType(Shape* shape);
Collider* lovrShapeGetCollider(Shape* shape);
int lovrShapeIsEnabled(Shape* shape);
void lovrShapeSetEnabled(Shape* shape, int enabled);
void* lovrShapeGetUserData(Shape* shape);
void lovrShapeSetUserData(Shape* shape, void* data);
void lovrShapeGetPosition(Shape* shape, float* x, float* y, float* z);
void lovrShapeSetPosition(Shape* shape, float x, float y, float z);
void lovrShapeGetOrientation(Shape* shape, float* angle, float* x, float* y, float* z);
void lovrShapeSetOrientation(Shape* shape, float angle, float x, float y, float z);
void lovrShapeGetMass(Shape* shape, float density, float* cx, float* cy, float* cz, float* mass, float inertia[6]);
void lovrShapeGetAABB(Shape* shape, float aabb[6]);

SphereShape* lovrSphereShapeCreate(float radius);
float lovrSphereShapeGetRadius(SphereShape* sphere);
void lovrSphereShapeSetRadius(SphereShape* sphere, float radius);

BoxShape* lovrBoxShapeCreate(float x, float y, float z);
void lovrBoxShapeGetDimensions(BoxShape* box, float* x, float* y, float* z);
void lovrBoxShapeSetDimensions(BoxShape* box, float x, float y, float z);

CapsuleShape* lovrCapsuleShapeCreate(float radius, float length);
float lovrCapsuleShapeGetRadius(CapsuleShape* capsule);
void lovrCapsuleShapeSetRadius(CapsuleShape* capsule, float radius);
float lovrCapsuleShapeGetLength(CapsuleShape* capsule);
void lovrCapsuleShapeSetLength(CapsuleShape* capsule, float length);

CylinderShape* lovrCylinderShapeCreate(float radius, float length);
float lovrCylinderShapeGetRadius(CylinderShape* cylinder);
void lovrCylinderShapeSetRadius(CylinderShape* cylinder, float radius);
float lovrCylinderShapeGetLength(CylinderShape* cylinder);
void lovrCylinderShapeSetLength(CylinderShape* cylinder, float length);

MeshShape* lovrMeshShapeCreate(Mesh* mesh);
void lovrMeshShapeDestroy(const Ref* ref);

void lovrJointDestroy(const Ref* ref);
void lovrJointDestroyData(Joint* joint);
JointType lovrJointGetType(Joint* joint);
void lovrJointGetColliders(Joint* joint, Collider** a, Collider** b);
void* lovrJointGetUserData(Joint* joint);
void lovrJointSetUserData(Joint* joint, void* data);

BallJoint* lovrBallJointCreate(Collider* a, Collider* b, float x, float y, float z);
void lovrBallJointGetAnchors(BallJoint* ball, float* x1, float* y1, float* z1, float* x2, float* y2, float* z2);
void lovrBallJointSetAnchor(BallJoint* ball, float x, float y, float z);

HingeJoint* lovrHingeJointCreate(Collider* a, Collider* b, float x, float y, float z, float ax, float ay, float az);
void lovrHingeJointGetAnchors(HingeJoint* hinge, float* x1, float* y1, float* z1, float* x2, float* y2, float* z2);
void lovrHingeJointSetAnchor(HingeJoint* hinge, float x, float y, float z);
void lovrHingeJointGetAxis(HingeJoint* hinge, float* x, float* y, float* z);
void lovrHingeJointSetAxis(HingeJoint* hinge, float x, float y, float z);
float lovrHingeJointGetAngle(HingeJoint* hinge);
float lovrHingeJointGetLowerLimit(HingeJoint* hinge);
void lovrHingeJointSetLowerLimit(HingeJoint* hinge, float limit);
float lovrHingeJointGetUpperLimit(HingeJoint* hinge);
void lovrHingeJointSetUpperLimit(HingeJoint* hinge, float limit);

SliderJoint* lovrSliderJointCreate(Collider* a, Collider* b, float ax, float ay, float az);
void lovrSliderJointGetAxis(SliderJoint* slider, float* x, float* y, float* z);
void lovrSliderJointSetAxis(SliderJoint* slider, float x, float y, float z);
float lovrSliderJointGetPosition(SliderJoint* slider);
float lovrSliderJointGetLowerLimit(SliderJoint* slider);
void lovrSliderJointSetLowerLimit(SliderJoint* slider, float limit);
float lovrSliderJointGetUpperLimit(SliderJoint* slider);
void lovrSliderJointSetUpperLimit(SliderJoint* slider, float limit);
