#include <hex/plugin.hpp>
#include <hex/api/content_registry.hpp>
#include <hex/api/imhex_api.hpp>
#include <hex/api/event_manager.hpp>
#include <hex/api/events/events_interaction.hpp>
#include <hex/providers/provider.hpp>
#include <hex/helpers/logger.hpp>

#include <chrono>
#include <mutex>
#include <thread>
#include <fstream>
#include <filesystem>

using namespace hex;

namespace hex::plugin::auto_reload {

    namespace {
        static bool s_autoReloadEnabled = false;
        static int s_reloadIntervalMs = 100;
        // Mutex to protect multiple reloads
        static std::mutex s_reloadMutex;
        
        // Retrieve the provider's filepath
        std::optional<std::string> getProviderFilePath(prv::Provider* provider) {
            if (provider == nullptr)
                return std::nullopt;
                
            try {
                // Try to get the file path from the provider description
                auto descriptions = provider->getDataDescription();
                for (const auto& desc : descriptions) {
                    if (desc.name.find("path") != std::string::npos) {
                        return desc.value;
                    }
                }
            } catch (...) {
                // Ignore errors
            }
            
            return std::nullopt;
        }
        
        void autoReloadService() {
            // Sleep for configured interval
            std::this_thread::sleep_for(std::chrono::milliseconds(s_reloadIntervalMs));
            
            // Only reload if the feature is enabled
            if (!s_autoReloadEnabled)
                return;
                
            // Prevent multiple reloads at once. Never happens, but just in case
            if (!s_reloadMutex.try_lock())
                return;
            
            // Get the current provider and check if it's valid
            auto provider = ImHexApi::Provider::get();
            if (provider == nullptr || !provider->isAvailable()) {
                s_reloadMutex.unlock();
                return;
            }
            
            try {
                // Get the file path from the provider
                auto filePath = getProviderFilePath(provider);
                if (!filePath) {
                    log::debug("Could not determine file path");
                    s_reloadMutex.unlock();
                    return;
                }
                
                // Store provider state
                auto baseAddress = provider->getBaseAddress();
                auto currentPage = provider->getCurrentPage();
                
                // Read the file directly as a binary file
                std::ifstream file(*filePath, std::ios::binary);
                if (!file.good()) {
                    log::error("Failed to open file for reading");
                    s_reloadMutex.unlock();
                    return;
                }
                
                // Get file size after opening
                auto fileSize = std::filesystem::file_size(std::filesystem::path(*filePath));
                
                // Read the entire file into a buffer
                std::vector<u8> buffer(fileSize);
                file.read(reinterpret_cast<char*>(buffer.data()), fileSize);
                file.close();
                
                // Resize the provider to match the file size
                if (provider->isResizable() && provider->getActualSize() != fileSize) {
                    provider->resizeRaw(fileSize);
                }
                
                // Write the data directly to the provider using writeRaw instead of write
                // This bypasses the patching system and writes directly to the data
                if (provider->isWritable() && fileSize > 0) {
                    provider->writeRaw(0, buffer.data(), buffer.size());
                }
                
                // Clear the edit flag to prevent red highlighting
                provider->markDirty(false);
                
                // Restore state
                provider->setBaseAddress(baseAddress);
                provider->setCurrentPage(currentPage);
                
                // Post data changed event to update UI
                EventManager::post<EventDataChanged>(provider);
            } catch (const std::exception& e) {
                log::error("Failed to reload file: {}", e.what());
            } catch (...) {
                log::error("Failed to reload file with unknown error");
            }
            
            s_reloadMutex.unlock();
        }
    }

}

using namespace hex::plugin::auto_reload;

IMHEX_PLUGIN_SETUP("Auto Reload", "stableversion", "Fast auto reload!") {
    
    ContentRegistry::BackgroundServices::registerService("hex.builtin.background_service.auto_reload", autoReloadService);
    
    ContentRegistry::Interface::addMenuItem(
        { "hex.builtin.menu.extras", "Auto Reload" }, 
        3500,
        Shortcut::None,
        [] {
            s_autoReloadEnabled = !s_autoReloadEnabled;
            log::info("Auto Reload toggled: {}", s_autoReloadEnabled ? "enabled" : "disabled");
        },
        [] { return true; },
        [] { return s_autoReloadEnabled; }
    );
} 
