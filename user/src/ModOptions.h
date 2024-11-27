#pragma once

#include <mallow/config.hpp>

struct ModOptions : public mallow::config::ConfigBase{
    bool raindowSpin;
    char spinButton;

    void read(const ArduinoJson::JsonObject &config) override {
        mallow::config::ConfigBase::read(config);
        raindowSpin = config["rainbowSpin"] | false;
        if(!config["spinButton"].is<const char*>()){
            spinButton = 'Y';
            return;
        }
        const char* buttonStr = config["spinButton"];
        spinButton = buttonStr[0];
    }
};
