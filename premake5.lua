workspace("otb_parsers")
configurations({ "Release", "Debug" })
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

-- Example project (uses shared library): ./example analyze <file> | ./example generate
project("example")
kind("ConsoleApp")
language("C++")
files({ "examples/example-main.cpp", "examples/analyze-otbm.cpp", "examples/generate-otbm.cpp" })
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
			os.execute("msbuild otb_parsers.sln /p:Configuration=Release /p:Platform=x64 /t:example")
			os.execute("bin\\Release\\example.exe generate")
		else
			os.execute("make -j4 config=release_x64 example")
			os.execute("./bin/Release/example generate")
		end
	end,
})

-- Custom action: Generate fixture, build, and run tests
newaction({
	trigger = "test",
	description = "Generate fixture, build, and run all tests",
	execute = function()
		-- (Re)generate the Makefiles for every project. premake.action.call
		-- only works for built-in actions, so we call gmake2 directly here
		-- rather than nesting into the "example" custom action (which trips
		-- a nil-index error in premake's action.lua).
		premake.action.call("gmake2")

		-- Build the fixture generator and (re)generate tests/fixture/world.otbm.
		os.execute("make -j4 config=release_x64 example")
		os.execute("./bin/Release/example generate")

		-- Build and run the tests.
		if os.target() == "windows" then
			os.execute("msbuild otb_parsers.sln /p:Configuration=Release /p:Platform=x64 /t:test")
		else
			os.execute("make -j4 config=release_x64 test")
		end
	end,
})
