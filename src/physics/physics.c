#include "physics.h"
#include "math/quat.h"
#include <stdlib.h>

static void defaultNearCallback(void* data, dGeomID a, dGeomID b) {
  lovrWorldCollide((World*) data, dGeomGetData(a), dGeomGetData(b), -1, -1);
}

static void customNearCallback(void* data, dGeomID shapeA, dGeomID shapeB) {
  World* world = data;
  vec_push(&world->overlaps, dGeomGetData(shapeA));
  vec_push(&world->overlaps, dGeomGetData(shapeB));
}

static void raycastCallback(void* data, dGeomID a, dGeomID b) {
  RaycastCallback callback = ((RaycastData*) data)->callback;
  void* userdata = ((RaycastData*) data)->userdata;
  Shape* shape = dGeomGetData(b);

  if (!shape) {
    return;
  }

  dContact contact;
  if (dCollide(a, b, MAX_CONTACTS, &contact.geom, sizeof(dContact))) {
    dContactGeom g = contact.geom;
    callback(shape, g.pos[0], g.pos[1], g.pos[2], g.normal[0], g.normal[1], g.normal[2], userdata);
  }
}

void lovrPhysicsInit() {
  dInitODE();
  atexit(lovrPhysicsDestroy);
}

void lovrPhysicsDestroy() {
  dCloseODE();
}

World* lovrWorldCreate(float xg, float yg, float zg, int allowSleep, const char** tags, int tagCount) {
  World* world = lovrAlloc(sizeof(World), lovrWorldDestroy);
  if (!world) return NULL;

  world->id = dWorldCreate();
  world->space = dHashSpaceCreate(0);
  dHashSpaceSetLevels(world->space, -4, 8);
  world->contactGroup = dJointGroupCreate(0);
  vec_init(&world->overlaps);
  lovrWorldSetGravity(world, xg, yg, zg);
  lovrWorldSetSleepingAllowed(world, allowSleep);
  map_init(&world->tags);
  for (int i = 0; i < tagCount; i++) {
    map_set(&world->tags, tags[i], i);
  }

  for (int i = 0; i < MAX_TAGS; i++) {
    world->masks[i] = ~0;
  }

  return world;
}

void lovrWorldDestroy(const Ref* ref) {
  World* world = containerof(ref, World);
  lovrWorldDestroyData(world);
  vec_deinit(&world->overlaps);
  free(world);
}

void lovrWorldDestroyData(World* world) {
  if (world->contactGroup) {
    dJointGroupEmpty(world->contactGroup);
    world->contactGroup = NULL;
  }

  if (world->space) {
    dSpaceDestroy(world->space);
    world->space = NULL;
  }

  if (world->id) {
    dWorldDestroy(world->id);
    world->id = NULL;
  }
}

void lovrWorldUpdate(World* world, float dt, CollisionResolver resolver, void* userdata) {
  if (resolver) {
    resolver(world, userdata);
  } else {
    dSpaceCollide(world->space, world, defaultNearCallback);
  }

  dWorldQuickStep(world->id, dt);
  dJointGroupEmpty(world->contactGroup);
}

void lovrWorldComputeOverlaps(World* world) {
  vec_clear(&world->overlaps);
  dSpaceCollide(world->space, world, customNearCallback);
}

int lovrWorldGetNextOverlap(World* world, Shape** a, Shape** b) {
  if (world->overlaps.length == 0) {
    *a = *b = NULL;
    return 0;
  }

  *a = vec_pop(&world->overlaps);
  *b = vec_pop(&world->overlaps);
  return 1;
}

int lovrWorldCollide(World* world, Shape* a, Shape* b, float friction, float restitution) {
  if (!a || !b) {
    return 0;
  }

  Collider* colliderA = a->collider;
  Collider* colliderB = b->collider;
  int tag1 = colliderA->tag;
  int tag2 = colliderB->tag;

  if (tag1 != NO_TAG && tag2 != NO_TAG && !((world->masks[tag1] & (1 << tag2)) && (world->masks[tag2] & (1 << tag1)))) {
    return 0;
  }

  if (friction < 0) {
    friction = sqrt(colliderA->friction * colliderB->friction);
  }

  if (restitution < 0) {
    restitution = MAX(colliderA->restitution, colliderB->restitution);
  }

  dContact contacts[MAX_CONTACTS];
  for (int i = 0; i < MAX_CONTACTS; i++) {
    contacts[i].surface.mode = 0;
    contacts[i].surface.mu = friction;
    contacts[i].surface.bounce = restitution;
    contacts[i].surface.mu = dInfinity;

    if (restitution > 0) {
      contacts[i].surface.mode |= dContactBounce;
    }
  }

  int contactCount = dCollide(a->id, b->id, MAX_CONTACTS, &contacts[0].geom, sizeof(dContact));

  for (int i = 0; i < contactCount; i++) {
    dJointID joint = dJointCreateContact(world->id, world->contactGroup, &contacts[i]);
    dJointAttach(joint, colliderA->body, colliderB->body);
  }

  return contactCount;
}

