#include "Scene.hpp"
#include "Components.hpp"

PhysicsWorld& Scene::EnsurePhysicsWorld()
{
    if (!m_PhysicsWorld)
    {
        m_PhysicsWorld = std::make_unique<PhysicsWorld>();
        // 以前は SceneSerializer で RigidBody を読んだときだけ Init していて、
        // それ以外のシーンでは後から RigidBody を付けても動かなかった
        m_PhysicsWorld->Init();
    }
    return *m_PhysicsWorld;
}

bool Scene::HasPhysicsWorld() const
{
    return m_PhysicsWorld != nullptr;
}

void Scene::ResetWorld()
{
    // Voice は AudioEngine 側が持っているので、Component を捨てる前に止めないと鳴り続ける
    m_World.Each<AudioSourceComponent>(
        [](Entity, AudioSourceComponent& src)
        {
            if (src.voice)
            {
                src.voice->Stop();
                src.voice->FlushSourceBuffers();
            }
        });

	m_World.Clear();
    ResetPhysicsWorld();
}

void Scene::ResetPhysicsWorld()
{
    m_PhysicsWorld.reset();
}

Scene::Scene()
{
}
