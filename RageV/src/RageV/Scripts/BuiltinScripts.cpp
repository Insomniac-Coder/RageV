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
#include "RageV/Math/Math.h"
#include <unordered_map>

namespace RageV
{
	// Rotates at a constant rate. The smallest thing that proves a scene is
	// being stepped and that Stop puts it back.
	class Spinner : public ScriptableEntity
	{
	public:
		void OnTick(Timestep dt) override
		{
			// Radians per second, applied per fixed step. Multiplying by dt is
			// what keeps the rate the same at any simulation frequency.
			Rotate({ 0.0f, Speed * dt.GetSeconds(), 0.0f });
		}

		// Public because the inspector's registration names it from outside the
		// class, and C++ has no way to reach a private member without a friend
		// declaration in every script. The C# rule is the opposite -- private
		// fields are editable there -- because reflection can reach them, and
		// demanding `public` would mean telling people to write worse C# for
		// the inspector's benefit.
		float Speed = 1.2f;
	};

	// Moves with the movement axes, relative to its own orientation.
	//
	// Reads through actions rather than keycodes, so it works with whatever the
	// input map is bound to and keeps working when the player rebinds.
	class Mover : public ScriptableEntity
	{
	public:
		void OnTick(Timestep dt) override
		{
			const Vec3 direction =
				GetForward() * GetAxis("MoveForward") +
				GetRight()   * GetAxis("MoveRight") +
				Vec3(0.0f, 1.0f, 0.0f) * GetAxis("MoveUp");

			// Normalising a zero vector produces NaNs, which then spread
			// through every transform derived from this one.
			if (Math::Dot(direction, direction) > 0.0f)
			{
				const float speed = IsActionDown("Sprint") ? Speed * SprintMultiplier : Speed;
				Translate(Math::Normalize(direction) * speed * dt.GetSeconds());
			}
		}

		float Speed = 4.0f;
		float SprintMultiplier = 3.0f;
	};

	// Follows another entity by name, at an offset. Demonstrates reaching
	// outside the entity a script is attached to.
	//
	// Also the worked example of *why* there are two rates. A follow camera is
	// presentation: nothing collides with it and nothing scores off it, and the
	// thing it chases has already been interpolated for this frame by the time
	// OnFrame runs. Chasing from OnTick at 240 Hz would move the camera on one
	// frame in four while the world moved on all four -- which reads worse than
	// no smoothing at all, because the stutter is differential.
	class Follow : public ScriptableEntity
	{
	public:
		void OnCreate() override
		{
			m_Target = FindEntityByName(m_TargetName);
			if (!m_Target)
				RV_CORE_WARN("Follow: no entity named '{0}'", m_TargetName);
		}

		void OnFrame(Timestep dt) override
		{
			if (!m_Target)
				return;

			const Vec3 goal =
				m_Target.GetComponent<TransformComponent>().Position + m_Offset;

			// Framerate-independent smoothing: a plain lerp by a constant
			// factor converges at a rate that depends on the step size -- which
			// on a frame, where dt varies, would mean the camera lagged further
			// behind whenever the frame rate dipped.
			const float t = 1.0f - std::exp(-m_Sharpness * dt.GetSeconds());
			GetPosition() += (goal - GetPosition()) * t;
		}

	private:
		std::string m_TargetName = "Player";
		Vec3 m_Offset{ 0.0f, 3.0f, 8.0f };
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
			if (!HasComponent<MeshComponent>())
				return;

			// The entity's own emissive override, not the material's.
			//
			// This used to reach into the Material object and write its
			// parameter block. That was already a shared resource in principle
			// and is one in fact now: two crates using one `.rmat` would both
			// light up when either was hit, and the flash would survive Stop
			// because a material is not scene data and a snapshot does not
			// restore it.
			//
			// An override is per entity, is scene data, and is restored by the
			// snapshot like anything else on the component -- so OnDestroy no
			// longer has to put anything back by hand.
			auto& mesh = GetComponent<MeshComponent>();
			m_Rest = mesh.OverrideEmissive ? mesh.EmissiveColor : Vec4(0.0f, 0.0f, 0.0f, 1.0f);
			m_Active = true;
		}

		void OnCollisionEnter(const Collision& collision) override
		{
			if (!m_Active)
				return;

			// Saturating rather than linear: a fall from any real height lands
			// at well over the speed that already reads as a bright flash, so a
			// linear scale would make every impact past the first look the same.
			m_Flash = Math::Max(m_Flash, Math::Min(collision.ImpactSpeed / 8.0f, 1.0f));
		}

		// On the frame, not the step: the impact that starts it is gameplay and
		// arrives in OnCollisionEnter, but the fade that follows is only ever
		// looked at. Fading per frame is also what stops it stepping visibly at
		// a high refresh rate.
		void OnFrame(Timestep dt) override
		{
			if (!m_Active || m_Flash <= 0.0f || !HasComponent<MeshComponent>())
				return;

			m_Flash = Math::Max(m_Flash - dt.GetSeconds() / m_FadeSeconds, 0.0f);

			auto& mesh = GetComponent<MeshComponent>();
			mesh.OverrideEmissive = true;
			mesh.EmissiveColor = m_Rest + Vec4(m_Colour * m_Flash, 0.0f);
			// No Invalidate: the scalars travel in the instance stream and are
			// rebuilt every frame, so there is no GPU buffer to mark dirty.
		}

	private:
		bool m_Active = false;
		Vec4 m_Rest{ 0.0f };
		Vec3 m_Colour{ 1.0f, 0.55f, 0.25f };
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

			const float loudness = Math::Min(collision.ImpactSpeed / m_FullVolumeSpeed, 1.0f);
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