void lovrWorldGetGravity(World* world, float* x, float* y, float* z) {
  dReal gravity[3];
  dWorldGetGravity(world->id, gravity);
  *x = gravity[0];
  *y = gravity[1];
  *z = gravity[2];
}

void lovrWorldSetGravity(World* world, float x, float y, float z) {
  dWorldSetGravity(world->id, x, y, z);
}

void lovrWorldGetLinearDamping(World* world, float* damping, float* threshold) {
  *damping = dWorldGetLinearDamping(world->id);
  *threshold = dWorldGetLinearDampingThreshold(world->id);
}

void lovrWorldSetLinearDamping(World* world, float damping, float threshold) {
  dWorldSetLinearDamping(world->id, damping);
  dWorldSetLinearDampingThreshold(world->id, threshold);
}

void lovrWorldGetAngularDamping(World* world, float* damping, float* threshold) {
  *damping = dWorldGetAngularDamping(world->id);
  *threshold = dWorldGetAngularDampingThreshold(world->id);
}

void lovrWorldSetAngularDamping(World* world, float damping, float threshold) {
  dWorldSetAngularDamping(world->id, damping);
  dWorldSetAngularDampingThreshold(world->id, threshold);
}

int lovrWorldIsSleepingAllowed(World* world) {
  return dWorldGetAutoDisableFlag(world->id);
}

void lovrWorldSetSleepingAllowed(World* world, int allowed) {
  dWorldSetAutoDisableFlag(world->id, allowed);
}

void lovrWorldRaycast(World* world, float x1, float y1, float z1, float x2, float y2, float z2, RaycastCallback callback, void* userdata) {
  RaycastData data = { .callback = callback, .userdata = userdata };
  float dx = x2 - x1;
  float dy = y2 - y1;
  float dz = z2 - z1;
  float length = sqrt(dx * dx + dy * dy + dz * dz);
  dGeomID ray = dCreateRay(world->space, length);
  dGeomRaySet(ray, x1, y1, z1, dx, dy, dz);
  dSpaceCollide2(ray, (dGeomID) world->space, &data, raycastCallback);
  dGeomDestroy(ray);
}

const char* lovrWorldGetTagName(World* world, int tag) {
  if (tag == NO_TAG) {
    return NULL;
  }

  const char* key;
  map_iter_t iter = map_iter(&world->tags);
  while ((key = map_next(&world->tags, &iter))) {
    if (*map_get(&world->tags, key) == tag) {
      return key;
    }
  }

  return NULL;
}

int lovrWorldDisableCollisionBetween(World* world, const char* tag1, const char* tag2) {
  int* index1 = map_get(&world->tags, tag1);
  int* index2 = map_get(&world->tags, tag2);
  if (!index1 || !index2) {
    return NO_TAG;
  }

  world->masks[*index1] &= ~(1 << *index2);
  world->masks[*index2] &= ~(1 << *index1);
  return 0;
}

int lovrWorldEnableCollisionBetween(World* world, const char* tag1, const char* tag2) {
  int* index1 = map_get(&world->tags, tag1);
  int* index2 = map_get(&world->tags, tag2);
  if (!index1 || !index2) {
    return NO_TAG;
  }

  world->masks[*index1] |= (1 << *index2);
  world->masks[*index2] |= (1 << *index1);
  return 0;
}

int lovrWorldIsCollisionEnabledBetween(World* world, const char* tag1, const char* tag2) {
  int* index1 = map_get(&world->tags, tag1);
  int* index2 = map_get(&world->tags, tag2);
  if (!index1 || !index2) {
    return NO_TAG;
  }

  return (world->masks[*index1] & (1 << *index2)) && (world->masks[*index2] & (1 << *index1));
}

Collider* lovrColliderCreate(World* world) {
  if (!world) {
    error("No world specified");
  }

  Collider* collider = lovrAlloc(sizeof(Collider), lovrColliderDestroy);
  if (!collider) return NULL;

  collider->body = dBodyCreate(world->id);
  collider->world = world;
  collider->friction = 0;
  collider->restitution = 0;
  collider->tag = NO_TAG;
  dBodySetData(collider->body, collider);
  vec_init(&collider->shapes);
  vec_init(&collider->joints);

  return collider;
}

