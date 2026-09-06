workspace("otb_parsers")
configurations({ "Debug", "Release" })
cppdialect("C++23")
symbols("On")

targetdir("bin/%{cfg.buildcfg}")
objdir("obj/%{cfg.buildcfg}")

filter("configurations:Debug")
defines({ "DEBUG" })
runtime("Debug")
filter("configurations:Release")
defines({ "NDEBUG" })
optimize("On")
runtime("Release")
filter({}) -- Reset filter

-- =============================================================================
-- 1. Static Library (Produces .lib on Windows, .a on Linux/macOS)
-- =============================================================================
project("lib")
kind("StaticLib")
language("C++")
files({ "src/*.hpp" })
includedirs({ "src" })

-- =============================================================================
-- 2. Shared Library (Produces .dll on Windows, .so on Linux/macOS)
-- =============================================================================
project("so")
kind("SharedLib")
language("C++")
files({ "src/*.hpp" })
includedirs({ "src" })

filter("system:windows")
defines({ "OTBM_BUILD_DLL" })
filter({})

project("test")
kind("ConsoleApp")
language("C++")
files({
	"tests/tests.cpp",
	"tests/ut/*.test.hpp",
	"tests/it/*.test.hpp",
	"tests/fixture/*",
})
includedirs({ "src", "tests" })
links({ "lib" })

postbuildcommands({ '"%{cfg.buildtarget.abspath}"' })

-- =============================================================================
-- Custom Premake Action: `premake5 test`
-- =============================================================================
newaction({
	trigger = "test",
	description = "Generate build files, compile, and run all tests automatically",
	execute = function()
		-- 1. Generate native build files
		if os.target() == "windows" then
			premake.action.call("vs2022")
		else
			premake.action.call("gmake2")
		end

		-- 2. Build and Run the 'test' target
		if os.target() == "windows" then
			os.execute("msbuild otb_parsers.sln /p:Configuration=Debug /p:Platform=x64 /t:test")
		else
			os.execute("make -j4 config=debug_x64")
		end
	end,
})
