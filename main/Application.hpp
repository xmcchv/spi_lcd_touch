#ifndef APPLICATION_HPP
#define APPLICATION_HPP

#include "UIManager.hpp"
#include "WiFiManager.hpp"
#include "JokeService.hpp"
#include "Setting.hpp"
#include "MicrophoneService.hpp"

class Application {
public:
    static Application& getInstance();
    
    bool initialize();
    void run();
    
private:
    Application() = default;
    ~Application() = default;
    
    UIManager& ui_manager_ = UIManager::getInstance();
    WiFiManager& wifi_manager_ = WiFiManager::getInstance();
    JokeService& joke_service_ = JokeService::getInstance();
    Setting& setting_ = Setting::getInstance();
    MicrophoneService& microphone_service_ = MicrophoneService::getInstance();
    
    void setupEventHandlers();
};

#endif // APPLICATION_HPP