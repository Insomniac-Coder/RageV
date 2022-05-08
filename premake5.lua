workspace "RageV"
    architecture "x64"
    configurations{
        "Debug",
        "Release",
        "Dist"
    }
    startproject "Sandbox"

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

IncludeDir = {}
IncludeDir['GLFW'] = "RageV/vendor/GLFW/include"
IncludeDir['GLAD'] = "RageV/vendor/GLAD/include"
IncludeDir['ImGui'] = "RageV/vendor/imgui"

group "Dependencies"
    include "RageV/vendor/imgui"
    include "RageV/vendor/GLAD"

group ""

project "RageV"
    location "RageV"
    kind "SharedLib"
    language "C++"
    staticruntime "off"

    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir("bin-int/" .. outputdir .. "/%{prj.name}")

    pchheader "rvpch.h"
    pchsource "RageV/src/rvpch.cpp"

    files
    {
        "%{prj.name}/src/**.h",
        "%{prj.name}/src/**.cpp"
    }

    includedirs
    {
        "%{prj.name}/vendor/spdlog/include",
        "%{IncludeDir.GLFW}",
        "%{IncludeDir.GLAD}",
        "%{IncludeDir.ImGui}",
        "%{prj.name}/src"
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
        cppdialect "C++17"
        systemversion "latest"

        defines
        {
            "RV_PLATFORM_WINDOWS",
            "RV_BUILD_DLL"
        }

        postbuildcommands
        {
            ("{COPY} %{cfg.buildtarget.relpath} \"../bin/" .. outputdir .. "/Sandbox/\"")
        }

    filter  "configurations:Debug"
        defines "RV_DEBUG"
        runtime "Debug"
        symbols "On"

    filter "configurations:Release"
        defines "RV_RELEASE"
        runtime "Release"
        optimize "On"

    filter "configurations:Dist"
        defines "RV_DIST"
        runtime "Release"
        optimize "On"
    
project "Sandbox"
    location "Sandbox"
    kind "ConsoleApp"
    language "C++"   
    staticruntime "off"

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
        "RageV/src"
    }

    links {
        "RageV"
    }

    filter "system:windows"
        cppdialect "C++17"
        systemversion "latest"

        defines
        {
            "RV_PLATFORM_WINDOWS"
        }

    filter  "configurations:Debug"
        defines "RV_DEBUG"
        symbols "On"

    filter "configurations:Release"
        defines "RV_RELEASE"
        optimize "On"

    filter "configurations:Dist"
        defines "RV_DIST"
        optimize "On"