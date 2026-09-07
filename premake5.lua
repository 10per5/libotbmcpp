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
filter({})

project("lib")
kind("StaticLib")
language("C++")
files({ "src/*.hpp" })
includedirs({ "src" })

project("so")
kind("SharedLib")
language("C++")
files({ "src/*.hpp" })
includedirs({ "src" })
targetname("otbmcpp")
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

-- Example project (uses shared library)
project("example")
kind("ConsoleApp")
language("C++")
files({ "examples/analyze-otbm.cpp" })
includedirs({ "src" })
links({ "so" })

filter("system:linux or system:bsd")
linkoptions({ "-Wl,-rpath,'$$ORIGIN'" })
filter("system:macosx")
linkoptions({ "-Wl,-rpath,@executable_path" })
filter({})

-- Custom action: Build and run the example
newaction({
	trigger = "example",
	description = "Build and run the fixture generator",
	execute = function()
		if os.target() == "windows" then
			premake.action.call("vs2022")
		else
			premake.action.call("gmake2")
		end

		if os.target() == "windows" then
			os.execute("msbuild otb_parsers.sln /p:Configuration=Debug /p:Platform=x64 /t:example")
			os.execute("bin\\Debug\\example.exe")
		else
			os.execute("make -j4 config=debug_x64 example")
			os.execute("./bin/Debug/example")
		end
	end,
})

-- Custom action: Generate fixture, build, and run tests
newaction({
	trigger = "test",
	description = "Generate fixture, build, and run all tests",
	execute = function()
		-- Generate fixture first
		premake.action.call("example")

		-- Then build and run tests
		if os.target() == "windows" then
			os.execute("msbuild otb_parsers.sln /p:Configuration=Debug /p:Platform=x64 /t:test")
		else
			os.execute("make -j4 config=debug_x64 test")
		end
	end,
})
