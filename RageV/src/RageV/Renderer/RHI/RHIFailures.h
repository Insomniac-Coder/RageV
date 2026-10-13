#pragma once
#include "ShaderCompiler.h"
#include <cstdint>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace RageV::RHI
{
	// **RT2-1: what went wrong in the GPU layer this run, counted and named.**
	//
	// A shader that failed to compile is ShaderCompiler's own count. This holds
	// the rest: a pipeline the driver did not build, a descriptor refused because
	// the shader declares no such binding, a push larger than the shader's block.
	// Each distinct problem (its key) is recorded once, so a pass that repeats
	// the mistake every frame is one entry, not thousands. Measuring runs exit 3
	// on any of them (Entrypoint.h), and the HUD names them, because the picture
	// such a run makes is not the picture the engine meant to make.
	//
	// Thread safe: pipelines are built on worker threads (RT2-9).
	class Failures
	{
	public:
		// True the first time `key` is seen -- the caller logs `message` then.
		static bool Record(const std::string& key, const std::string& message)
		{
			std::lock_guard<std::mutex> lock(Mutex());
			if (!Keys().insert(key).second)
				return false;
			Messages().push_back(message);
			return true;
		}

		static uint32_t Count()
		{
			std::lock_guard<std::mutex> lock(Mutex());
			return (uint32_t)Keys().size();
		}

		// Every recorded message, in order: the banner's lines.
		static std::vector<std::string> List()
		{
			std::lock_guard<std::mutex> lock(Mutex());
			return Messages();
		}

		// Everything that went wrong this run: shaders that did not compile
		// (ShaderCompiler's count) and the rest above. What a measurement checks
		// before it writes a picture or a number (RT2-1).
		static uint32_t Total() { return ShaderCompiler::FailureCount() + Count(); }
		static std::string First()
		{
			if (ShaderCompiler::FailureCount() > 0)
				return "shader '" + std::string(ShaderCompiler::FirstFailure()) + "' did not compile";
			const std::vector<std::string> list = List();
			return list.empty() ? std::string() : list.front();
		}

	private:
		static std::mutex& Mutex() { static std::mutex mutex; return mutex; }
		static std::set<std::string>& Keys() { static std::set<std::string> keys; return keys; }
		static std::vector<std::string>& Messages() { static std::vector<std::string> messages; return messages; }
	};
}
