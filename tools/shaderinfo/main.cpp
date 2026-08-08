// Offline shader inspection: compiles a .rvshader through the same path the
// engine uses, then prints the recovered reflection and the GLSL SPIRV-Cross
// generates for the OpenGL backend. Useful for checking binding layouts and
// vertex strides without launching the editor.
//
//   shaderinfo [path/to/shader.rvshader]
#include <rvpch.h>
#include "RageV/Renderer/RHI/ShaderCompiler.h"
#include "RageV/Core/Log.h"

using namespace RageV;
using namespace RageV::RHI;

static const char* TypeName(ResourceType t)
{
    switch (t)
    {
        case ResourceType::UniformBuffer:        return "UniformBuffer";
        case ResourceType::StorageBuffer:        return "StorageBuffer";
        case ResourceType::CombinedImageSampler: return "CombinedImageSampler";
        case ResourceType::StorageImage:         return "StorageImage";
    }
    return "?";
}

static const char* FormatName(Format f)
{
    switch (f)
    {
        case Format::R32_SFLOAT:          return "R32_SFLOAT";
        case Format::R32G32_SFLOAT:       return "R32G32_SFLOAT";
        case Format::R32G32B32_SFLOAT:    return "R32G32B32_SFLOAT";
        case Format::R32G32B32A32_SFLOAT: return "R32G32B32A32_SFLOAT";
        default:                          return "other";
    }
}

int main(int argc, char** argv)
{
    Log::Init();
    ShaderCompiler::Init();

    const char* path = argc > 1 ? argv[1] : "assets/shaders/quad.rvshader";
    auto compiled = ShaderCompiler::CompileFromFile(path);
    if (!compiled)
    {
        RV_CORE_ERROR("COMPILE FAILED");
        return 1;
    }

    RV_CORE_INFO("Shader '{0}' compiled, {1} stage(s)", compiled->Name, compiled->Stages.size());
    for (const auto& stage : compiled->Stages)
        RV_CORE_INFO("  stage {0}: {1} SPIR-V words", ShaderStageName(stage.Stage), stage.SpirV.size());

    const auto& reflection = compiled->Reflection;
    RV_CORE_INFO("Resource sets: {0}", reflection.Sets.size());
    for (const auto& set : reflection.Sets)
    {
        RV_CORE_INFO("  set {0}:", set.Set);
        for (const auto& binding : set.Bindings)
        {
            RV_CORE_INFO("    binding {0} '{1}' type={2} count={3} blockSize={4} stages={5}{6}",
                binding.Binding, binding.Name, TypeName(binding.Type), binding.Count, binding.BlockSize,
                HasFlag(binding.Stages, ShaderStage::Vertex) ? "V" : "",
                HasFlag(binding.Stages, ShaderStage::Fragment) ? "F" : "");
        }
    }

    RV_CORE_INFO("Vertex layout: stride {0}, {1} attribute(s)",
        reflection.VertexInput.Bindings.empty() ? 0 : reflection.VertexInput.Bindings[0].Stride,
        reflection.VertexInput.Attributes.size());
    for (const auto& attribute : reflection.VertexInput.Attributes)
        RV_CORE_INFO("  location {0} offset {1} {2}", attribute.Location, attribute.Offset, FormatName(attribute.Format));

    RV_CORE_INFO("Push constant ranges: {0}", reflection.PushConstants.size());

    for (const auto& stage : compiled->Stages)
    {
        auto glsl = ShaderCompiler::CrossCompileToGLSL(stage, 450);
        if (!glsl)
        {
            RV_CORE_ERROR("CROSS-COMPILE FAILED for {0}", ShaderStageName(stage.Stage));
            return 1;
        }
        RV_CORE_INFO("--- GLSL for {0} ({1} bytes) ---\n{2}", ShaderStageName(stage.Stage), glsl->size(), *glsl);
    }

    ShaderCompiler::Shutdown();
    RV_CORE_INFO("OK");
    return 0;
}
