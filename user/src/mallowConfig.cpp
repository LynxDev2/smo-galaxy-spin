#include <mallow/mallow.hpp>
#include <nn/fs.h>
#include <nn/fs/fs_directories.h>
#include <nn/fs/fs_types.h>

#include "ModOptions.h"

namespace mallow::config {
    const char* path = "sd:/atmosphere/contents/0100000000010000/GalaxySpin_config.json";
    const char* pathEmu = "sd:/GalaxySpin_config.json";
    const char* defaultConfig = R"(
{
    "spinButton": "Y",
}
    )";

    Allocator* getAllocator() {
        static DefaultAllocator allocator = {};
        return &allocator;
    }
    ConfigBase* getConfig() {
        static ModOptions modConfig = {};
        return &modConfig;
    }
    bool isEmu(){
        nn::fs::DirectoryEntryType type;
        nn::fs::GetEntryType(&type, "sd:/atmosphere/package3");
        return type != nn::fs::DirectoryEntryType_File;
    }
}