void lovrColliderDestroy(const Ref* ref) {
  Collider* collider = containerof(ref, Collider);
  vec_deinit(&collider->shapes);
  vec_deinit(&collider->joints);
  lovrColliderDestroyData(collider);
  free(collider);
}

void lovrColliderDestroyData(Collider* collider) {
  if (collider->body) {
    dBodyDestroy(collider->body);
    collider->body = NULL;
  }
}

World* lovrColliderGetWorld(Collider* collider) {
  return collider->world;
}

void lovrColliderAddShape(Collider* collider, Shape* shape) {
  shape->collider = collider;
  dGeomSetBody(shape->id, collider->body);

  dSpaceID oldSpace = dGeomGetSpace(shape->id);
  dSpaceID newSpace = collider->world->space;

  if (oldSpace && oldSpace != newSpace) {
    dSpaceRemove(oldSpace, shape->id);
  }

  dSpaceAdd(newSpace, shape->id);
}

void lovrColliderRemoveShape(Collider* collider, Shape* shape) {
  if (shape->collider == collider) {
    dSpaceRemove(collider->world->space, shape->id);
    dGeomSetBody(shape->id, 0);
  }
}

vec_void_t* lovrColliderGetShapes(Collider* collider) {
  vec_clear(&collider->shapes);
  for (dGeomID geom = dBodyGetFirstGeom(collider->body); geom; geom = dBodyGetNextGeom(geom)) {
    Shape* shape = dGeomGetData(geom);
    if (shape) {
      vec_push(&collider->shapes, shape);
    }
  }
  return &collider->shapes;
}

vec_void_t* lovrColliderGetJoints(Collider* collider) {
  vec_clear(&collider->joints);
  int jointCount = dBodyGetNumJoints(collider->body);
  for (int i = 0; i < jointCount; i++) {
    Joint* joint = dJointGetData(dBodyGetJoint(collider->body, i));
    if (joint) {
      vec_push(&collider->joints, joint);
    }
  }
  return &collider->joints;
}

void* lovrColliderGetUserData(Collider* collider) {
  return collider->userdata;
}

void lovrColliderSetUserData(Collider* collider, void* data) {
  collider->userdata = data;
}

int lovrColliderIsKinematic(Collider* collider) {
  return dBodyIsKinematic(collider->body);
}

void lovrColliderSetKinematic(Collider* collider, int kinematic) {
  if (kinematic) {
    dBodySetKinematic(collider->body);
  } else {
    dBodySetDynamic(collider->body);
  }
}

int lovrColliderIsGravityIgnored(Collider* collider) {
  return !dBodyGetGravityMode(collider->body);
}

void lovrColliderSetGravityIgnored(Collider* collider, int ignored) {
  dBodySetGravityMode(collider->body, !ignored);
}

int lovrColliderIsSleepingAllowed(Collider* collider) {
  return dBodyGetAutoDisableFlag(collider->body);
}

void lovrColliderSetSleepingAllowed(Collider* collider, int allowed) {
  dBodySetAutoDisableFlag(collider->body, allowed);
}

int lovrColliderIsAwake(Collider* collider) {
  return dBodyIsEnabled(collider->body);
}

void lovrColliderSetAwake(Collider* collider, int awake) {
  if (awake) {
    dBodyEnable(collider->body);
  } else {
    dBodyDisable(collider->body);
  }
}

float lovrColliderGetMass(Collider* collider) {
  dMass m;
  dBodyGetMass(collider->body, &m);
  return m.mass;
}

void lovrColliderSetMass(Collider* collider, float mass) {
  dMass m;
  dBodyGetMass(collider->body, &m);
  dMassAdjust(&m, mass);
  dBodySetMass(collider->body, &m);
}

void lovrColliderGetMassData(Collider* collider, float* cx, float* cy, float* cz, float* mass, float inertia[6]) {
  dMass m;
  dBodyGetMass(collider->body, &m);
  *cx = m.c[0];
  *cy = m.c[1];
  *cz = m.c[2];
  *mass = m.mass;

  // Diagonal
  inertia[0] = m.I[0];
  inertia[1] = m.I[5];
  inertia[2] = m.I[10];

  // Lower triangular
  inertia[3] = m.I[4];
  inertia[4] = m.I[8];
  inertia[5] = m.I[9];
}

