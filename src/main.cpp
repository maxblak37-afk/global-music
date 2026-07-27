#include <Geode/Geode.hpp>
#include <Geode/modify/FMODAudioEngine.hpp>
#include <filesystem>

using namespace geode::prelude;

class $modify(GlobalMusicPlayer, FMODAudioEngine) {
    void playMusic(gd::string path, bool loop, float fade, int p3) {
        bool isReplacementEnabled = Mod::get()->getSettingValue<bool>("enable-replacement");
        
        if (isReplacementEnabled) {
            auto customPath = Mod::get()->getSettingValue<std::filesystem::path>("music-path");
            
            // Check if the file is configured and actually exists on disk
            if (!customPath.empty() && std::filesystem::exists(customPath)) {
                // Replace the requested path with the custom MP3
                path = customPath.string();
                
                // Apply custom fade duration if configured
                float customFade = Mod::get()->getSettingValue<double>("fade-duration");
                fade = customFade;
                
                // Note: Modifying background volume here overrides GD's settings, 
                // but we keep it since it was a requested feature.
                // It might be better to multiply it, but this keeps it simple.
            }
        }
        
        // Let the original function handle playing the music with our replaced path and fade
        FMODAudioEngine::playMusic(path, loop, fade, p3);
    }
};