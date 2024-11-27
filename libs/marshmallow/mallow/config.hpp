#pragma once

#include <ArduinoJson.hpp>
#include <mallow/alloc.hpp>
#include <exl/types.h>

// For reading from the config, look at Chapter 3.3, 3.4, 3.6, 3.7:
// https://files.arduinojson.org/v7/deserialization_tutorial.pdf
// For writing to the config, look at Chapter 4.2, 4.3, 4.6, 4.7
// https://files.arduinojson.org/v7/serialization_tutorial.pdf
namespace mallow::config {
    // externs are defined in user/src/mallowConfig.cpp

    struct ConfigBase{
        bool enableLogger;
        bool tryReconnectLogger;
        const char* loggerIP;
        u16 loggerPort;

        virtual void read(const ArduinoJson::JsonObject& config);
    };

    extern const char* path;
    extern const char* pathEmu;
    extern const char* defaultConfig;
    extern Allocator* getAllocator();
    extern ConfigBase* getConfig();
    extern bool isEmu();

    template<typename T>
    T* getConfg(){
        return reinterpret_cast<T*>(getConfig());
    }

    const char* calcConfigPath();

    // If loading fails once, it will not retry unless you pass true to this function.
    bool loadConfig(bool retry);
    bool saveConfig();
    bool isLoadedConfig();
    bool useDefaultConfig();
    bool readConfigToStruct();

    // If loading fails, it will return an empty JsonObject.
    //WARNING: Deprecated
    ArduinoJson::JsonObject getConfigJson();
}  // namespace mallow::config
