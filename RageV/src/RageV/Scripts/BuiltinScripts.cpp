// Scripts that ship with the engine.
//
// Two reasons they exist rather than being sample code in the editor project.
// First, pressing Play has to do something observable out of the box, or the
// whole play/stop loop is unverifiable by eye. Second, they are the worked
// examples of the script API -- what a script is allowed to reach, and what
// reads well.

#include <rvpch.h>
#include "RageV/Scene/ScriptRegistry.h"
#include "RageV/Scene/Components.h"
#include <glm/glm.hpp>
#include <unordered_map>

namespace RageV
{
	// Rotates at a constant rate. The smallest thing that proves a scene is
	// being stepped and that Stop puts it back.
	class Spinner : public ScriptableEntity
	{
	public:
		void OnUpdate(Timestep dt) override
		{
			// Radians per second, applied per fixed step. Multiplying by dt is
			// what keeps the rate the same at any simulation frequency.
			Rotate({ 0.0f, m_Speed * dt.GetSeconds(), 0.0f });
		}

	private:
		float m_Speed = 1.2f;
	};

	// Moves with the movement axes, relative to its own orientation.
	//
	// Reads through actions rather than keycodes, so it works with whatever the
	// input map is bound to and keeps working when the player rebinds.
	class Mover : public ScriptableEntity
	{
	public:
		void OnUpdate(Timestep dt) override
		{
			const glm::vec3 direction =
				GetForward() * GetAxis("MoveForward") +
				GetRight()   * GetAxis("MoveRight") +
				glm::vec3(0.0f, 1.0f, 0.0f) * GetAxis("MoveUp");

			// Normalising a zero vector produces NaNs, which then spread
			// through every transform derived from this one.
			if (glm::dot(direction, direction) > 0.0f)
			{
				const float speed = IsActionDown("Sprint") ? m_Speed * 3.0f : m_Speed;
				Translate(glm::normalize(direction) * speed * dt.GetSeconds());
			}
		}

	private:
		float m_Speed = 4.0f;
	};

	// Follows another entity by name, at an offset. Demonstrates reaching
	// outside the entity a script is attached to.
	class Follow : public ScriptableEntity
	{
	public:
		void OnCreate() override
		{
			m_Target = FindEntityByName(m_TargetName);
			if (!m_Target)
				RV_CORE_WARN("Follow: no entity named '{0}'", m_TargetName);
		}

		void OnUpdate(Timestep dt) override
		{
			if (!m_Target)
				return;

			const glm::vec3 goal =
				m_Target.GetComponent<TransformComponent>().Position + m_Offset;

			// Framerate-independent smoothing: a plain lerp by a constant
			// factor converges at a rate that depends on the step size.
			const float t = 1.0f - std::exp(-m_Sharpness * dt.GetSeconds());
			GetPosition() += (goal - GetPosition()) * t;
		}

	private:
		std::string m_TargetName = "Player";
		glm::vec3 m_Offset{ 0.0f, 3.0f, 8.0f };
		float m_Sharpness = 4.0f;
		Entity m_Target;
	};
	// Glows on impact, in proportion to how hard it was hit, and fades back.
	//
	// The worked example for OnCollisionEnter, and the reason ImpactSpeed is on
	// Collision at all: "something touched me" is rarely enough on its own, and
	// almost every use -- an impact sound's volume, a damage number, the size of
	// a dent -- wants to know how hard.
	class ImpactFlash : public ScriptableEntity
	{
	public:
		void OnCreate() override
		{
			// A null material means the renderer's shared default, which every
			// other object without one is also drawn with. Tinting that would
			// light up the whole scene on one collision.
			if (!HasComponent<MeshComponent>() || !GetComponent<MeshComponent>().Material)
				return;

			m_Material = GetComponent<MeshComponent>().Material;
			m_Rest = m_Material->GetParams().EmissiveColor;
		}

		void OnCollisionEnter(const Collision& collision) override
		{
			if (!m_Material)
				return;

			// Saturating rather than linear: a fall from any real height lands
			// at well over the speed that already reads as a bright flash, so a
			// linear scale would make every impact past the first look the same.
			m_Flash = glm::max(m_Flash, glm::min(collision.ImpactSpeed / 8.0f, 1.0f));
		}

		void OnUpdate(Timestep dt) override
		{
			if (!m_Material || m_Flash <= 0.0f)
				return;

			m_Flash = glm::max(m_Flash - dt.GetSeconds() / m_FadeSeconds, 0.0f);

			auto& params = m_Material->GetParams();
			params.EmissiveColor = m_Rest + glm::vec4(m_Colour * m_Flash, 0.0f);
			// The parameter block is a GPU buffer; without this the write above
			// never reaches it.
			m_Material->Invalidate();
		}

		void OnDestroy() override
		{
			// Stop puts the scene back from a snapshot, but a material is a
			// shared resource rather than scene data, so a flash left mid-fade
			// would survive it.
			if (!m_Material)
				return;

			m_Material->GetParams().EmissiveColor = m_Rest;
			m_Material->Invalidate();
		}

	private:
		RHI::Ref<Material> m_Material;
		glm::vec4 m_Rest{ 0.0f };
		glm::vec3 m_Colour{ 1.0f, 0.55f, 0.25f };
		float m_Flash = 0.0f;
		float m_FadeSeconds = 0.45f;
	};