void lovrColliderSetMassData(Collider* collider, float cx, float cy, float cz, float mass, float inertia[]) {
  dMass m;
  dBodyGetMass(collider->body, &m);
  dMassSetParameters(&m, mass, cx, cy, cz, inertia[0], inertia[1], inertia[2], inertia[3], inertia[4], inertia[5]);
  dBodySetMass(collider->body, &m);
}

void lovrColliderGetPosition(Collider* collider, float* x, float* y, float* z) {
  const dReal* position = dBodyGetPosition(collider->body);
  *x = position[0];
  *y = position[1];
  *z = position[2];
}

void lovrColliderSetPosition(Collider* collider, float x, float y, float z) {
  dBodySetPosition(collider->body, x, y, z);
}

void lovrColliderGetOrientation(Collider* collider, float* angle, float* x, float* y, float* z) {
  const dReal* q = dBodyGetQuaternion(collider->body);
  float quaternion[4] = { q[1], q[2], q[3], q[0] };
  quat_getAngleAxis(quaternion, angle, x, y, z);
}

void lovrColliderSetOrientation(Collider* collider, float angle, float x, float y, float z) {
  float quaternion[4];
  float axis[3] = { x, y, z };
  quat_fromAngleAxis(quaternion, angle, axis);
  float q[4] = { quaternion[3], quaternion[0], quaternion[1], quaternion[2] };
  dBodySetQuaternion(collider->body, q);
}

void lovrColliderGetLinearVelocity(Collider* collider, float* x, float* y, float* z) {
  const dReal* velocity = dBodyGetLinearVel(collider->body);
  *x = velocity[0];
  *y = velocity[1];
  *z = velocity[2];
}

void lovrColliderSetLinearVelocity(Collider* collider, float x, float y, float z) {
  dBodySetLinearVel(collider->body, x, y, z);
}

void lovrColliderGetAngularVelocity(Collider* collider, float* x, float* y, float* z) {
  const dReal* velocity = dBodyGetAngularVel(collider->body);
  *x = velocity[0];
  *y = velocity[1];
  *z = velocity[2];
}

void lovrColliderSetAngularVelocity(Collider* collider, float x, float y, float z) {
  dBodySetAngularVel(collider->body, x, y, z);
}

void lovrColliderGetLinearDamping(Collider* collider, float* damping, float* threshold) {
  *damping = dBodyGetLinearDamping(collider->body);
  *threshold = dBodyGetLinearDampingThreshold(collider->body);
}

void lovrColliderSetLinearDamping(Collider* collider, float damping, float threshold) {
  dBodySetLinearDamping(collider->body, damping);
  dBodySetLinearDampingThreshold(collider->body, threshold);
}

void lovrColliderGetAngularDamping(Collider* collider, float* damping, float* threshold) {
  *damping = dBodyGetAngularDamping(collider->body);
  *threshold = dBodyGetAngularDampingThreshold(collider->body);
}

void lovrColliderSetAngularDamping(Collider* collider, float damping, float threshold) {
  dBodySetAngularDamping(collider->body, damping);
  dBodySetAngularDampingThreshold(collider->body, threshold);
}

void lovrColliderApplyForce(Collider* collider, float x, float y, float z) {
  dBodyAddForce(collider->body, x, y, z);
}

void lovrColliderApplyForceAtPosition(Collider* collider, float x, float y, float z, float cx, float cy, float cz) {
  dBodyAddForceAtPos(collider->body, x, y, z, cx, cy, cz);
}

void lovrColliderApplyTorque(Collider* collider, float x, float y, float z) {
  dBodyAddTorque(collider->body, x, y, z);
}

void lovrColliderGetLocalCenter(Collider* collider, float* x, float* y, float* z) {
  dMass m;
  dBodyGetMass(collider->body, &m);
  *x = m.c[0];
  *y = m.c[1];
  *z = m.c[2];
}

void lovrColliderGetLocalPoint(Collider* collider, float wx, float wy, float wz, float* x, float* y, float* z) {
  dReal local[3];
  dBodyGetPosRelPoint(collider->body, wx, wy, wz, local);
  *x = local[0];
  *y = local[1];
  *z = local[2];
}

void lovrColliderGetWorldPoint(Collider* collider, float x, float y, float z, float* wx, float* wy, float* wz) {
  dReal world[3];
  dBodyGetRelPointPos(collider->body, x, y, z, world);
  *wx = world[0];
  *wy = world[1];
  *wz = world[2];
}

void lovrColliderGetLocalVector(Collider* collider, float wx, float wy, float wz, float* x, float* y, float* z) {
  dReal local[3];
  dBodyVectorFromWorld(collider->body, wx, wy, wz, local);
  *x = local[0];
  *y = local[1];
  *z = local[2];
}

