#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/loader/Log.hpp>
#include <filesystem>
#include <cmath>

using namespace geode::prelude;

static std::string pathToUtf8(std::filesystem::path const& p) {
    auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}

static bool isReplacementEnabled() {
    return Mod::get()->getSettingValue<bool>("enable-replacement");
}

static std::filesystem::path getMusicPath() {
    return Mod::get()->getSettingValue<std::filesystem::path>("music-path");
}

static float getFadeDuration() {
    return Mod::get()->getSettingValue<double>("fade-duration");
}

static float getVolume() {
    return Mod::get()->getSettingValue<double>("music-volume");
}

static bool isScreenPulseEnabled() {
    return Mod::get()->getSettingValue<bool>("screen-pulse");
}

static bool isSideWavesEnabled() {
    return Mod::get()->getSettingValue<bool>("side-waves");
}

static void forcePlayCustomMusic(float fade) {
    if (!isReplacementEnabled()) return;

    auto musicPath = getMusicPath();
    if (musicPath.empty() || !std::filesystem::exists(musicPath)) return;

    auto utf8Path = pathToUtf8(musicPath);
    auto fmodEngine = FMODAudioEngine::sharedEngine();

    fmodEngine->stopAllMusic(false);
    fmodEngine->setBackgroundMusicVolume(getVolume());
    fmodEngine->playMusic(utf8Path, true, fade, 0);
}

class AudioVisualizerNode : public CCLayer {
protected:
    CCDrawNode* m_waves = nullptr;
    CCLayerColor* m_pulseOverlay = nullptr;
    float m_smoothedLevel = 0.f;
    float m_wavePhase = 0.f;

public:
    static AudioVisualizerNode* create() {
        auto ret = new AudioVisualizerNode();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    bool init() override {
        if (!CCLayer::init()) return false;

        auto fmodEngine = FMODAudioEngine::sharedEngine();
        fmodEngine->enableMetering();

        m_waves = CCDrawNode::create();
        this->addChild(m_waves, 100);

        auto winSize = CCDirector::sharedDirector()->getWinSize();
        m_pulseOverlay = CCLayerColor::create({255, 255, 255, 0}, winSize.width, winSize.height);
        m_pulseOverlay->setPosition({0, 0});
        this->addChild(m_pulseOverlay, 200);

        this->setTouchEnabled(false);
        this->scheduleUpdate();

        return true;
    }

    void update(float dt) override {
        auto fmodEngine = FMODAudioEngine::sharedEngine();
        float level = fmodEngine->getMeteringValue();

        m_smoothedLevel += (level - m_smoothedLevel) * 0.3f;
        m_wavePhase += dt * 4.f;

        auto winSize = CCDirector::sharedDirector()->getWinSize();

        if (isSideWavesEnabled()) {
            m_waves->clear();
            drawSideWave(winSize, true);
            drawSideWave(winSize, false);
        } else {
            m_waves->clear();
        }

        if (isScreenPulseEnabled()) {
            // Лёгкая вспышка яркости в такт басу вместо искажения геймплея
            float alpha = m_smoothedLevel * 40.f; // максимум ~40/255 прозрачности
            alpha = fmaxf(0.f, fminf(40.f, alpha));
            m_pulseOverlay->setOpacity((GLubyte)alpha);
        } else {
            m_pulseOverlay->setOpacity(0);
        }
    }

    void drawSideWave(CCSize const& winSize, bool leftSide) {
        int segments = 40;
        float segmentHeight = winSize.height / segments;
        ccColor4F color = { 0.2f, 0.7f, 1.f, 0.45f };

        std::vector<CCPoint> points;

        // Базовый край (у стенки экрана)
        float edgeX = leftSide ? 0.f : winSize.width;
        float direction = leftSide ? 1.f : -1.f;

        // Верхняя точка (край экрана)
        points.push_back({ edgeX, winSize.height });

        // Идём вниз по экрану, формируя плавную волнистую кривую
        for (int i = 0; i <= segments; i++) {
            float y = winSize.height - i * segmentHeight;
            float t = (float)i / segments;

            // Плавная синусоида, промодулированная громкостью баса
            float wave = sinf(t * 10.f + m_wavePhase) * 0.5f + 0.5f;
            float amplitude = 6.f + m_smoothedLevel * 18.f; // поменьше, чем раньше
            float x = edgeX + direction * (amplitude * wave);

            points.push_back({ x, y });
        }

        points.push_back({ edgeX, 0.f });

        // Рисуем как единый залитый многоугольник — цельная волнистая полоса
        m_waves->drawPolygon(
            points.data(), (int)points.size(),
            color, 0.f, color
        );
    }
};

class $modify(PlayLayer) {
    struct Fields {
        bool m_musicStarted = false;
        float m_elapsed = 0.f;
        AudioVisualizerNode* m_visualizer = nullptr;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontRunActions) {
        if (!PlayLayer::init(level, useReplay, dontRunActions)) return false;

        if (isReplacementEnabled() && (isSideWavesEnabled() || isScreenPulseEnabled())) {
            auto fields = m_fields.self();
            fields->m_visualizer = AudioVisualizerNode::create();
            this->addChild(fields->m_visualizer, 1000);
        }

        return true;
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        auto fields = m_fields.self();
        if (fields->m_musicStarted) return;
        if (!isReplacementEnabled()) return;

        fields->m_elapsed += dt;

        if (fields->m_elapsed >= 1.0f) {
            fields->m_musicStarted = true;
            forcePlayCustomMusic(getFadeDuration());
        }
    }

    void resetLevel() {
        PlayLayer::resetLevel();

        if (!isReplacementEnabled()) return;

        auto musicPath = getMusicPath();
        if (musicPath.empty() || !std::filesystem::exists(musicPath)) return;

        auto utf8Path = pathToUtf8(musicPath);
        auto fmodEngine = FMODAudioEngine::sharedEngine();

        if (!fmodEngine->isMusicPlaying(utf8Path, 0)) {
            forcePlayCustomMusic(0.0f);
        }
    }

    void onQuit() {
        if (isReplacementEnabled()) {
            FMODAudioEngine::sharedEngine()->fadeOutMusic(getFadeDuration(), 0);
        }
        PlayLayer::onQuit();
    }

    void pauseGame(bool pause) {
        PlayLayer::pauseGame(pause);

        if (!isReplacementEnabled()) return;

        auto fmodEngine = FMODAudioEngine::sharedEngine();
        if (pause) {
            fmodEngine->pauseMusic(0);
        } else {
            fmodEngine->resumeMusic(0);
        }
    }
};

class $modify(PauseLayer) {
    void onResume(CCObject* sender) {
        PauseLayer::onResume(sender);

        if (isReplacementEnabled()) {
            FMODAudioEngine::sharedEngine()->resumeMusic(0);
        }
    }
};