#include <rvpch.h>
#include "AudioEngine.h"
#include "RageV/Core/Log.h"
#include "RageV/Asset/AssetRegistry.h"

// miniaudio's implementation lives in the vendored miniaudio.c; this is the
// only translation unit in the engine that needs its declarations, which keeps
// a 90000-line header out of everything else's compile.
#include <miniaudio.h>

#include <filesystem>
#include <memory>
#include <unordered_map>

namespace RageV
{
	namespace
	{
		struct Voice
		{
			// Heap-allocated because ma_sound is large and the map moves its
			// values around; miniaudio holds pointers into a live sound from
			// the node graph, so it may not be relocated.
			std::unique_ptr<ma_sound> Sound;
			bool Loop = false;
		};

		struct AudioState
		{
			bool Initialised = false;
			bool Available = false;

			ma_engine Engine{};
			// One per bus except Master, which is the engine's own volume --
			// there is no group above the endpoint to attach one to.
			ma_sound_group Groups[(size_t)AudioBus::Count]{};

			float BusVolume[(size_t)AudioBus::Count] = { 1.0f, 1.0f, 1.0f, 1.0f };

			std::unordered_map<AudioVoice, Voice> Voices;
			AudioVoice NextVoice = 1;

			// Reported once per handle rather than once per attempt: a looping
			// source with a missing clip would otherwise log every frame.
			std::unordered_map<uint64_t, bool> Warned;
		};

		AudioState s_Audio;

		ma_sound_group* GroupFor(AudioBus bus)
		{
			// Master has no group of its own. A sound assigned to it goes
			// straight to the endpoint, which the engine's volume scales.
			if (!s_Audio.Available || bus == AudioBus::Master || bus >= AudioBus::Count)
				return nullptr;

			return &s_Audio.Groups[(size_t)bus];
		}

		std::string ResolveClip(AssetHandle clip)
		{
			if (!clip.IsValid())
				return {};

			const std::filesystem::path path = AssetRegistry::GetAbsolutePath(clip);
			if (path.empty() || !std::filesystem::exists(path))
				return {};

			return path.string();
		}
	}

	const char* AudioBusName(AudioBus bus)
	{
		switch (bus)
		{
			case AudioBus::Master: return "Master";
			case AudioBus::Music:  return "Music";
			case AudioBus::SFX:    return "SFX";
			case AudioBus::UI:     return "UI";
			default:               return "Master";
		}
	}

	void AudioEngine::Init(bool openDevice)
	{
		if (s_Audio.Initialised)
			return;

		s_Audio.Initialised = true;

		if (!openDevice)
		{
			RV_CORE_INFO("Audio: disabled; playback will be silent");
			s_Audio.Available = false;
			return;
		}

		ma_engine_config config = ma_engine_config_init();
		// One listener. More exist for split-screen, which is not on the
		// roadmap, and every extra listener costs a spatialisation pass per
		// sound.
		config.listenerCount = 1;

		if (ma_engine_init(&config, &s_Audio.Engine) != MA_SUCCESS)
		{
			// Not an error. A machine with no output device, a remote session,
			// or a test host is a normal thing to run on, and none of them
			// should stop the engine starting.
			RV_CORE_WARN("Audio: no output device; playback will be silent");
			s_Audio.Available = false;
			return;
		}

		s_Audio.Available = true;

		for (uint32_t i = 0; i < (uint32_t)AudioBus::Count; i++)
		{
			if (i == (uint32_t)AudioBus::Master)
				continue;

			if (ma_sound_group_init(&s_Audio.Engine, 0, nullptr, &s_Audio.Groups[i]) != MA_SUCCESS)
				RV_CORE_ERROR("Audio: could not create the {0} bus", AudioBusName((AudioBus)i));
		}

		RV_CORE_INFO("Audio: {0} Hz, {1} channels",
					 ma_engine_get_sample_rate(&s_Audio.Engine),
					 ma_engine_get_channels(&s_Audio.Engine));
	}

