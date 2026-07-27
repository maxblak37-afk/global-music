#include <Geode/Geode.hpp>
#include <Geode/modify/FMODAudioEngine.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/UILayer.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <filesystem>
#include <cmath>
#include <cstdlib>

using namespace geode::prelude;

// ========================================================
//  MUSIC REPLACEMENT
// ========================================================
class $modify(GlobalMusicPlayer, FMODAudioEngine) {
    bool shouldReplace(const gd::string& path) {
        if (!Mod::get()->getSettingValue<bool>("enable-replacement")) return false;
        bool isMenu = (path.find("menuLoop") != gd::string::npos);
        if (isMenu && !Mod::get()->getSettingValue<bool>("replace-menu-music")) return false;
        return true;
    }

    gd::string getCustomStr() {
        auto customPath = Mod::get()->getSettingValue<std::filesystem::path>("music-path");
        if (customPath.empty() || !std::filesystem::exists(customPath)) return "";
        std::string utf8(reinterpret_cast<const char*>(customPath.u8string().c_str()));
        return gd::string(utf8.c_str());
    }

    void playMusic(gd::string path, bool loop, float fade, int p3) {
        if (shouldReplace(path)) {
            gd::string s = getCustomStr();
            if (!s.empty()) {
                float f = Mod::get()->getSettingValue<double>("fade-duration");
                FMODAudioEngine::loadMusic(s, 1.f, 0.f, 1.f, loop, 0, 0, false);
                FMODAudioEngine::playMusic(s, loop, f, p3);
                return;
            }
        }
        FMODAudioEngine::playMusic(path, loop, fade, p3);
    }

    void loadMusic(gd::string path, float speed, float unk, float vol, bool loop, int mid, int cid, bool dr) {
        if (shouldReplace(path)) {
            gd::string s = getCustomStr();
            if (!s.empty()) { FMODAudioEngine::loadMusic(s, speed, unk, vol, loop, mid, cid, dr); return; }
        }
        FMODAudioEngine::loadMusic(path, speed, unk, vol, loop, mid, cid, dr);
    }

    void loadAndPlayMusic(gd::string path, unsigned int t, int mid) {
        if (shouldReplace(path)) {
            gd::string s = getCustomStr();
            if (!s.empty()) { FMODAudioEngine::loadAndPlayMusic(s, t, mid); return; }
        }
        FMODAudioEngine::loadAndPlayMusic(path, t, mid);
    }
};

// ========================================================
//  PAUSE MENU UI
// ========================================================
class $modify(MusicFXPause, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        
        auto menu = this->getChildByID("left-button-menu");
        if (!menu) {
            // Fallback if no left-button-menu
            menu = CCMenu::create();
            menu->setID("gm-fx-menu");
            menu->setPosition({30.f, 100.f});
            this->addChild(menu);
        }

        auto spr = ButtonSprite::create("FX", "goldFont.fnt", "GJ_button_01.png", .8f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(MusicFXPause::onMusicFX));
        btn->setID("music-fx-btn"_spr);
        menu->addChild(btn);
        menu->updateLayout();
    }

    void onMusicFX(CCObject*) {
        geode::openSettingsPopup(Mod::get());
    }
};

// ========================================================
//  BEAT DETECTION + VISUAL EFFECTS
// ========================================================
class $modify(GlobalVisuals, PlayLayer) {
    struct Fields {
        // --- beat detection state ---
        float prevMeter     = 0.f;
        float smoothEnergy  = 0.f;
        float peakEnergy    = 0.f;
        float beatIntensity = 0.f;
        float timeSinceBeat = 10.f;
        float recentMin     = 0.f;
        float quietTime     = 0.f;
        
        // --- color state ---
        ccColor3B targetBGColor  = ccc3(255, 255, 255);
        ccColor3B currentBGColor = ccc3(255, 255, 255);
        ccColor3B origBGColor    = ccc3(40, 125, 255);
        bool      origBGSaved    = false;
        bool      hasBeatThisFrame = false;

        // --- new UI / FX state ---
        float miniPlayerTimer = 0.f;
        float playerOrigScale = 1.0f;
        bool  hasSavedPlayerScale = false;
        float glitchOffset = 0.f;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        bool pulse  = Mod::get()->getSettingValue<bool>("screen-pulse");
        bool waves  = Mod::get()->getSettingValue<bool>("side-waves");
        bool bgCol  = Mod::get()->getSettingValue<bool>("random-color-pulse");
        bool shake  = Mod::get()->getSettingValue<bool>("screen-shake");
        bool pPulse = Mod::get()->getSettingValue<bool>("player-pulse");
        bool parts  = Mod::get()->getSettingValue<bool>("beat-particles");
        bool glitch = Mod::get()->getSettingValue<bool>("chromatic-glitch");

