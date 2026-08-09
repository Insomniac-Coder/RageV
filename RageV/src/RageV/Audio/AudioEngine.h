#pragma once
#include "RageV/Asset/Asset.h"
#include "RageV/Math/Math.h"
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

	enum class AudioMode
	{
		// Normal: open an output device and let it pull the mix.
		Device,
		// No device and no mixing. Voices are still allocated, tracked and
		// retired, so engine behaviour does not depend on the hardware. This is
		// what a machine with no sound card gets, and what --audio=off selects.
		Silent,
		// A real mixer with no device, pulled by RenderFrames.
		//
		// This exists so the output can be *looked at*. Every other check on
		// audio can pass while the engine produces silence -- "a voice was
		// created" says nothing about whether a sample came out of it, which is
		// the one thing that matters and the one thing nobody can verify by
		// listening on someone else's machine.
		Offline,
	};

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
		Vec3 Position{ 0.0f };

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
		static void Init(AudioMode mode = AudioMode::Device);
		static void Shutdown();

		// False when nothing will actually be heard -- no device was opened, or
		// the mode never intended to open one. Only worth asking in order to
		// tell the user; nothing else needs to branch on it.
		static bool IsAvailable();

		static AudioMode GetMode();
		// Zero before Init, and in Silent mode: there is no mix to have a
		// format.
		static uint32_t GetChannels();
		static uint32_t GetSampleRate();

		// Offline mode only; returns 0 otherwise.
		//
		// Mixes `frameCount` frames into `out`, which must hold
		// frameCount * GetChannels() floats, and returns how many it wrote.
		// This is the same graph a device pulls, so what it writes is what
		// would have been heard.
		static uint64_t RenderFrames(float* out, uint64_t frameCount);

		// Zero if the clip resolves to no file. Starts immediately.
		static AudioVoice Play(const AudioPlayback& playback);

		static void Stop(AudioVoice voice);
		static void StopAll();
		static bool IsPlaying(AudioVoice voice);

		// Ignored for a voice that has already ended, so a caller never has to
		// check first.
		static void SetVoicePosition(AudioVoice voice, const Vec3& position);
		static void SetVoiceVolume(AudioVoice voice, float volume);
		static void SetVoicePitch(AudioVoice voice, float pitch);

		// 0 silences, 1 is unchanged, above 1 amplifies and will clip.
		static void SetBusVolume(AudioBus bus, float volume);
		static float GetBusVolume(AudioBus bus);

		// Where the world is heard from. Forward and up need not be normalised.
		static void SetListener(const Vec3& position, const Vec3& forward,
								const Vec3& up = { 0.0f, 1.0f, 0.0f });

		// Retires voices that have played to their end. Once per frame, from
		// the application loop -- without it, one-shots accumulate for the
		// lifetime of the process.
		static void Update();

		static size_t GetVoiceCount();
	};
}