void lovrColliderGetWorldVector(Collider* collider, float x, float y, float z, float* wx, float* wy, float* wz) {
  dReal world[3];
  dBodyVectorToWorld(collider->body, x, y, z, world);
  *wx = world[0];
  *wy = world[1];
  *wz = world[2];
}

void lovrColliderGetLinearVelocityFromLocalPoint(Collider* collider, float x, float y, float z, float* vx, float* vy, float* vz) {
  dReal velocity[3];
  dBodyGetRelPointVel(collider->body, x, y, z, velocity);
  *vx = velocity[0];
  *vy = velocity[1];
  *vz = velocity[2];
}

void lovrColliderGetLinearVelocityFromWorldPoint(Collider* collider, float wx, float wy, float wz, float* vx, float* vy, float* vz) {
  dReal velocity[3];
  dBodyGetPointVel(collider->body, wx, wy, wz, velocity);
  *vx = velocity[0];
  *vy = velocity[1];
  *vz = velocity[2];
}

float lovrColliderGetFriction(Collider* collider) {
  return collider->friction;
}

void lovrColliderSetFriction(Collider* collider, float friction) {
  collider->friction = friction;
}

float lovrColliderGetRestitution(Collider* collider) {
  return collider->restitution;
}

void lovrColliderSetRestitution(Collider* collider, float restitution) {
  collider->restitution = restitution;
}

void lovrColliderGetAABB(Collider* collider, float aabb[6]) {
  dGeomID shape = dBodyGetFirstGeom(collider->body);

  if (!shape) {
    memset(aabb, 0, 6 * sizeof(float));
    return;
  }

  dGeomGetAABB(shape, aabb);

  float otherAABB[6];
  while ((shape = dBodyGetNextGeom(shape)) != NULL) {
    dGeomGetAABB(shape, otherAABB);
    aabb[0] = MIN(aabb[0], otherAABB[0]);
    aabb[1] = MAX(aabb[0], otherAABB[0]);
    aabb[2] = MIN(aabb[2], otherAABB[2]);
    aabb[3] = MAX(aabb[3], otherAABB[3]);
    aabb[4] = MIN(aabb[4], otherAABB[4]);
    aabb[5] = MAX(aabb[5], otherAABB[5]);
  }
}

const char* lovrColliderGetTag(Collider* collider) {
  return lovrWorldGetTagName(collider->world, collider->tag);
}

int lovrColliderSetTag(Collider* collider, const char* tag) {
  if (tag == NULL) {
    collider->tag = NO_TAG;
    return 0;
  }

  int* index = map_get(&collider->world->tags, tag);

  if (!index) {
    return NO_TAG;
  }

  collider->tag = *index;
  return 0;
}

void lovrShapeDestroy(const Ref* ref) {
  Shape* shape = containerof(ref, Shape);
  lovrShapeDestroyData(shape);
  free(shape);
}

void lovrShapeDestroyData(Shape* shape) {
  if (shape->id) {
    dGeomDestroy(shape->id);
    shape->id = NULL;
  }
}

ShapeType lovrShapeGetType(Shape* shape) {
  return shape->type;
}

Collider* lovrShapeGetCollider(Shape* shape) {
  return shape->collider;
}

int lovrShapeIsEnabled(Shape* shape) {
  return dGeomIsEnabled(shape->id);
}

void lovrShapeSetEnabled(Shape* shape, int enabled) {
  if (enabled) {
    dGeomEnable(shape->id);
  } else {
    dGeomDisable(shape->id);
  }
}

void* lovrShapeGetUserData(Shape* shape) {
  return shape->userdata;
}

void lovrShapeSetUserData(Shape* shape, void* data) {
  shape->userdata = data;
}

void lovrShapeGetPosition(Shape* shape, float* x, float* y, float* z) {
  const dReal* position = dGeomGetOffsetPosition(shape->id);
  *x = position[0];
  *y = position[1];
  *z = position[2];
}

void lovrShapeSetPosition(Shape* shape, float x, float y, float z) {
  dGeomSetOffsetPosition(shape->id, x, y, z);
}

void lovrShapeGetOrientation(Shape* shape, float* angle, float* x, float* y, float* z) {
  dReal q[4];
  dGeomGetOffsetQuaternion(shape->id, q);
  float quaternion[4] = { q[1], q[2], q[3], q[0] };
  quat_getAngleAxis(quaternion, angle, x, y, z);
}