	// Plays this entity's clip when it is hit, at a volume that follows how
	// hard. Needs an AudioSourceComponent for the clip; PlayOnAwake should be
	// off, since the point is that a collision starts it.
	//
	// A one-shot rather than the source itself, so two hits close together
	// overlap the way two real impacts would instead of cutting each other off.
	class ImpactSound : public ScriptableEntity
	{
	public:
		void OnCollisionEnter(const Collision& collision) override
		{
			if (!HasComponent<AudioSourceComponent>())
				return;

			const auto& source = GetComponent<AudioSourceComponent>();
			if (!source.Clip.IsValid())
				return;

			// Below this, a contact is something settling rather than landing.
			// Without the threshold a stack coming to rest sounds like a
			// machine gun: the last centimetre of a box finding its place is
			// several separate contacts, each of them technically a collision.
			if (collision.ImpactSpeed < m_QuietestAudibleSpeed)
				return;

			const float loudness = glm::min(collision.ImpactSpeed / m_FullVolumeSpeed, 1.0f);
			PlayOneShot(source.Clip, loudness * source.Volume);
		}

	private:
		float m_QuietestAudibleSpeed = 0.9f;
		float m_FullVolumeSpeed = 7.0f;
	};

	// An invisible volume that tints whatever is inside it.
	//
	// The worked example for triggers, and for reaching the *other* entity in a
	// collision rather than the one the script is on. Attach it to an entity
	// with a collider marked IsTrigger.
	class TriggerZone : public ScriptableEntity
	{
	public:
		void OnTriggerEnter(const Collision& collision) override
		{
			// Announced whether or not the thing that entered can be tinted --
			// a pressure plate with nothing to recolour should still click.
			if (HasComponent<AudioSourceComponent>())
			{
				const auto& source = GetComponent<AudioSourceComponent>();
				if (source.Clip.IsValid())
					PlayOneShot(source.Clip, source.Volume);
			}

			if (!collision.Other || !collision.Other.HasComponent<MeshComponent>())
				return;

			const RHI::Ref<Material> material = collision.Other.GetComponent<MeshComponent>().Material;
			if (!material)
				return;

			// Keyed by entity, not by material: two entities may share one, and
			// the second to leave would otherwise restore a colour that the
			// first had already put back.
			const UUID id = collision.Other.GetUUID();
			if (m_Occupants.find(id) != m_Occupants.end())
				return;

			m_Occupants[id] = { material, material->GetParams().BaseColor };

			material->GetParams().BaseColor = m_Tint;
			material->Invalidate();
		}

		void OnTriggerExit(const Collision& collision) override
		{
			// Deliberately by id rather than through collision.Other: the exit
			// may be *because* it was destroyed, in which case Other is invalid
			// and the entity's components are already gone.
			Restore(collision.Other ? collision.Other.GetUUID() : UUID::Invalid());
		}

		void OnDestroy() override
		{
			while (!m_Occupants.empty())
				Restore(m_Occupants.begin()->first);
		}

		// How many bodies are inside. Useful on its own -- a pressure plate is
		// this and nothing else.
		size_t GetOccupantCount() const { return m_Occupants.size(); }

	private:
		struct Tinted
		{
			RHI::Ref<Material> Material;
			glm::vec4 Original{ 1.0f };
		};

		void Restore(UUID id)
		{
			const auto it = m_Occupants.find(id);
			if (it == m_Occupants.end())
				return;

			if (it->second.Material)
			{
				it->second.Material->GetParams().BaseColor = it->second.Original;
				it->second.Material->Invalidate();
			}
			m_Occupants.erase(it);
		}

		std::unordered_map<UUID, Tinted> m_Occupants;
		glm::vec4 m_Tint{ 0.95f, 0.24f, 0.26f, 1.0f };
	};

	// Called explicitly rather than registered by a static initializer.
	//
	// This file's only contents used to be registrar objects, and a linker may
	// drop an object file from a static library when nothing references a
	// symbol in it. It did: the registrations never ran, the inspector's script
	// dropdown was empty, and pressing Play could do nothing because no script
	// could be attached in the first place.
	//
	// An explicit function referenced from ScriptRegistry forces this
	// translation unit to be linked. Scripts compiled straight into an
	// executable do not have the problem -- object files handed to the linker
	// directly are always included -- so RV_REGISTER_SCRIPT remains the right
	// tool for game code.
	void RegisterBuiltinScripts()
	{
		// Unqualified names on purpose: these strings go into scene files.
		ScriptRegistry::Register("Spinner",     []() -> ScriptableEntity* { return new Spinner(); });
		ScriptRegistry::Register("Mover",       []() -> ScriptableEntity* { return new Mover(); });
		ScriptRegistry::Register("Follow",      []() -> ScriptableEntity* { return new Follow(); });
		ScriptRegistry::Register("ImpactFlash", []() -> ScriptableEntity* { return new ImpactFlash(); });
		ScriptRegistry::Register("ImpactSound", []() -> ScriptableEntity* { return new ImpactSound(); });
		ScriptRegistry::Register("TriggerZone", []() -> ScriptableEntity* { return new TriggerZone(); });
	}
}
