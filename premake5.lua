workspace "RageV"
    architecture "x64"
    configurations{
        "Debug",
        "Release",
        "Dist"
    }
    startproject "RageVEditor"

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

IncludeDir = {}
IncludeDir['GLFW'] = "RageV/vendor/GLFW/include"
IncludeDir['GLAD'] = "RageV/vendor/GLAD/include"
IncludeDir['ImGui'] = "RageV/vendor/imgui"
IncludeDir['glm'] = "RageV/vendor/glm"
IncludeDir['stb_image'] = "RageV/vendor/stb_image"
IncludeDir['EnTT'] = "RageV/vendor/EnTT/include"

group "Dependencies"
    include "RageV/vendor/imgui"
    include "RageV/vendor/GLAD"

group ""

project "RageV"
    location "RageV"
    kind "StaticLib"
    language "C++"
    cppdialect "C++17"
    staticruntime "on"

    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir("bin-int/" .. outputdir .. "/%{prj.name}")

    pchheader "rvpch.h"
    pchsource "RageV/src/rvpch.cpp"

    files
    {
        "%{prj.name}/src/**.h",
        "%{prj.name}/src/**.cpp",
        "%{prj.name}/vendor/stb_image/**.h",
        "%{prj.name}/vendor/stb_image/**.cpp",
        "%{prj.name}/vendor/glm/glm/**.hpp",
        "%{prj.name}/vendor/glm/glm/**.inl"
    }

    includedirs
    {
        "%{prj.name}/vendor/spdlog/include",
        "%{IncludeDir.GLFW}",
        "%{IncludeDir.GLAD}",
        "%{IncludeDir.ImGui}",
        "%{IncludeDir.glm}",
        "%{IncludeDir.stb_image}",
        "%{prj.name}/src",
        "%{IncludeDir.EnTT}"
    }

    libdirs
    {
        "%{prj.name}/vendor/GLFW/lib-vc2022"
    }

    links
    {
        "GLAD",
        "ImGui",
        "glfw3_mt.lib",
        "opengl32.lib"
    }

    filter "system:windows"
        systemversion "latest"

        defines
        {
            "RV_PLATFORM_WINDOWS",
            "RV_BUILD_DLL"
        }

    filter  "configurations:Debug"
        defines "RV_DEBUG"
        runtime "Debug"
        symbols "on"

    filter "configurations:Release"
        defines "RV_RELEASE"
        runtime "Release"
        optimize "on"

    filter "configurations:Dist"
        defines "RV_DIST"
        runtime "Release"
        optimize "on"
    
project "Sandbox"
    location "Sandbox"
    kind "ConsoleApp"
    language "C++"   
    cppdialect "C++17"
    staticruntime "on"

    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir("bin-int/" .. outputdir .. "/%{prj.name}")

    files
    {
        "%{prj.name}/src/**.h",
        "%{prj.name}/src/**.cpp"
    }

    includedirs
    {
        "RageV/vendor/spdlog/include",
        "RageV/src",
        "%{IncludeDir.glm}",
        "%{IncludeDir.ImGui}",
        "%{IncludeDir.EnTT}"
    }

    links {
        "RageV"
    }

    filter "system:windows"
        systemversion "latest"

        defines
        {
            "RV_PLATFORM_WINDOWS"
        }

    filter  "configurations:Debug"
        defines "RV_DEBUG"
        runtime "Debug"
        symbols "on"

    filter "configurations:Release"
        defines "RV_RELEASE"
        runtime "Release"
        optimize "on"

    filter "configurations:Dist"
        defines "RV_DIST"
        runtime "Release"
        optimize "on"

project "RageVEditor"
    location "RageVEditor"
    kind "ConsoleApp"
    language "C++"   
    cppdialect "C++17"
    staticruntime "on"

    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir("bin-int/" .. outputdir .. "/%{prj.name}")

    files
    {
        "%{prj.name}/src/**.h",
        "%{prj.name}/src/**.cpp"
    }

    includedirs
    {
        "RageV/vendor/spdlog/include",
        "RageV/src",
        "%{IncludeDir.glm}",
        "%{IncludeDir.ImGui}",
        "%{IncludeDir.EnTT}"
    }

    links {
        "RageV"
    }

    filter "system:windows"
        systemversion "latest"

        defines
        {
            "RV_PLATFORM_WINDOWS"
        }

    filter  "configurations:Debug"
        defines "RV_DEBUG"
        runtime "Debug"
        symbols "on"

    filter "configurations:Release"
        defines "RV_RELEASE"
        runtime "Release"
        optimize "on"

    filter "configurations:Dist"
        defines "RV_DIST"
        runtime "Release"
        optimize "on"