        if (pulse || waves || bgCol || shake || pPulse || parts || glitch) {
            FMODAudioEngine::sharedEngine()->enableMetering();
        }

        auto ws = CCDirector::sharedDirector()->getWinSize();

        // --- screen pulse overlay ---
        if (pulse) {
            auto p = CCLayerColor::create(ccc4(255, 255, 255, 0));
            p->setContentSize(ws);
            p->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
            p->setID("gm-pulse"_spr);
            p->setZOrder(1000);
            this->addChild(p);
        }

        // --- side waves ---
        if (waves) {
            auto lw = CCLayerColor::create(ccc4(0, 200, 255, 0));
            lw->setContentSize({0.f, ws.height});
            lw->setPosition({0, 0});
            lw->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
            lw->setID("gm-wave-l"_spr);
            lw->setZOrder(999);
            this->addChild(lw);

            auto rw = CCLayerColor::create(ccc4(0, 200, 255, 0));
            rw->setContentSize({0.f, ws.height});
            rw->setAnchorPoint({1.f, 0.f});
            rw->ignoreAnchorPointForPosition(false);
            rw->setPosition({ws.width, 0});
            rw->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
            rw->setID("gm-wave-r"_spr);
            rw->setZOrder(999);
            this->addChild(rw);
        }

        // --- vignette ---
        if (pulse || waves) {
            auto topBar = CCLayerColor::create(ccc4(0, 0, 0, 40));
            topBar->setContentSize({ws.width, 3.f});
            topBar->setPosition({0, ws.height - 3.f});
            topBar->setID("gm-bar-top"_spr);
            topBar->setZOrder(1001);
            this->addChild(topBar);

            auto bottomBar = CCLayerColor::create(ccc4(0, 0, 0, 40));
            bottomBar->setContentSize({ws.width, 3.f});
            bottomBar->setPosition({0, 0});
            bottomBar->setID("gm-bar-bot"_spr);
            bottomBar->setZOrder(1001);
            this->addChild(bottomBar);
        }

        // --- mini player ---
        if (Mod::get()->getSettingValue<bool>("show-mini-player")) {
            auto customPath = Mod::get()->getSettingValue<std::filesystem::path>("music-path");
            std::string filename(reinterpret_cast<const char*>(customPath.filename().u8string().c_str()));
            if (filename.empty()) filename = "No Track Selected";

            auto pNode = CCNode::create();
            pNode->setID("gm-mini-player"_spr);
            pNode->setPosition({ws.width - 15.f, ws.height - 15.f});
            pNode->setAnchorPoint({1.f, 1.f});
            
            auto icon = CCSprite::createWithSpriteFrameName("GJ_musicIcon_001.png");
            icon->setPosition({-5.f, -5.f});
            icon->setAnchorPoint({1.f, 1.f});
            icon->setScale(0.6f);
            pNode->addChild(icon);

            auto label = CCLabelBMFont::create(filename.c_str(), "chatFont.fnt");
            label->setAnchorPoint({1.f, 1.f});
            label->setPosition({-25.f, -7.f});
            label->setScale(0.5f);
            label->setColor(ccc3(200, 255, 200));
            pNode->addChild(label);

            this->addChild(pNode, 1002);
            
            // Initial state (invisible)
            icon->setOpacity(0);
            label->setOpacity(0);
            
            // We recreate the action sequence for each node because cocos2d-x v2 doesn't have clone()
            auto seqIcon = CCSequence::create(
                CCFadeIn::create(0.5f),
                CCDelayTime::create(3.5f),
                CCFadeOut::create(1.0f),
                nullptr
            );
            
            auto seqLabel = CCSequence::create(
                CCFadeIn::create(0.5f),
                CCDelayTime::create(3.5f),
                CCFadeOut::create(1.0f),
                nullptr
            );
            
            icon->runAction(seqIcon);
            label->runAction(seqLabel);
        }

        return true;
    }

