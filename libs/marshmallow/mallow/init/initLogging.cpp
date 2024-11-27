#include <mallow/init/initLogging.hpp>
#include <mallow/hook/helpers.hpp>
#include <mallow/config.hpp>
#include <mallow/logging/logSinks.hpp>
#include <mallow/logging/logger.hpp>

namespace mallow::init {

void setupLogging(){
    using namespace log::sink;
    // This sink writes to a file on the SD card.
    auto* config = config::getConfig();
    if(!config || !config->enableLogger)
        return;
    static FileSink fileSink = FileSink("sd:/mallow.log");
    addLogSink(&fileSink);

    if (config->loggerIP) {
        static NetworkSink networkSink = NetworkSink(
            config->loggerIP,
            config->loggerPort,
            config->tryReconnectLogger
        );
        if (networkSink.isSuccessfullyConnected() || config->tryReconnectLogger)
            addLogSink(&networkSink);
        else
            log::logLine("Failed to connect to the network sink");
    }
    if(config::isEmu()){
        static DebugPrintSink debugPrintSink = DebugPrintSink();
        addLogSink(&debugPrintSink);
    }
}

struct nnMainHook : public mallow::hook::Trampoline<nnMainHook>{
    static void Callback(){
        nn::fs::MountSdCardForDebug("sd");
        if(!mallow::config::loadConfig(true)){
            config::useDefaultConfig();
            config::saveConfig();
        }
        config::readConfigToStruct();

        setupLogging();
        log::logLine("Logging and config set up!");
        Orig();
    }
};

void installHooks(){
    nnMainHook::InstallAtSymbol("nnMain");
}

}