void lovrShapeSetOrientation(Shape* shape, float angle, float x, float y, float z) {
  float quaternion[4];
  float axis[3] = { x, y, z };
  quat_fromAngleAxis(quaternion, angle, axis);
  float q[4] = { quaternion[3], quaternion[0], quaternion[1], quaternion[2] };
  dGeomSetOffsetQuaternion(shape->id, q);
}

void lovrShapeGetMass(Shape* shape, float density, float* cx, float* cy, float* cz, float* mass, float inertia[6]) {
  dMass m;
  dMassSetZero(&m);
  switch (shape->type) {
    case SHAPE_SPHERE: {
      dMassSetSphere(&m, density, dGeomSphereGetRadius(shape->id));
      break;
    }

    case SHAPE_BOX: {
      dReal lengths[3];
      dGeomBoxGetLengths(shape->id, lengths);
      dMassSetBox(&m, density, lengths[0], lengths[1], lengths[2]);
      break;
    }

    case SHAPE_CAPSULE: {
      dReal radius, length;
      dGeomCapsuleGetParams(shape->id, &radius, &length);
      dMassSetCapsule(&m, density, 3, radius, length);
      break;
    }

    case SHAPE_CYLINDER: {
      dReal radius, length;
      dGeomCylinderGetParams(shape->id, &radius, &length);
      dMassSetCylinder(&m, density, 3, radius, length);
      break;
    }

    case SHAPE_MESH: {
      dMassSetTrimesh(&m, density, shape->id);
    }
  }

  const dReal* position = dGeomGetOffsetPosition(shape->id);
  dMassTranslate(&m, position[0], position[1], position[2]);
  const dReal* rotation = dGeomGetOffsetRotation(shape->id);
  dMassRotate(&m, rotation);

  *cx = m.c[0];
  *cy = m.c[1];
  *cz = m.c[2];
  *mass = m.mass;

  // Diagonal
  inertia[0] = m.I[0];
  inertia[1] = m.I[5];
  inertia[2] = m.I[10];

  // Lower triangular
  inertia[3] = m.I[4];
  inertia[4] = m.I[8];
  inertia[5] = m.I[9];
}

void lovrShapeGetAABB(Shape* shape, float aabb[6]) {
  dGeomGetAABB(shape->id, aabb);
}

SphereShape* lovrSphereShapeCreate(float radius) {
  SphereShape* sphere = lovrAlloc(sizeof(SphereShape), lovrShapeDestroy);
  if (!sphere) return NULL;

  sphere->type = SHAPE_SPHERE;
  sphere->id = dCreateSphere(0, radius);
  dGeomSetData(sphere->id, sphere);

  return sphere;
}

float lovrSphereShapeGetRadius(SphereShape* sphere) {
  return dGeomSphereGetRadius(sphere->id);
}

void lovrSphereShapeSetRadius(SphereShape* sphere, float radius) {
  dGeomSphereSetRadius(sphere->id, radius);
}

BoxShape* lovrBoxShapeCreate(float x, float y, float z) {
  BoxShape* box = lovrAlloc(sizeof(BoxShape), lovrShapeDestroy);
  if (!box) return NULL;

  box->type = SHAPE_BOX;
  box->id = dCreateBox(0, x, y, z);
  dGeomSetData(box->id, box);

  return box;
}

void lovrBoxShapeGetDimensions(BoxShape* box, float* x, float* y, float* z) {
  float dimensions[3];
  dGeomBoxGetLengths(box->id, dimensions);
  *x = dimensions[0];
  *y = dimensions[1];
  *z = dimensions[2];
}

void lovrBoxShapeSetDimensions(BoxShape* box, float x, float y, float z) {
  dGeomBoxSetLengths(box->id, x, y, z);
}

CapsuleShape* lovrCapsuleShapeCreate(float radius, float length) {
  CapsuleShape* capsule = lovrAlloc(sizeof(CapsuleShape), lovrShapeDestroy);
  if (!capsule) return NULL;

  capsule->type = SHAPE_CAPSULE;
  capsule->id = dCreateCapsule(0, radius, length);
  dGeomSetData(capsule->id, capsule);

  return capsule;
}

float lovrCapsuleShapeGetRadius(CapsuleShape* capsule) {
  float radius, length;
  dGeomCapsuleGetParams(capsule->id, &radius, &length);
  return radius;
}

void lovrCapsuleShapeSetRadius(CapsuleShape* capsule, float radius) {
  dGeomCapsuleSetParams(capsule->id, radius, lovrCapsuleShapeGetLength(capsule));
}

float lovrCapsuleShapeGetLength(CapsuleShape* capsule) {
  float radius, length;
  dGeomCapsuleGetParams(capsule->id, &radius, &length);
  return length;
}