    void updateColor(ccColor3B& color, float fadeTime, int colorID, bool blending,
                     float opacity, ccHSVValue& copyHSV, int colorIDToCopy,
                     bool copyOpacity, EffectGameObject* caller, int unk1, int unk2)
    {
        bool bgCol = Mod::get()->getSettingValue<bool>("random-color-pulse");
        
        if (bgCol && colorID == 1000) {
            if (!m_fields->origBGSaved) {
                m_fields->origBGColor  = color;
                m_fields->origBGSaved  = true;
                m_fields->currentBGColor = color;
            }
            ccColor3B c = m_fields->currentBGColor;
            PlayLayer::updateColor(c, fadeTime, colorID, blending, opacity, copyHSV, colorIDToCopy, copyOpacity, caller, unk1, unk2);
            return;
        }

        PlayLayer::updateColor(color, fadeTime, colorID, blending, opacity, copyHSV, colorIDToCopy, copyOpacity, caller, unk1, unk2);
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        bool pulse  = Mod::get()->getSettingValue<bool>("screen-pulse");
        bool waves  = Mod::get()->getSettingValue<bool>("side-waves");
        bool bgCol  = Mod::get()->getSettingValue<bool>("random-color-pulse");
        bool shake  = Mod::get()->getSettingValue<bool>("screen-shake");
        bool pPulse = Mod::get()->getSettingValue<bool>("player-pulse");
        bool parts  = Mod::get()->getSettingValue<bool>("beat-particles");
        bool glitch = Mod::get()->getSettingValue<bool>("chromatic-glitch");

        if (!pulse && !waves && !bgCol && !shake && !pPulse && !parts && !glitch) return;

        float meter = FMODAudioEngine::sharedEngine()->getMeteringValue();
        meter = std::max(0.f, std::min(1.f, meter));

        float delta = meter - m_fields->prevMeter;
        m_fields->prevMeter = meter;

        const float smoothAlpha = 1.2f * dt;
        m_fields->smoothEnergy += (meter - m_fields->smoothEnergy) * std::min(1.f, smoothAlpha);

        if (meter < m_fields->recentMin) {
            m_fields->recentMin = meter;
        } else {
            m_fields->recentMin += (meter - m_fields->recentMin) * std::min(1.f, 5.f * dt);
        }

        if (meter > m_fields->peakEnergy) {
            m_fields->peakEnergy = meter;
        } else {
            m_fields->peakEnergy -= dt * 0.12f;
            m_fields->peakEnergy = std::max(m_fields->peakEnergy, 0.15f);
        }

        if (meter < 0.12f) {
            m_fields->quietTime += dt;
        } else {
            m_fields->quietTime = 0.f;
        }

        float sensitivity = Mod::get()->getSettingValue<double>("beat-sensitivity");

        float contrast    = meter - m_fields->recentMin;
        float minDelta    = 0.12f - sensitivity * 0.06f;
        float minContrast = 0.18f - sensitivity * 0.10f;
        float minCooldown = 0.25f - sensitivity * 0.12f;

        m_fields->timeSinceBeat += dt;

        bool isBeat = false;
        if (delta > minDelta 
            && contrast > minContrast 
            && meter > 0.25f 
            && m_fields->timeSinceBeat > minCooldown) 
        {
            isBeat = true;
            m_fields->timeSinceBeat = 0.f;
            m_fields->beatIntensity = 1.f;
            m_fields->recentMin = meter;
        }

        m_fields->hasBeatThisFrame = isBeat;
        m_fields->beatIntensity -= dt * 4.5f;
        m_fields->beatIntensity = std::max(0.f, m_fields->beatIntensity);

        // --- 1. Random BG Color ---
        if (bgCol && isBeat) {
            float hue = static_cast<float>(rand() % 360);
            float s = 0.85f, v = 1.0f;
            int hi = static_cast<int>(hue / 60.f) % 6;
            float f = hue / 60.f - hi;
            float p = v * (1.f - s);
            float q = v * (1.f - f * s);
            float t = v * (1.f - (1.f - f) * s);
            float r, g, b;
            switch (hi) {
                case 0: r=v; g=t; b=p; break;
                case 1: r=q; g=v; b=p; break;
                case 2: r=p; g=v; b=t; break;
                case 3: r=p; g=q; b=v; break;
                case 4: r=t; g=p; b=v; break;
                default:r=v; g=p; b=q; break;
            }
            m_fields->targetBGColor = ccc3(
                static_cast<GLubyte>(r * 255.f),
                static_cast<GLubyte>(g * 255.f),
                static_cast<GLubyte>(b * 255.f)
            );
        }

        if (bgCol && m_fields->quietTime > 0.4f) {
            float fadeFactor = std::min(1.f, (m_fields->quietTime - 0.4f) * 1.5f);
            m_fields->targetBGColor = ccc3(
                static_cast<GLubyte>(m_fields->targetBGColor.r * (1.f - fadeFactor)),
                static_cast<GLubyte>(m_fields->targetBGColor.g * (1.f - fadeFactor)),
                static_cast<GLubyte>(m_fields->targetBGColor.b * (1.f - fadeFactor))
            );
        }

        if (bgCol) {
            float lerpSpeed = 6.f * dt;
            auto& cur = m_fields->currentBGColor;
            auto& tgt = m_fields->targetBGColor;
            cur.r += static_cast<GLubyte>((static_cast<float>(tgt.r) - cur.r) * lerpSpeed);
            cur.g += static_cast<GLubyte>((static_cast<float>(tgt.g) - cur.g) * lerpSpeed);
            cur.b += static_cast<GLubyte>((static_cast<float>(tgt.b) - cur.b) * lerpSpeed);
            if (this->m_background) this->m_background->setColor(cur);
        }

        // --- 2. Screen Pulse ---
        if (pulse) {
            if (auto pl = static_cast<CCLayerColor*>(this->getChildByIDRecursive("gm-pulse"_spr))) {
                float intensity = m_fields->beatIntensity;
                GLubyte alpha = static_cast<GLubyte>(intensity * 100.f);
                ccColor3B pulseCol = bgCol ? m_fields->targetBGColor : ccc3(255, 255, 255);
                pl->setColor(pulseCol);
                pl->setOpacity(alpha);
            }
        }

        // --- 3. Side Waves ---
        if (waves) {
            auto ws = CCDirector::sharedDirector()->getWinSize();
            float waveWidth = meter * meter * 80.f;
            float waveAlpha = std::min(meter * 200.f, 200.f);
            ccColor3B waveCol = bgCol ? m_fields->targetBGColor : ccc3(0, 200, 255);

            if (auto lw = static_cast<CCLayerColor*>(this->getChildByIDRecursive("gm-wave-l"_spr))) {
                float curW = lw->getContentSize().width;
                if (waveWidth > curW) curW += (waveWidth - curW) * 20.f * dt;
                else curW -= dt * 300.f;
                lw->setContentSize({std::max(0.f, curW), ws.height});
                lw->setColor(waveCol);
                lw->setOpacity(static_cast<GLubyte>(waveAlpha));
            }

            if (auto rw = static_cast<CCLayerColor*>(this->getChildByIDRecursive("gm-wave-r"_spr))) {
                float curW = rw->getContentSize().width;
                if (waveWidth > curW) curW += (waveWidth - curW) * 20.f * dt;
                else curW -= dt * 300.f;
                rw->setContentSize({std::max(0.f, curW), ws.height});
                rw->setColor(waveCol);
                rw->setOpacity(static_cast<GLubyte>(waveAlpha));
            }
        }

        // --- 4. Screen Shake & Glitch ---
        float ox = 0.f;
        float oy = 0.f;
        
        if (shake && m_fields->beatIntensity > 0.1f) {
            float amp = m_fields->beatIntensity * 3.f;
            ox += (static_cast<float>(rand() % 200) / 100.f - 1.f) * amp;
            oy += (static_cast<float>(rand() % 200) / 100.f - 1.f) * amp;
        }
        
        if (glitch) {
            // Horizontal jitter glitch on beats
            if (m_fields->beatIntensity > 0.5f) {
                m_fields->glitchOffset = (rand() % 2 == 0 ? 4.f : -4.f) * m_fields->beatIntensity;
            } else {
                m_fields->glitchOffset = 0.f;
            }
            ox += m_fields->glitchOffset;
        }

        if (shake || glitch) {
            if (ox != 0.f || oy != 0.f) {
                this->setPosition({ox, oy});
            } else {
                auto pos = this->getPosition();
                this->setPosition({pos.x * 0.85f, pos.y * 0.85f});
            }
        }

        // --- 5. Player Pulse ---
        if (pPulse && this->m_player1) {
            if (!m_fields->hasSavedPlayerScale) {
                m_fields->playerOrigScale = this->m_player1->getScale();
                m_fields->hasSavedPlayerScale = true;
            }
            float targetScale = m_fields->playerOrigScale * (1.0f + m_fields->beatIntensity * 0.25f);
            this->m_player1->setScale(targetScale);
        }

        // --- 6. Beat Particles ---
        if (parts && isBeat && meter > 0.6f && this->m_player1) {
            // Only emit particles on very strong beats
            auto p = CCParticleExplosion::create();
            p->setPosition(this->m_player1->getPosition());
            p->setLife(0.3f);
            p->setLifeVar(0.1f);
            p->setStartSize(10.f);
            p->setEndSize(0.f);
            
            auto col = bgCol ? m_fields->targetBGColor : ccc3(255, 255, 255);
            ccColor4F c4 = {col.r/255.f, col.g/255.f, col.b/255.f, 1.0f};
            p->setStartColor(c4);
            p->setEndColor({c4.r, c4.g, c4.b, 0.f});
            
            p->setTotalParticles(15);
            p->setSpeed(120.f);
            p->setSpeedVar(30.f);
            p->setGravity({0.f, 0.f});
            this->addChild(p, 100);
        }
    }
};

