from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, CMakeDeps

class WordSorterConan(ConanFile):
    name = "word_sorter"
    version = "1.0"
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain"

    def requirements(self):
        self.requires("lz4/1.9.4")
        self.requires("cli11/2.4.2")

    def build_requirements(self):
        self.tool_requires("cmake/3.25.3")
        self.tool_requires("ninja/1.11.1")

    def layout(self):
        self.folders.build = "build"

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()