	void AudioEngine::Shutdown()
	{
		if (!s_Audio.Initialised)
			return;

		// Sounds before groups before the engine: each holds a node in the
		// graph owned by the next, and tearing down out of order leaves the
		// audio thread reading freed memory.
		StopAll();

		if (s_Audio.Available)
		{
			for (uint32_t i = 0; i < (uint32_t)AudioBus::Count; i++)
			{
				if (i != (uint32_t)AudioBus::Master)
					ma_sound_group_uninit(&s_Audio.Groups[i]);
			}

			ma_engine_uninit(&s_Audio.Engine);
		}

		s_Audio.Available = false;
		s_Audio.Initialised = false;
		s_Audio.NextVoice = 1;
		s_Audio.Warned.clear();

		for (float& volume : s_Audio.BusVolume)
			volume = 1.0f;
	}

	bool AudioEngine::IsAvailable()
	{
		return s_Audio.Available;
	}

	AudioVoice AudioEngine::Play(const AudioPlayback& playback)
	{
		// Resolved before the device is considered, so a broken clip reference
		// is reported the same way whether or not anything can be heard. A bug
		// that only appears on machines with speakers is a bad bug.
		const std::string path = ResolveClip(playback.Clip);
		if (path.empty())
		{
			if (playback.Clip.IsValid() && !s_Audio.Warned[(uint64_t)playback.Clip])
			{
				s_Audio.Warned[(uint64_t)playback.Clip] = true;
				RV_CORE_WARN("Audio: clip {0} resolves to no file", (uint64_t)playback.Clip);
			}
			return 0;
		}

		const AudioVoice id = s_Audio.NextVoice++;

		Voice voice;
		voice.Loop = playback.Loop;

		if (!s_Audio.Available)
		{
			// Tracked with no sound behind it, so the bookkeeping a caller
			// depends on is identical either way.
			s_Audio.Voices[id] = std::move(voice);
			return id;
		}

		ma_uint32 flags = 0;
		// Streaming decodes as it plays; the alternative decodes the whole
		// clip up front, which is what makes a short sound cheap to retrigger.
		flags |= playback.Stream ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
		if (!playback.Spatial)
			flags |= MA_SOUND_FLAG_NO_SPATIALIZATION;

		voice.Sound = std::make_unique<ma_sound>();

		if (ma_sound_init_from_file(&s_Audio.Engine, path.c_str(), flags,
									GroupFor(playback.Bus), nullptr, voice.Sound.get()) != MA_SUCCESS)
		{
			RV_CORE_ERROR("Audio: could not open {0}", path);
			return 0;
		}

		ma_sound_set_volume(voice.Sound.get(), glm::max(playback.Volume, 0.0f));
		ma_sound_set_pitch(voice.Sound.get(), glm::max(playback.Pitch, 0.01f));
		ma_sound_set_looping(voice.Sound.get(), playback.Loop ? MA_TRUE : MA_FALSE);

		if (playback.Spatial)
		{
			ma_sound_set_position(voice.Sound.get(), playback.Position.x,
								  playback.Position.y, playback.Position.z);
			ma_sound_set_min_distance(voice.Sound.get(), glm::max(playback.MinDistance, 0.01f));
			ma_sound_set_max_distance(voice.Sound.get(),
									  glm::max(playback.MaxDistance, playback.MinDistance + 0.01f));
		}

		ma_sound_start(voice.Sound.get());
		s_Audio.Voices[id] = std::move(voice);
		return id;
	}

	void AudioEngine::Stop(AudioVoice voice)
	{
		const auto it = s_Audio.Voices.find(voice);
		if (it == s_Audio.Voices.end())
			return;

		if (it->second.Sound)
		{
			ma_sound_stop(it->second.Sound.get());
			ma_sound_uninit(it->second.Sound.get());
		}

		s_Audio.Voices.erase(it);
	}

	void AudioEngine::StopAll()
	{
		for (auto& [id, voice] : s_Audio.Voices)
		{
			if (voice.Sound)
			{
				ma_sound_stop(voice.Sound.get());
				ma_sound_uninit(voice.Sound.get());
			}
		}
		s_Audio.Voices.clear();
	}