// ========================================================
//  TRAJECTORY BOT (SECRET ALGORITHMIC AUTO-PLAY)
// ========================================================
class $modify(TrajectoryBot, PlayLayer) {
    struct Fields {
        bool holdingJump = false;
    };

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (!Mod::get()->getSettingValue<bool>("secret-bot-enabled")) return;
        if (!m_player1) return;
        
        bool mustJump = false;
        bool safeToJump = true;
        
        float lookAhead = 22.f;
        float jumpDist = 130.f; 
        
        auto pBox = m_player1->boundingBox();
        
        CCRect groundPathBox = { pBox.origin.x, pBox.origin.y, lookAhead, pBox.size.height };
        CCRect landingBox = { pBox.origin.x + jumpDist, pBox.origin.y, pBox.size.width, 150.f }; // Tall box to check for spikes on top of blocks
        
        if (this->m_objects) {
            for (auto go : CCArrayExt<GameObject*>(this->m_objects)) {
                if (!go) continue;
                bool isHazard = (go->m_objectType == GameObjectType::Hazard);
                bool isSolid = (go->m_objectType == GameObjectType::Solid);
                
                if (!isHazard && !isSolid) continue;
                
                auto gBox = go->boundingBox();
                
                // Fast exit if object is too far behind or too far ahead
                if (gBox.origin.x < pBox.origin.x - 100.f) continue;
                if (gBox.origin.x > pBox.origin.x + 200.f) continue;
                
                if (isHazard) {
                    if (groundPathBox.intersectsRect(gBox)) {
                        mustJump = true;
                    }
                    if (landingBox.intersectsRect(gBox)) {
                        safeToJump = false;
                    }
                } else if (isSolid) {
                    // Check if this solid block is blocking our current path (Y-axis collision)
                    if (gBox.origin.y < pBox.origin.y + pBox.size.height && gBox.origin.y + gBox.size.height > pBox.origin.y) {
                        if (groundPathBox.intersectsRect(gBox)) {
                            mustJump = true; // We must jump to avoid hitting the wall
                        }
                    }
                }
            }
        }
        
        if (m_player1->m_isOnGround) {
            if (mustJump && safeToJump) {
                if (!m_fields->holdingJump) {
                    this->handleButton(true, 1, true); // Jump
                    m_fields->holdingJump = true;
                }
            } else {
                if (m_fields->holdingJump) {
                    this->handleButton(false, 1, true);
                    m_fields->holdingJump = false;
                }
            }
        } else {
            // We are in the air! Release the button so we don't automatically jump again when we land
            if (m_fields->holdingJump) {
                this->handleButton(false, 1, true);
                m_fields->holdingJump = false;
            }
        }
    }
};

class $modify(BotKeybind, UILayer) {
    void keyDown(cocos2d::enumKeyCodes key, double timestamp) {
        UILayer::keyDown(key, timestamp);
        if (key == cocos2d::KEY_B) {
            auto mod = Mod::get();
            bool current = mod->getSettingValue<bool>("secret-bot-enabled");
            mod->setSettingValue("secret-bot-enabled", !current);
            
            if (!current) {
                geode::Notification::create("Bot Enabled!", geode::NotificationIcon::Success)->show();
            } else {
                geode::Notification::create("Bot Disabled!", geode::NotificationIcon::Error)->show();
            }
        }
    }
};