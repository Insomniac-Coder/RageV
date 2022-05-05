workspace "RageV"
    architecture "x64"
    configurations{
        "Debug",
        "Release",
        "Dist"
    }

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

project "RageV"
    location "RageV"
    kind "SharedLib"
    language "C++"

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
        "%{prj.name}/src"
    }

    filter "system:windows"
        cppdialect "C++17"
        staticruntime "On"
        systemversion "latest"

        defines
        {
            "RV_PLATFORM_WINDOWS",
            "RV_BUILD_DLL",
            "_WINDLL"
        }

        postbuildcommands
        {
            ("{COPY} %{cfg.buildtarget.relpath} ../bin/" .. outputdir .. "/Sandbox")
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
    
project "Sandbox"
    location "Sandbox"
    kind "ConsoleApp"
    language "C++"   

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
        staticruntime "On"
        systemversion "latest"

        defines
        {
            "RV_PLATFORM_WINDOWS",
            "_WINDLL"
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