	bool AudioEngine::IsPlaying(AudioVoice voice)
	{
		const auto it = s_Audio.Voices.find(voice);
		if (it == s_Audio.Voices.end())
			return false;

		// A tracked voice with no sound is one that would be playing if there
		// were anywhere to play it.
		if (!it->second.Sound)
			return true;

		return ma_sound_is_playing(it->second.Sound.get()) == MA_TRUE;
	}

	void AudioEngine::SetVoicePosition(AudioVoice voice, const glm::vec3& position)
	{
		const auto it = s_Audio.Voices.find(voice);
		if (it == s_Audio.Voices.end() || !it->second.Sound)
			return;

		ma_sound_set_position(it->second.Sound.get(), position.x, position.y, position.z);
	}

	void AudioEngine::SetVoiceVolume(AudioVoice voice, float volume)
	{
		const auto it = s_Audio.Voices.find(voice);
		if (it == s_Audio.Voices.end() || !it->second.Sound)
			return;

		ma_sound_set_volume(it->second.Sound.get(), glm::max(volume, 0.0f));
	}

	void AudioEngine::SetVoicePitch(AudioVoice voice, float pitch)
	{
		const auto it = s_Audio.Voices.find(voice);
		if (it == s_Audio.Voices.end() || !it->second.Sound)
			return;

		ma_sound_set_pitch(it->second.Sound.get(), glm::max(pitch, 0.01f));
	}

	void AudioEngine::SetBusVolume(AudioBus bus, float volume)
	{
		if (bus >= AudioBus::Count)
			return;

		volume = glm::max(volume, 0.0f);
		s_Audio.BusVolume[(size_t)bus] = volume;

		if (!s_Audio.Available)
			return;

		if (bus == AudioBus::Master)
			ma_engine_set_volume(&s_Audio.Engine, volume);
		else
			ma_sound_group_set_volume(&s_Audio.Groups[(size_t)bus], volume);
	}

	float AudioEngine::GetBusVolume(AudioBus bus)
	{
		// Read back from the cached value rather than from miniaudio, so it
		// reads the same with and without a device.
		return bus < AudioBus::Count ? s_Audio.BusVolume[(size_t)bus] : 1.0f;
	}

	void AudioEngine::SetListener(const glm::vec3& position, const glm::vec3& forward,
								  const glm::vec3& up)
	{
		if (!s_Audio.Available)
			return;

		ma_engine_listener_set_position(&s_Audio.Engine, 0, position.x, position.y, position.z);

		// A zero-length direction would leave miniaudio normalising a zero
		// vector, and every sound panned by a NaN is silent in a way that looks
		// like a missing file.
		if (glm::dot(forward, forward) > 1e-8f)
		{
			const glm::vec3 f = glm::normalize(forward);
			ma_engine_listener_set_direction(&s_Audio.Engine, 0, f.x, f.y, f.z);
		}

		if (glm::dot(up, up) > 1e-8f)
		{
			const glm::vec3 u = glm::normalize(up);
			ma_engine_listener_set_world_up(&s_Audio.Engine, 0, u.x, u.y, u.z);
		}
	}

	void AudioEngine::Update()
	{
		for (auto it = s_Audio.Voices.begin(); it != s_Audio.Voices.end(); )
		{
			// A looping voice never ends on its own; it is stopped or it is
			// not. Retiring by "at end" would silently kill it at the first
			// wrap.
			if (it->second.Loop)
			{
				++it;
				continue;
			}

			// With no device there is nothing to have played, so a one-shot is
			// over as soon as the frame that started it is. That keeps a scene
			// full of fire-and-forget sounds from growing without bound on a
			// machine that cannot hear them.
			const bool finished = !it->second.Sound ||
								  ma_sound_at_end(it->second.Sound.get()) == MA_TRUE;

			if (!finished)
			{
				++it;
				continue;
			}

			if (it->second.Sound)
				ma_sound_uninit(it->second.Sound.get());

			it = s_Audio.Voices.erase(it);
		}
	}

	size_t AudioEngine::GetVoiceCount()
	{
		return s_Audio.Voices.size();
	}
}
