#include "modes/FloatingPhysics.h"

// Jolt must be included first.
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_map>

namespace it {

namespace {

constexpr JPH::ObjectLayer kMoving = 0;

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return 1; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer) const override { return JPH::BroadPhaseLayer(0); }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer) const override { return "cards"; }
#endif
};
class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer, JPH::BroadPhaseLayer) const override { return true; }
};
class ObjectPairs final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer, JPH::ObjectLayer) const override { return true; }
};

void joltGlobalInit() {
    static std::once_flag once;
    std::call_once(once, [] {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();  // lives for the whole process
        JPH::RegisterTypes();
    });
}

JPH::Vec3 toJ(const glm::vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
glm::vec3 fromJ(JPH::Vec3Arg v) { return {v.GetX(), v.GetY(), v.GetZ()}; }

}  // namespace

struct FloatingPhysics::Impl {
    BroadPhaseLayers bpLayers;
    ObjectVsBroadPhase objVsBp;
    ObjectPairs objPairs;
    JPH::TempAllocatorImpl temp{8 * 1024 * 1024};
    JPH::JobSystemSingleThreaded jobs{JPH::cMaxPhysicsJobs};  // decode threads already use the other cores
    JPH::PhysicsSystem system;
    bool allowSpin = true;
    int addedSinceOptimize = 0;
    std::unordered_map<uint32_t, float> cruise;  // body id -> cruise speed toward the camera
};

FloatingPhysics::FloatingPhysics(bool allowSpin) {
    joltGlobalInit();
    impl_ = std::make_unique<Impl>();
    impl_->allowSpin = allowSpin;
    impl_->system.Init(1024, 0, 4096, 2048, impl_->bpLayers, impl_->objVsBp, impl_->objPairs);
    impl_->system.SetGravity(JPH::Vec3::sZero());
}

FloatingPhysics::~FloatingPhysics() {
    if (!impl_) return;
    JPH::BodyInterface& bi = impl_->system.GetBodyInterface();
    for (const auto& [id, c] : impl_->cruise) {
        bi.RemoveBody(JPH::BodyID(id));
        bi.DestroyBody(JPH::BodyID(id));
    }
}

uint32_t FloatingPhysics::add(const glm::vec3& pos, const glm::quat& rot, const glm::vec2& halfSize,
                              const glm::vec2& offset, const glm::vec3& vel, const glm::vec3& angVel) {
    constexpr float kHalfThickness = 0.5f, kConvex = 0.05f;
    JPH::Ref<JPH::Shape> box = new JPH::BoxShape(
        JPH::Vec3(std::max(halfSize.x, 0.2f), std::max(halfSize.y, 0.2f), kHalfThickness), kConvex);
    JPH::Ref<JPH::Shape> shape = new JPH::RotatedTranslatedShape(JPH::Vec3(offset.x, offset.y, 0.0f),
                                                                 JPH::Quat::sIdentity(), box);
    JPH::BodyCreationSettings s(shape, JPH::RVec3(pos.x, pos.y, pos.z), JPH::Quat(rot.x, rot.y, rot.z, rot.w),
                                JPH::EMotionType::Dynamic, kMoving);
    s.mGravityFactor = 0.0f;
    s.mAllowSleeping = false;
    s.mLinearDamping = 0.0f;
    s.mAngularDamping = 0.0f;  // applied in step() so it follows the live parameters
    s.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
    s.mMassPropertiesOverride.mMass = 1.0f;
    if (!impl_->allowSpin)
        s.mAllowedDOFs = JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY | JPH::EAllowedDOFs::TranslationZ;
    s.mLinearVelocity = toJ(vel);
    if (impl_->allowSpin) s.mAngularVelocity = toJ(angVel);
    const JPH::BodyID id = impl_->system.GetBodyInterface().CreateAndAddBody(s, JPH::EActivation::Activate);
    if (id.IsInvalid()) return UINT32_MAX;
    impl_->cruise[id.GetIndexAndSequenceNumber()] = vel.z;
    ++impl_->addedSinceOptimize;
    return id.GetIndexAndSequenceNumber();
}

void FloatingPhysics::remove(uint32_t id) {
    if (id == UINT32_MAX) return;
    JPH::BodyInterface& bi = impl_->system.GetBodyInterface();
    bi.RemoveBody(JPH::BodyID(id));
    bi.DestroyBody(JPH::BodyID(id));
    impl_->cruise.erase(id);
}

void FloatingPhysics::setCruise(uint32_t id, float cruise) {
    auto it = impl_->cruise.find(id);
    if (it != impl_->cruise.end()) it->second = cruise;
}

void FloatingPhysics::step(float dt, const Params& p) {
    if (dt <= 0.0f) return;
    // Bodies arrive one at a time as cards respawn; Jolt's broad phase degrades unless it is
    // re-optimised after additions. With this and the lock-free interface, 500 cards went from
    // ~7.6 ms to ~1.2 ms per step on a Ryzen 9950X3D (112 cards: ~0.2 ms).
    if (impl_->addedSinceOptimize >= 16) {
        impl_->system.OptimizeBroadPhase();
        impl_->addedSinceOptimize = 0;
    }
    // Single-threaded use, so the lock-free body interface is safe (and much cheaper per call).
    JPH::BodyInterface& bi = impl_->system.GetBodyInterfaceNoLock();
    const float relax = std::min(1.0f, p.speedRelax * dt);
    const float damp = std::exp(-p.lateralDamping * dt);
    const float spinDamp = std::exp(-p.angularDamping * dt);
    for (const auto& [raw, cruise] : impl_->cruise) {
        const JPH::BodyID id(raw);
        glm::vec3 v = fromJ(bi.GetLinearVelocity(id));
        const glm::vec3 x = fromJ(JPH::Vec3(bi.GetPosition(id)));
        v.z += (cruise - v.z) * relax;  // conveyor toward the camera
        v.x *= damp;
        v.y *= damp;
        const float r = glm::length(glm::vec2(x));
        if (r > 1e-3f) {  // soft ring containment, as in make-way
            const glm::vec2 dir = glm::vec2(x) / r;
            float spring = 0.0f;
            if (r > p.radius * 1.1f) spring = -(r - p.radius * 1.1f);
            else if (r < p.radius * 0.5f) spring = p.radius * 0.5f - r;
            v.x += dir.x * spring * 2.0f * dt;
            v.y += dir.y * spring * 2.0f * dt;
        }
        bi.SetLinearVelocity(id, toJ(v));
        if (impl_->allowSpin) bi.SetAngularVelocity(id, bi.GetAngularVelocity(id) * spinDamp);
        bi.SetRestitution(id, p.restitution);
        bi.SetFriction(id, p.friction);
    }
    // Sub-step only below ~60 fps: cards move < 1 unit (their thickness) per 1/60 s step.
    const int steps = std::clamp(static_cast<int>(std::ceil(dt / (1.0f / 60.0f) - 0.05f)), 1, 4);
    impl_->system.Update(dt, steps, &impl_->temp, &impl_->jobs);
}

void FloatingPhysics::read(uint32_t id, glm::vec3& pos, glm::quat& rot) const {
    JPH::RVec3 p;
    JPH::Quat q;
    impl_->system.GetBodyInterfaceNoLock().GetPositionAndRotation(JPH::BodyID(id), p, q);
    pos = {static_cast<float>(p.GetX()), static_cast<float>(p.GetY()), static_cast<float>(p.GetZ())};
    rot = glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
}

int FloatingPhysics::bodyCount() const { return static_cast<int>(impl_->cruise.size()); }

}  // namespace it