			// Keyed by entity, and now tinting per entity too.
			//
			// The comment below used to explain why the *bookkeeping* was keyed
			// by entity while the tint was written into a shared Material: two
			// entities could share one, and the second to leave would restore a
			// colour the first had already put back. The override removes the
			// problem rather than working around it -- each entity carries its
			// own base colour, so nothing is shared to get wrong.
			const UUID id = collision.Other.GetUUID();
			if (m_Occupants.find(id) != m_Occupants.end())
				return;

			auto& mesh = collision.Other.GetComponent<MeshComponent>();
			m_Occupants[id] = { mesh.OverrideBaseColor, mesh.BaseColor };

			mesh.OverrideBaseColor = true;
			mesh.BaseColor = m_Tint;
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
			// Whether the entity had a base-colour override *before* this zone
			// touched it. Restoring has to put the switch back as well as the
			// value, or an entity that had no override keeps one forever --
			// with the colour it happened to be showing when it walked in.
			bool HadOverride = false;
			Vec4 Original{ 1.0f };
		};

		void Restore(UUID id)
		{
			const auto it = m_Occupants.find(id);
			if (it == m_Occupants.end())
				return;

			// The entity may be gone -- an exit can be *because* it was
			// destroyed -- so this is a lookup rather than a stored reference.
			if (Entity entity = FindEntityByUUID(id); entity && entity.HasComponent<MeshComponent>())
			{
				auto& mesh = entity.GetComponent<MeshComponent>();
				mesh.OverrideBaseColor = it->second.HadOverride;
				mesh.BaseColor = it->second.Original;
			}

			m_Occupants.erase(it);
		}

		std::unordered_map<UUID, Tinted> m_Occupants;
		Vec4 m_Tint{ 0.95f, 0.24f, 0.26f, 1.0f };
	};

	// Counts its own clicks into a label. The worked example for the UI, and
	// the regression probe for it -- the same job ContactCounter does for the
	// managed contact surface.
	//
	// Short on purpose: the whole of a working button is *one* line of polling
	// in OnTick. Everything that makes that line correct -- hit-testing front
	// to back, a press that is cancellable, an edge consumed by exactly one
	// step -- happens before it is reached, which is the point.
	class ClickCounter : public ScriptableEntity
	{
	public:
		// Authored in the inspector, so the same script labels a Start button
		// and a Quit one.
		std::string Caption = "Click me";

		void OnCreate() override
		{
			// The label is a child rather than this entity, because a button is
			// an image and a caption laid out inside it. Cached here rather than
			// searched every step.
			for (Entity child : GetChildren())
			{
				if (child.HasComponent<UITextComponent>())
				{
					m_Label = child;
					break;
				}
			}

			Show();
		}

		// --- the two ways to hear about a click ------------------------------
		//
		// **This script demonstrates both, and that is the point of it.**
		//
		// `Count` is *bound*: a button's OnClick names it, and the engine calls
		// it. That is the one to reach for when a manager handles several
		// buttons, or when the handler does not live on the button.
		//
		// `OnTick` *polls*: it asks its own button whether it was clicked. That
		// is the one to reach for when the script is on the button itself, or
		// when a click is one of several things a step already checks.
		//
		// They are the same click seen two ways, so a button that both names
		// Count and carries this script would count twice -- which is not a
		// trap so much as arithmetic, and the demo scene binds one button per
		// mechanism rather than doubling up on one.
		void Count()
		{
			m_Clicks++;
			Show();
		}

		// The fixed step, not the frame: a click is an event the game acts on,
		// and acting on it twice because a frame ran two steps is exactly the
		// bug the edge contract exists to prevent.
		void OnTick(Timestep) override
		{
			if (PollOwnButton && WasButtonClicked())
				Count();
		}

		// Off for a button that binds Count, on for one that does not. Authored
		// rather than inferred: this script cannot see its own button's
		// binding, and guessing would make the double-count depend on a field
		// nobody could see.
		bool PollOwnButton = true;

		int Clicks() const { return m_Clicks; }

	private:
		void Show()
		{
			if (!m_Label)
				return;

			SetText(m_Label, m_Clicks == 0
				? std::string(Caption)
				: Caption + std::string(" x") + std::to_string(m_Clicks));
		}

		Entity m_Label;
		int m_Clicks = 0;
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
		ScriptRegistry::Register("Spinner", []() -> ScriptableEntity* { return new Spinner(); })
			.Field<&Spinner::Speed>("Speed");
		ScriptRegistry::Register("Mover", []() -> ScriptableEntity* { return new Mover(); })
			.Field<&Mover::Speed>("Speed")
			.Field<&Mover::SprintMultiplier>("SprintMultiplier");
		ScriptRegistry::Register("Follow",      []() -> ScriptableEntity* { return new Follow(); });
		ScriptRegistry::Register("ImpactFlash", []() -> ScriptableEntity* { return new ImpactFlash(); });
		ScriptRegistry::Register("ImpactSound", []() -> ScriptableEntity* { return new ImpactSound(); });
		ScriptRegistry::Register("TriggerZone", []() -> ScriptableEntity* { return new TriggerZone(); });
		ScriptRegistry::Register("ClickCounter", []() -> ScriptableEntity* { return new ClickCounter(); })
			.Field<&ClickCounter::Caption>("Caption")
			.Field<&ClickCounter::PollOwnButton>("PollOwnButton")
			// The worked example for a bound handler, and the regression probe
			// for it -- the same role RageV.Builtin.ContactCounter plays for
			// the contact path.
			.Method<&ClickCounter::Count>("Count");
	}
}