void lovrCapsuleShapeSetLength(CapsuleShape* capsule, float length) {
  dGeomCapsuleSetParams(capsule->id, lovrCapsuleShapeGetRadius(capsule), length);
}

CylinderShape* lovrCylinderShapeCreate(float radius, float length) {
  CylinderShape* cylinder = lovrAlloc(sizeof(CylinderShape), lovrShapeDestroy);
  if (!cylinder) return NULL;

  cylinder->type = SHAPE_CYLINDER;
  cylinder->id = dCreateCylinder(0, radius, length);
  dGeomSetData(cylinder->id, cylinder);

  return cylinder;
}

float lovrCylinderShapeGetRadius(CylinderShape* cylinder) {
  float radius, length;
  dGeomCylinderGetParams(cylinder->id, &radius, &length);
  return radius;
}

void lovrCylinderShapeSetRadius(CylinderShape* cylinder, float radius) {
  dGeomCylinderSetParams(cylinder->id, radius, lovrCylinderShapeGetLength(cylinder));
}

float lovrCylinderShapeGetLength(CylinderShape* cylinder) {
  float radius, length;
  dGeomCylinderGetParams(cylinder->id, &radius, &length);
  return length;
}

void lovrCylinderShapeSetLength(CylinderShape* cylinder, float length) {
  dGeomCylinderSetParams(cylinder->id, lovrCylinderShapeGetRadius(cylinder), length);
}

MeshShape* lovrMeshShapeCreate(Mesh* mesh) {
  MeshShape* shape = lovrAlloc(sizeof(MeshShape), lovrMeshShapeDestroy);
  if (!shape) return NULL;

  shape->data = dGeomTriMeshDataCreate();

  dGeomTriMeshDataBuildSingle1(shape->data, mesh->data, mesh->stride, mesh->count, mesh->map.data, mesh->map.length, 3 * sizeof(unsigned int), (char*) mesh->data + 3 * sizeof(float));

  shape->shape.type = SHAPE_MESH;
  shape->shape.id = dCreateTriMesh(0, mesh->data, NULL, NULL, NULL);
  dGeomSetData(shape->shape.id, shape);

  return shape;
}

void lovrMeshShapeDestroy(const Ref* ref) {
  Shape* shape = containerof(ref, Shape);
  MeshShape* mesh = (MeshShape*) shape;
  dGeomTriMeshDataDestroy(mesh->data);
  free(mesh->vertices);
  free(mesh->normals);
  free(mesh->indices);
  lovrShapeDestroy(&mesh->shape.ref);
}

void lovrJointDestroy(const Ref* ref) {
  Joint* joint = containerof(ref, Joint);
  lovrJointDestroyData(joint);
  free(joint);
}

void lovrJointDestroyData(Joint* joint) {
  if (joint->id) {
    dJointDestroy(joint->id);
    joint->id = NULL;
  }
}

JointType lovrJointGetType(Joint* joint) {
  return joint->type;
}

void lovrJointGetColliders(Joint* joint, Collider** a, Collider** b) {
  dBodyID bodyA = dJointGetBody(joint->id, 0);
  dBodyID bodyB = dJointGetBody(joint->id, 1);

  if (bodyA) {
    *a = dBodyGetData(bodyA);
  }

  if (bodyB) {
    *b = dBodyGetData(bodyB);
  }
}

void* lovrJointGetUserData(Joint* joint) {
  return joint->userdata;
}

void lovrJointSetUserData(Joint* joint, void* data) {
  joint->userdata = data;
}

BallJoint* lovrBallJointCreate(Collider* a, Collider* b, float x, float y, float z) {
  if (a->world != b->world) {
    error("Joint bodies must exist in same World");
  }

  BallJoint* joint = lovrAlloc(sizeof(BallJoint), lovrJointDestroy);
  if (!joint) return NULL;

  joint->type = JOINT_BALL;
  joint->id = dJointCreateBall(a->world->id, 0);
  dJointSetData(joint->id, joint);
  dJointAttach(joint->id, a->body, b->body);
  dJointSetBallAnchor(joint->id, x, y, z);

  return joint;
}

void lovrBallJointGetAnchors(BallJoint* ball, float* x1, float* y1, float* z1, float* x2, float* y2, float* z2) {
  float anchor[3];
  dJointGetBallAnchor(ball->id, anchor);
  *x1 = anchor[0];
  *y1 = anchor[1];
  *z1 = anchor[2];
  dJointGetBallAnchor2(ball->id, anchor);
  *x2 = anchor[0];
  *y2 = anchor[1];
  *z2 = anchor[2];
}

