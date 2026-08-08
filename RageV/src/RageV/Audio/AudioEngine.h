#pragma once
#include "RageV/Asset/Asset.h"
#include <glm/glm.hpp>
#include <cstdint>

namespace RageV
{
	// Where a sound is mixed.
	//
	// Deliberately a fixed short list rather than arbitrary named groups. Every
	// game needs exactly this separation -- turn the music down without turning
	// the gunfire down -- and a fixed enum is something the inspector, the
	// serializer and a settings screen can all present without inventing a
	// naming scheme first. Arbitrary buses can come later; they cannot be
	// removed later.
	enum class AudioBus : uint32_t
	{
		Master = 0,
		Music,
		SFX,
		UI,
		Count
	};

	const char* AudioBusName(AudioBus bus);

	// One playing sound. Zero means nothing is playing.
	//
	// A handle rather than a pointer because voices are retired from underneath
	// their owner: a one-shot ends on its own, and a stale pointer to the sound
	// that played it would be a use-after-free waiting for a slow frame.
	using AudioVoice = uint64_t;

	// Everything needed to start a sound. Filled from an AudioSourceComponent,
	// or by hand for a one-shot.
	struct AudioPlayback
	{
		AssetHandle Clip = AssetHandle::Invalid();
		AudioBus Bus = AudioBus::SFX;

		float Volume = 1.0f;
		float Pitch = 1.0f;
		bool Loop = false;

		// Decoded on the fly rather than up front. What music wants: a
		// three-minute track decoded into memory is tens of megabytes, and it
		// is played once.
		bool Stream = false;

		// Positioned in the world, and therefore attenuated by distance and
		// panned by direction. False for music and UI, which should not get
		// quieter because the listener walked away.
		bool Spatial = false;
		glm::vec3 Position{ 0.0f };

		// Full volume within MinDistance, falling off to MaxDistance.
		float MinDistance = 1.0f;
		float MaxDistance = 50.0f;
	};

	// Playback, mixing and 3D positioning, over miniaudio.
	//
	// Static rather than an instance for the same reason the renderer is: there
	// is one output device, and threading a reference to it through every
	// script that wants to make a noise buys nothing.
	//
	// **Every call is safe when there is no audio device.** Init records the
	// failure and everything afterwards runs silently -- voices are still
	// allocated, tracked and retired, so what the engine does does not depend
	// on whether the machine can play it back. A remote session, a machine with
	// audio disabled, and a headless test all take that path, and none of them
	// should behave differently from a machine that can.
	class AudioEngine
	{
	public:
		// Passing false skips opening a device and takes the silent path
		// deliberately. That is the same path a machine with no sound card
		// takes, so it is how that path stays working rather than being
		// assumed to.
		static void Init(bool openDevice = true);
		static void Shutdown();

		// False when no output device could be opened. Only worth asking in
		// order to tell the user; nothing else needs to branch on it.
		static bool IsAvailable();

		// Zero if the clip resolves to no file. Starts immediately.
		static AudioVoice Play(const AudioPlayback& playback);

		static void Stop(AudioVoice voice);
		static void StopAll();
		static bool IsPlaying(AudioVoice voice);

		// Ignored for a voice that has already ended, so a caller never has to
		// check first.
		static void SetVoicePosition(AudioVoice voice, const glm::vec3& position);
		static void SetVoiceVolume(AudioVoice voice, float volume);
		static void SetVoicePitch(AudioVoice voice, float pitch);

		// 0 silences, 1 is unchanged, above 1 amplifies and will clip.
		static void SetBusVolume(AudioBus bus, float volume);
		static float GetBusVolume(AudioBus bus);

		// Where the world is heard from. Forward and up need not be normalised.
		static void SetListener(const glm::vec3& position, const glm::vec3& forward,
								const glm::vec3& up = { 0.0f, 1.0f, 0.0f });

		// Retires voices that have played to their end. Once per frame, from
		// the application loop -- without it, one-shots accumulate for the
		// lifetime of the process.
		static void Update();

		static size_t GetVoiceCount();
	};
}