void lovrBallJointSetAnchor(BallJoint* ball, float x, float y, float z) {
  dJointSetBallAnchor(ball->id, x, y, z);
}

HingeJoint* lovrHingeJointCreate(Collider* a, Collider* b, float x, float y, float z, float ax, float ay, float az) {
  if (a->world != b->world) {
    error("Joint bodies must exist in same World");
  }

  HingeJoint* joint = lovrAlloc(sizeof(HingeJoint), lovrJointDestroy);
  if (!joint) return NULL;

  joint->type = JOINT_HINGE;
  joint->id = dJointCreateHinge(a->world->id, 0);
  dJointSetData(joint->id, joint);
  dJointAttach(joint->id, a->body, b->body);
  dJointSetHingeAnchor(joint->id, x, y, z);
  dJointSetHingeAxis(joint->id, ax, ay, az);

  return joint;
}

void lovrHingeJointGetAnchors(HingeJoint* hinge, float* x1, float* y1, float* z1, float* x2, float* y2, float* z2) {
  float anchor[3];
  dJointGetHingeAnchor(hinge->id, anchor);
  *x1 = anchor[0];
  *y1 = anchor[1];
  *z1 = anchor[2];
  dJointGetHingeAnchor2(hinge->id, anchor);
  *x2 = anchor[0];
  *y2 = anchor[1];
  *z2 = anchor[2];
}

void lovrHingeJointSetAnchor(HingeJoint* hinge, float x, float y, float z) {
  dJointSetHingeAnchor(hinge->id, x, y, z);
}

void lovrHingeJointGetAxis(HingeJoint* hinge, float* x, float* y, float* z) {
  float axis[3];
  dJointGetHingeAxis(hinge->id, axis);
  *x = axis[0];
  *y = axis[1];
  *z = axis[2];
}

void lovrHingeJointSetAxis(HingeJoint* hinge, float x, float y, float z) {
  dJointSetHingeAxis(hinge->id, x, y, z);
}

float lovrHingeJointGetAngle(HingeJoint* hinge) {
  return dJointGetHingeAngle(hinge->id);
}

float lovrHingeJointGetLowerLimit(HingeJoint* hinge) {
  return dJointGetHingeParam(hinge->id, dParamLoStop);
}

void lovrHingeJointSetLowerLimit(HingeJoint* hinge, float limit) {
  dJointSetHingeParam(hinge->id, dParamLoStop, limit);
}

float lovrHingeJointGetUpperLimit(HingeJoint* hinge) {
  return dJointGetHingeParam(hinge->id, dParamHiStop);
}

void lovrHingeJointSetUpperLimit(HingeJoint* hinge, float limit) {
  dJointSetHingeParam(hinge->id, dParamHiStop, limit);
}

SliderJoint* lovrSliderJointCreate(Collider* a, Collider* b, float ax, float ay, float az) {
  if (a->world != b->world) {
    error("Joint bodies must exist in same World");
  }

  SliderJoint* joint = lovrAlloc(sizeof(SliderJoint), lovrJointDestroy);
  if (!joint) return NULL;

  joint->type = JOINT_SLIDER;
  joint->id = dJointCreateSlider(a->world->id, 0);
  dJointSetData(joint->id, joint);
  dJointAttach(joint->id, a->body, b->body);
  dJointSetSliderAxis(joint->id, ax, ay, az);

  return joint;
}

void lovrSliderJointGetAxis(SliderJoint* slider, float* x, float* y, float* z) {
  float axis[3];
  dJointGetSliderAxis(slider->id, axis);
  *x = axis[0];
  *y = axis[1];
  *z = axis[2];
}

void lovrSliderJointSetAxis(SliderJoint* slider, float x, float y, float z) {
  dJointSetSliderAxis(slider->id, x, y, z);
}

float lovrSliderJointGetPosition(SliderJoint* slider) {
  return dJointGetSliderPosition(slider->id);
}

float lovrSliderJointGetLowerLimit(SliderJoint* slider) {
  return dJointGetSliderParam(slider->id, dParamLoStop);
}

void lovrSliderJointSetLowerLimit(SliderJoint* slider, float limit) {
  dJointSetSliderParam(slider->id, dParamLoStop, limit);
}

float lovrSliderJointGetUpperLimit(SliderJoint* slider) {
  return dJointGetSliderParam(slider->id, dParamHiStop);
}

void lovrSliderJointSetUpperLimit(SliderJoint* slider, float limit) {
  dJointSetSliderParam(slider->id, dParamHiStop, limit);
}
