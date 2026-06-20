#include "PCH.h"
#include "UI/ScaleformUI.h"
#include "BarterManager.h"
#include "Hooks.h"

BarterOfferMenu::BarterOfferMenu() {
    logger::info("BarterOfferMenu: Constructing menu, loading SWF from '{}'", MENU_PATH);

    // FxDelegate must be set up BEFORE LoadMovieEx so BSScaleformManager can connect it
    fxDelegate.reset(new RE::FxDelegate());
    auto callback = RE::make_gptr<FxDelegateCallback>();
    fxDelegate->RegisterHandler(callback.get());

    auto* scaleformManager = RE::BSScaleformManager::GetSingleton();
    if (scaleformManager) {
        scaleformManager->LoadMovieEx(this, MENU_PATH,
            [](RE::GFxMovieDef* a_def) -> void {
                a_def->SetState(
                    RE::GFxState::StateType::kLog,
                    RE::make_gptr<RE::GFxLog>().get());
            });
    } else {
        logger::error("BarterOfferMenu: BSScaleformManager is null!");
    }

    auto view = uiMovie;
    if (view) {
        view->SetMouseCursorCount(1);
        view->SetVisible(true);

        // Force 60 FPS minimum for smooth slider/button interaction
        RE::GFxValue highQuality;
        highQuality.SetNumber(2.0);  // HIGH quality
        view->SetVariable("_quality", highQuality);

        logger::info("BarterOfferMenu: SWF loaded successfully (movie=0x{:X}), FxDelegate registered",
            reinterpret_cast<uintptr_t>(view.get()));
    } else {
        logger::error("BarterOfferMenu: Failed to load SWF from '{}' - uiMovie is null", MENU_PATH);
    }

    menuFlags.set(RE::UI_MENU_FLAGS::kModal);
    menuFlags.set(RE::UI_MENU_FLAGS::kRequiresUpdate);
    menuFlags.set(RE::UI_MENU_FLAGS::kRendersOffscreenTargets);
    menuFlags.set(RE::UI_MENU_FLAGS::kUsesCursor);
    depthPriority = 10;
    inputContext = Context::kNone;
    logger::info("BarterOfferMenu: Menu flags set (Modal|RequiresUpdate|RendersOffscreen|UsesCursor), depth={}", depthPriority);
}

void BarterOfferMenu::Register() {
    auto* ui = RE::UI::GetSingleton();
    if (ui) {
        ui->Register(MENU_NAME, Creator);
        logger::info("BarterOfferMenu registered");
    }
}

RE::IMenu* BarterOfferMenu::Creator() {
    return new BarterOfferMenu();
}

void BarterOfferMenu::Show() {
    logger::info("BarterOfferMenu::Show() - Requesting menu open");
    auto* msgQ = RE::UIMessageQueue::GetSingleton();
    if (msgQ) {
        msgQ->AddMessage(MENU_NAME, RE::UI_MESSAGE_TYPE::kShow, nullptr);
    } else {
        logger::error("BarterOfferMenu::Show() - UIMessageQueue is null!");
    }
}

void BarterOfferMenu::Hide() {
    logger::info("BarterOfferMenu::Hide() - Requesting menu close");
    auto* msgQ = RE::UIMessageQueue::GetSingleton();
    if (msgQ) {
        msgQ->AddMessage(MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
    }
    Hooks::interceptingTransaction = false;
}

void BarterOfferMenu::AdvanceMovie(float a_interval, std::uint32_t a_currentTime) {
    if (pendingOfferData.has_value() && uiMovie) {
        logger::info("BarterOfferMenu::AdvanceMovie - Applying pending offer data");
        SetOfferData(pendingOfferData.value());
        pendingOfferData.reset();
    }

    // Tick down input cooldown (prevents activation key from auto-submitting)
    if (inputCooldown > 0.0f) {
        inputCooldown -= a_interval;
        // Drain any queued accept inputs during cooldown
        if (auto* sink = InputDeviceSink::GetSingleton()) {
            sink->ConsumeAccept();
            sink->ConsumeX();
            sink->ConsumeR();
        }
    }

    // --- Responsive slider: C++ mouse drag tracking ---
    if (uiMovie && !showingResult) {
        bool mouseHeld = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        static bool mouseWasHeld = false;
        bool mouseJustPressed = mouseHeld && !mouseWasHeld;
        bool mouseJustReleased = !mouseHeld && mouseWasHeld;
        mouseWasHeld = mouseHeld;

        // Use ROOT space coordinates for all hit testing
        RE::GFxValue rootXM, rootYM;
        uiMovie->GetVariable(&rootXM, "_root._xmouse");
        uiMovie->GetVariable(&rootYM, "_root._ymouse");
        double mx = (rootXM.GetType() == RE::GFxValue::ValueType::kNumber) ? rootXM.GetNumber() : -1.0;
        double my = (rootYM.GetType() == RE::GFxValue::ValueType::kNumber) ? rootYM.GetNumber() : -1.0;

        // Slider absolute bounds in root space (must match generate_swf.py)
        constexpr double sliderAbsX = 540.0;  // 640 - 100
        constexpr double sliderAbsY = 298.0;  // panel_y(160) + 138
        constexpr double sliderTrackW = 200.0;
        constexpr double sliderTrackH = 8.0;

        // Only allow slider interaction in offer state (not counter or result)
        if (!showingCounter && !showingResult && mouseHeld) {
            // Start drag only if clicking within the slider track area (root space)
            if (!sliderDragging && mouseJustPressed) {
                if (mx >= sliderAbsX - 5 && mx <= sliderAbsX + sliderTrackW + 5 &&
                    my >= sliderAbsY - 12 && my <= sliderAbsY + sliderTrackH + 12) {
                    sliderDragging = true;
                }
            }

            // While dragging, update slider from mouse X position
            if (sliderDragging) {
                double localX = mx - sliderAbsX;
                if (localX < 0.0) localX = 0.0;
                if (localX > sliderTrackW) localX = sliderTrackW;

                RE::GFxValue sliderMinV, sliderMaxV;
                uiMovie->GetVariable(&sliderMinV, "_root.sliderMin");
                uiMovie->GetVariable(&sliderMaxV, "_root.sliderMax");
                double minVal = (sliderMinV.GetType() == RE::GFxValue::ValueType::kNumber) ? sliderMinV.GetNumber() : 0.0;
                double maxVal = (sliderMaxV.GetType() == RE::GFxValue::ValueType::kNumber) ? sliderMaxV.GetNumber() : 100.0;

                double newValue = minVal + (maxVal - minVal) * (localX / sliderTrackW);
                newValue = std::round(newValue);
                newValue = std::clamp(newValue, minVal, maxVal);
                RE::GFxValue val;
                val.SetNumber(newValue);
                uiMovie->SetVariable("_root.sliderValue", val);
                uiMovie->Invoke("_root.updateSliderDisplay", nullptr, nullptr, 0);
            }
        } else {
            sliderDragging = false;
        }

        // --- C++ Button hit-testing (bypasses broken AS mouse events) ---
        constexpr int panel_x = 470;
        constexpr int panel_y = 160;
        constexpr int btn_w = 80;
        constexpr int btn_h = 24;
        constexpr int btn_gap = 8;
        constexpr int btn_start_x = panel_x + 42;
        constexpr int btn_y_pos = panel_y + 252;

        struct ButtonDef { const char* name; int x; int y; };
        ButtonDef buttons[] = {
            // Offer state buttons
            {"btn_submit", btn_start_x, btn_y_pos},
            {"btn_intimidate", btn_start_x + btn_w + btn_gap, btn_y_pos},
            {"btn_cancel", btn_start_x + 2 * (btn_w + btn_gap), btn_y_pos},
            // Counter-offer state buttons
            {"btn_accept", btn_start_x, btn_y_pos},
            {"btn_reoffer", btn_start_x + btn_w + btn_gap, btn_y_pos},
            {"btn_walkaway", btn_start_x + 2 * (btn_w + btn_gap), btn_y_pos},
            // Result state button
            {"btn_continue", btn_start_x + btn_w + btn_gap, btn_y_pos},
        };
        constexpr int numButtons = 7;

        int newHovered = -1;
        for (int i = 0; i < numButtons; i++) {
            // Check button visibility first
            std::string visPath = std::format("_root.{}._visible", buttons[i].name);
            RE::GFxValue btnVis;
            uiMovie->GetVariable(&btnVis, visPath.c_str());
            if (btnVis.GetType() == RE::GFxValue::ValueType::kBoolean && !btnVis.GetBool())
                continue;

            if (mx >= buttons[i].x && mx <= buttons[i].x + btn_w &&
                my >= buttons[i].y && my <= buttons[i].y + btn_h) {
                newHovered = i;
                break;
            }
        }

        // Update hover state (use _alpha, keep bgNormal visible but dimmed)
        if (newHovered != hoveredButton) {
            // Unhover old - restore full opacity on normal, hide highlight
            if (hoveredButton >= 0 && hoveredButton < numButtons) {
                RE::GFxValue full, zero;
                full.SetNumber(100.0); zero.SetNumber(0.0);
                uiMovie->SetVariable(std::format("_root.{}.bgNormal._alpha", buttons[hoveredButton].name).c_str(), full);
                uiMovie->SetVariable(std::format("_root.{}.bgHighlight._alpha", buttons[hoveredButton].name).c_str(), zero);
            }
            // Hover new - dim normal, show highlight
            if (newHovered >= 0) {
                RE::GFxValue dim, full;
                dim.SetNumber(40.0); full.SetNumber(100.0);
                uiMovie->SetVariable(std::format("_root.{}.bgNormal._alpha", buttons[newHovered].name).c_str(), dim);
                uiMovie->SetVariable(std::format("_root.{}.bgHighlight._alpha", buttons[newHovered].name).c_str(), full);
            }
            hoveredButton = newHovered;
        }

        // Handle click on mouse-down within a button (don't require drag release)
        if (mouseJustPressed && hoveredButton >= 0) {
            const char* btnName = buttons[hoveredButton].name;
            logger::info("BarterOfferMenu: Button clicked: {}", btnName);
            if (std::string_view(btnName) == "btn_submit") {
                RE::GFxValue sliderVal;
                uiMovie->GetVariable(&sliderVal, "_root.sliderValue");
                if (sliderVal.GetType() == RE::GFxValue::ValueType::kNumber) {
                    int offeredPrice = static_cast<int>(std::round(sliderVal.GetNumber()));
                    SKSE::GetTaskInterface()->AddTask([offeredPrice]() {
                        BarterManager::GetSingleton()->OnPlayerOffer(offeredPrice);
                    });
                }
            } else if (std::string_view(btnName) == "btn_intimidate") {
                SKSE::GetTaskInterface()->AddTask([]() {
                    BarterManager::GetSingleton()->OnIntimidateAttempt();
                });
            } else if (std::string_view(btnName) == "btn_cancel") {
                SKSE::GetTaskInterface()->AddTask([]() {
                    BarterManager::GetSingleton()->OnCancelled();
                });
            } else if (std::string_view(btnName) == "btn_accept") {
                // Accept counter-offer
                SKSE::GetTaskInterface()->AddTask([]() {
                    BarterManager::GetSingleton()->OnCounterResponse(0);
                });
            } else if (std::string_view(btnName) == "btn_reoffer") {
                // Re-offer: restore slider UI for a new attempt
                showingCounter = false;
                RestoreOfferUI();
                SKSE::GetTaskInterface()->AddTask([]() {
                    BarterManager::GetSingleton()->OnCounterResponse(1);
                });
            } else if (std::string_view(btnName) == "btn_walkaway") {
                // Walk away from counter-offer
                SKSE::GetTaskInterface()->AddTask([]() {
                    BarterManager::GetSingleton()->OnCounterResponse(2);
                });
            } else if (std::string_view(btnName) == "btn_continue") {
                showingResult = false;
                if (lastResultAccepted) {
                    SKSE::GetTaskInterface()->AddTask([]() {
                        BarterManager::GetSingleton()->OnCancelled();
                    });
                } else {
                    RestoreOfferUI();
                }
            }
        }

        // --- Update input hints based on actual input device (via InputDeviceSink) ---
        auto* inputSink = InputDeviceSink::GetSingleton();
        bool currentGamepad = inputSink->IsUsingGamepad();
        if (currentGamepad != lastInputWasGamepad) {
            lastInputWasGamepad = currentGamepad;
            RE::GFxValue hintVal;
            if (showingCounter) {
                if (lastInputWasGamepad) {
                    hintVal.SetString(R"(<p align="center"><font face="$EverywhereMediumFont" size="8" color="#606060">[A] Accept    [X] Re-offer    [B] Walk Away</font></p>)");
                } else {
                    hintVal.SetString(R"(<p align="center"><font face="$EverywhereMediumFont" size="8" color="#606060">[E] Accept    [R] Re-offer    [Tab] Walk Away</font></p>)");
                }
            } else if (showingResult) {
                if (lastInputWasGamepad) {
                    hintVal.SetString(R"(<p align="center"><font face="$EverywhereMediumFont" size="8" color="#606060">[A] Retry    [B] Cancel</font></p>)");
                } else {
                    hintVal.SetString(R"(<p align="center"><font face="$EverywhereMediumFont" size="8" color="#606060">[E] Retry    [Tab] Cancel</font></p>)");
                }
            } else {
                if (lastInputWasGamepad) {
                    hintVal.SetString(R"(<p align="center"><font face="$EverywhereMediumFont" size="8" color="#606060">[A] Confirm    [B] Cancel    [LB/RB] +/-5    [D-pad] Adjust</font></p>)");
                } else {
                    hintVal.SetString(R"(<p align="center"><font face="$EverywhereMediumFont" size="8" color="#606060">[E] Confirm    [Tab] Cancel    [LB/RB] +/-5    [Arrow Keys] Adjust</font></p>)");
                }
            }
            uiMovie->SetVariable("_root.ButtonHintText.htmlText", hintVal);
        }

        // --- Raw gamepad input via InputDeviceSink (bypasses broken input context) ---

        // Handle gamepad accept/cancel buttons directly
        if (inputSink->ConsumeAccept()) {
            if (inputCooldown > 0.0f) {
                // Ignore - cooldown active
            } else if (showingResult) {
                showingResult = false;
                RestoreOfferUI();
            } else if (showingCounter) {
                // A button in counter state = accept the counter-offer
                inputCooldown = 0.4f;
                SKSE::GetTaskInterface()->AddTask([]() {
                    BarterManager::GetSingleton()->OnCounterResponse(0);
                });
            } else if (uiMovie) {
                RE::GFxValue sliderVal;
                uiMovie->GetVariable(&sliderVal, "_root.sliderValue");
                if (sliderVal.GetType() == RE::GFxValue::ValueType::kNumber) {
                    int offeredPrice = static_cast<int>(std::round(sliderVal.GetNumber()));
                    inputCooldown = 0.4f;  // Prevent double-input
                    SKSE::GetTaskInterface()->AddTask([offeredPrice]() {
                        BarterManager::GetSingleton()->OnPlayerOffer(offeredPrice);
                    });
                }
            }
        }
        if (inputSink->ConsumeCancel()) {
            if (showingResult) {
                showingResult = false;
                RestoreOfferUI();
            } else if (showingCounter) {
                // B button in counter state = walk away
                SKSE::GetTaskInterface()->AddTask([]() {
                    BarterManager::GetSingleton()->OnCounterResponse(2);
                });
            } else {
                SKSE::GetTaskInterface()->AddTask([]() {
                    BarterManager::GetSingleton()->OnCancelled();
                });
            }
        }
        // X button = re-offer (only in counter state)
        if (inputSink->ConsumeX()) {
            if (showingCounter) {
                showingCounter = false;
                RestoreOfferUI();
                SKSE::GetTaskInterface()->AddTask([]() {
                    BarterManager::GetSingleton()->OnCounterResponse(1);
                });
            }
        }
        // Keyboard R = re-offer (only in counter state)
        if (inputSink->ConsumeR()) {
            if (showingCounter) {
                showingCounter = false;
                RestoreOfferUI();
                SKSE::GetTaskInterface()->AddTask([]() {
                    BarterManager::GetSingleton()->OnCounterResponse(1);
                });
            }
        }
        // Consume Y to prevent accidental input passthrough
        inputSink->ConsumeY();

        // Handle D-pad/bumper directional input for slider (only in offer state)
        int rawDir = inputSink->ConsumeGamepadDir();
        if (!showingCounter && !showingResult && rawDir != 0 && uiMovie) {
            // D-pad press: if it's a NEW direction, move exactly 1 and start grace timer
            if (rawDir != gamepadHoldDir) {
                // New direction or first press: exactly 1 step
                RE::GFxValue arg;
                int step = (rawDir == -5 || rawDir == 5) ? rawDir : (rawDir > 0 ? 1 : -1);
                arg.SetNumber(static_cast<double>(step));
                uiMovie->Invoke("_root.adjustSlider", nullptr, &arg, 1);
                gamepadHoldDir = (rawDir > 0) ? 1 : -1;  // Normalize direction
                if (rawDir == -5 || rawDir == 5) gamepadHoldDir = rawDir;  // Keep bumper magnitude
                gamepadHoldTimer = 0.0f;
                gamepadGraceDone = false;
            }
        } else if (rawDir == 0 && !showingCounter && !showingResult) {
            // D-pad released - only clear if thumbstick isn't active
            float thumbXCheck = inputSink->GetThumbstickX();
            if (std::abs(thumbXCheck) <= 0.25f && gamepadHoldDir != 0) {
                gamepadHoldDir = 0;
                gamepadHoldTimer = 0.0f;
                gamepadGraceDone = false;
            }
        }

        // Handle thumbstick slider movement (only in offer state)
        float thumbX = inputSink->GetThumbstickX();
        if (!showingCounter && !showingResult && std::abs(thumbX) > 0.25f && uiMovie) {
            int dir = (thumbX < 0) ? -1 : 1;
            if (gamepadHoldDir == 0) {
                // First deflection: exactly 1 step, then start grace
                RE::GFxValue arg;
                arg.SetNumber(static_cast<double>(dir));
                uiMovie->Invoke("_root.adjustSlider", nullptr, &arg, 1);
                gamepadHoldDir = dir;
                gamepadHoldTimer = 0.0f;
                gamepadGraceDone = false;
            } else {
                // Already holding - direction maintained (handled by hold-to-repeat below)
                gamepadHoldDir = dir;
            }
        } else if (rawDir == 0 && std::abs(thumbX) <= 0.25f) {
            if (gamepadHoldDir != 0) {
                gamepadHoldDir = 0;
                gamepadHoldTimer = 0.0f;
                gamepadGraceDone = false;
            }
        }

        // --- Gamepad hold-to-repeat: accelerate slider movement AFTER grace period ---
        if (!showingCounter && !showingResult && gamepadHoldDir != 0) {
            gamepadHoldTimer += a_interval;
            // Grace period: 0.3s of no repeat
            if (gamepadHoldTimer >= 0.3f) {
                gamepadGraceDone = true;
            }
            // Only repeat after grace period
            if (gamepadGraceDone) {
                float repeatInterval = (gamepadHoldTimer < 1.0f) ? 0.08f : 0.03f;
                float speed = (gamepadHoldTimer < 1.0f) ? 1.0f : 2.0f;
                // Use frame-rate-independent accumulator
                static float repeatAccum = 0.0f;
                repeatAccum += a_interval;
                if (repeatAccum >= repeatInterval) {
                    repeatAccum -= repeatInterval;
                    RE::GFxValue arg;
                    int step = (std::abs(gamepadHoldDir) == 5) ? gamepadHoldDir : 
                               ((gamepadHoldDir > 0 ? 1 : -1) * static_cast<int>(speed));
                    arg.SetNumber(static_cast<double>(step));
                    uiMovie->Invoke("_root.adjustSlider", nullptr, &arg, 1);
                }
            }
        }

        // --- Dynamic UI updates: acceptance indicator + relationship preview (offer state only) ---
        static int updateCounter = 0;
        updateCounter++;
        if (!showingCounter && !showingResult &&
            (sliderDragging || gamepadHoldDir != 0 || (updateCounter % 10 == 0))) {
            // Hard-clamp sliderValue to never exceed sliderMax or go below sliderMin
            RE::GFxValue sliderVal, sliderMinV, sliderMaxV;
            uiMovie->GetVariable(&sliderVal, "_root.sliderValue");
            uiMovie->GetVariable(&sliderMinV, "_root.sliderMin");
            uiMovie->GetVariable(&sliderMaxV, "_root.sliderMax");
            if (sliderVal.GetType() == RE::GFxValue::ValueType::kNumber) {
                double val = sliderVal.GetNumber();
                double minV = (sliderMinV.GetType() == RE::GFxValue::ValueType::kNumber) ? sliderMinV.GetNumber() : 0.0;
                double maxV = (sliderMaxV.GetType() == RE::GFxValue::ValueType::kNumber) ? sliderMaxV.GetNumber() : 9999.0;
                if (val < minV || val > maxV) {
                    double clamped = std::clamp(val, minV, maxV);
                    RE::GFxValue clampedVal;
                    clampedVal.SetNumber(clamped);
                    uiMovie->SetVariable("_root.sliderValue", clampedVal);
                    uiMovie->Invoke("_root.updateSliderDisplay", nullptr, nullptr, 0);
                }
            }

            RE::GFxValue baseChanceVal, effPriceVal;
            uiMovie->GetVariable(&sliderVal, "_root.sliderValue");
            uiMovie->GetVariable(&baseChanceVal, "_root.acceptanceChance");
            uiMovie->GetVariable(&effPriceVal, "_root.effectivePrice");

            if (sliderVal.GetType() == RE::GFxValue::ValueType::kNumber &&
                effPriceVal.GetType() == RE::GFxValue::ValueType::kNumber) {
                double offeredGold = sliderVal.GetNumber();
                double effPrice = effPriceVal.GetNumber();
                double baseChance = baseChanceVal.GetType() == RE::GFxValue::ValueType::kNumber
                    ? baseChanceVal.GetNumber() : 50.0;

                // Calculate discount percentage from gold values
                double pct = effPrice > 0 ? ((offeredGold - effPrice) / effPrice) * 100.0 : 0.0;

                // Estimate acceptance based on discount
                double penalty = 0.0;
                if (pct < 0) penalty = std::abs(pct) * 1.5;
                else penalty = -pct * 0.5;
                double estimatedChance = std::clamp(baseChance - penalty, 0.0, 99.0);

                // Update acceptance text
                std::string acceptText;
                std::string acceptColor;
                if (estimatedChance >= 80) {
                    acceptText = "Merchant will ACCEPT this offer";
                    acceptColor = "#50B050";
                } else if (estimatedChance >= 60) {
                    acceptText = "Merchant will likely accept";
                    acceptColor = "#80B050";
                } else if (estimatedChance >= 40) {
                    acceptText = "Merchant looks uncertain";
                    acceptColor = "#D4B054";
                } else if (estimatedChance >= 20) {
                    acceptText = "Merchant looks unimpressed";
                    acceptColor = "#D08030";
                } else {
                    acceptText = "Merchant will likely REFUSE";
                    acceptColor = "#B05050";
                }

                auto html = std::format(
                    R"(<p align="center"><font face="$EverywhereMediumFont" size="10" color="{}">{}</font></p>)",
                    acceptColor, acceptText);
                RE::GFxValue htmlVal;
                htmlVal.SetString(html.c_str());
                uiMovie->SetVariable("_root.AcceptanceText.htmlText", htmlVal);

                // Update relationship effect preview
                std::string effectText;
                std::string effectColor = "#807060";
                if (pct < -15) {
                    effectText = "This offer will worsen your standing";
                    effectColor = "#B05050";
                } else if (pct < -5) {
                    effectText = "This offer may slightly worsen your standing";
                    effectColor = "#D08030";
                } else if (pct > 10) {
                    effectText = "This offer will improve your standing";
                    effectColor = "#50B050";
                } else if (pct > 3) {
                    effectText = "This offer may slightly improve your standing";
                    effectColor = "#80B050";
                } else {
                    effectText = "This offer has little effect on your standing";
                    effectColor = "#A09080";
                }
                auto effHtml = std::format(
                    R"(<p align="center"><font face="$EverywhereMediumFont" size="8" color="{}">{}</font></p>)",
                    effectColor, effectText);
                RE::GFxValue effVal;
                effVal.SetString(effHtml.c_str());
                uiMovie->SetVariable("_root.RelEffectText.htmlText", effVal);
            }
        }
    }

    RE::IMenu::AdvanceMovie(a_interval, a_currentTime);
}

RE::UI_MESSAGE_RESULTS BarterOfferMenu::ProcessMessage(RE::UIMessage& a_message) {
    switch (*a_message.type) {
        case RE::UI_MESSAGE_TYPE::kUserEvent: {
            auto* strData = static_cast<RE::BSUIMessageData*>(a_message.data);
            if (strData) {
                auto* userEvents = RE::UserEvents::GetSingleton();
                auto eventStr = strData->fixedStr;

                logger::trace("BarterOfferMenu event: {}", eventStr.c_str());

                if (eventStr == userEvents->cancel || eventStr == userEvents->back) {
                    // Ignore during input cooldown (prevents stale events after state transitions)
                    if (inputCooldown > 0.0f) {
                        return RE::UI_MESSAGE_RESULTS::kHandled;
                    }
                    logger::info("BarterOfferMenu: Cancel/Back pressed");
                    gamepadHoldDir = 0;
                    if (showingResult) {
                        showingResult = false;
                        inputCooldown = 0.3f;
                        if (lastResultAccepted) {
                            SKSE::GetTaskInterface()->AddTask([]() {
                                BarterManager::GetSingleton()->OnCancelled();
                            });
                        } else {
                            RestoreOfferUI();
                        }
                        return RE::UI_MESSAGE_RESULTS::kHandled;
                    }
                    if (showingCounter) {
                        // Cancel/Back during counter = walk away
                        showingCounter = false;
                        SKSE::GetTaskInterface()->AddTask([]() {
                            BarterManager::GetSingleton()->OnCounterResponse(2);
                        });
                        return RE::UI_MESSAGE_RESULTS::kHandled;
                    }
                    SKSE::GetTaskInterface()->AddTask([]() {
                        BarterManager::GetSingleton()->OnCancelled();
                    });
                    return RE::UI_MESSAGE_RESULTS::kHandled;
                }

                if (eventStr == userEvents->accept) {
                    gamepadHoldDir = 0;
                    // Ignore accept during input cooldown (prevents activation key from auto-submitting)
                    if (inputCooldown > 0.0f) {
                        return RE::UI_MESSAGE_RESULTS::kHandled;
                    }
                    if (showingResult) {
                        logger::info("BarterOfferMenu: Accept pressed in result state - returning to offer");
                        showingResult = false;
                        inputCooldown = 0.3f;
                        if (lastResultAccepted) {
                            SKSE::GetTaskInterface()->AddTask([]() {
                                BarterManager::GetSingleton()->OnCancelled();
                            });
                        } else {
                            RestoreOfferUI();
                        }
                        return RE::UI_MESSAGE_RESULTS::kHandled;
                    }
                    if (showingCounter) {
                        // Accept during counter = accept the counter-offer
                        logger::info("BarterOfferMenu: Accept pressed in counter state - accepting counter");
                        inputCooldown = 0.4f;
                        SKSE::GetTaskInterface()->AddTask([]() {
                            BarterManager::GetSingleton()->OnCounterResponse(0);
                        });
                        return RE::UI_MESSAGE_RESULTS::kHandled;
                    }
                    logger::info("BarterOfferMenu: Accept pressed - submitting offer");
                    if (uiMovie) {
                        RE::GFxValue sliderVal;
                        uiMovie->GetVariable(&sliderVal, "_root.sliderValue");
                        if (sliderVal.GetType() == RE::GFxValue::ValueType::kNumber) {
                            int offeredPrice = static_cast<int>(std::round(sliderVal.GetNumber()));
                            inputCooldown = 0.4f;  // Prevent double-input
                            SKSE::GetTaskInterface()->AddTask([offeredPrice]() {
                                BarterManager::GetSingleton()->OnPlayerOffer(offeredPrice);
                            });
                        }
                    }
                    return RE::UI_MESSAGE_RESULTS::kHandled;
                }

                // D-pad / arrow keys: adjust slider by 1 (only in offer state)
                if (!showingCounter && !showingResult &&
                    (eventStr == userEvents->left || eventStr == userEvents->right)) {
                    int dir = (eventStr == userEvents->left) ? -1 : 1;
                    if (uiMovie) {
                        RE::GFxValue arg;
                        arg.SetNumber(static_cast<double>(dir));
                        uiMovie->Invoke("_root.adjustSlider", nullptr, &arg, 1);
                    }
                    gamepadHoldDir = dir;
                    gamepadHoldTimer = 0.0f;
                    return RE::UI_MESSAGE_RESULTS::kHandled;
                }

                // Also catch "StrafeLeft"/"StrafeRight" which D-pad may generate in some contexts
                if (!showingCounter && !showingResult &&
                    (eventStr == userEvents->strafeLeft || eventStr == userEvents->strafeRight)) {
                    lastInputWasGamepad = true;
                    int dir = (eventStr == userEvents->strafeLeft) ? -1 : 1;
                    if (uiMovie) {
                        RE::GFxValue arg;
                        arg.SetNumber(static_cast<double>(dir));
                        uiMovie->Invoke("_root.adjustSlider", nullptr, &arg, 1);
                    }
                    gamepadHoldDir = dir;
                    gamepadHoldTimer = 0.0f;
                    return RE::UI_MESSAGE_RESULTS::kHandled;
                }

                // Bumpers (LB/RB) or PgUp/PgDn: adjust by 5
                if (!showingCounter && !showingResult &&
                    (eventStr == userEvents->prevPage || eventStr == userEvents->nextPage)) {
                    int dir = (eventStr == userEvents->prevPage) ? -5 : 5;
                    if (uiMovie) {
                        RE::GFxValue arg;
                        arg.SetNumber(static_cast<double>(dir));
                        uiMovie->Invoke("_root.adjustSlider", nullptr, &arg, 1);
                    }
                    return RE::UI_MESSAGE_RESULTS::kHandled;
                }

                // Shoulder buttons as alternative large step
                if (!showingCounter && !showingResult &&
                    (eventStr == userEvents->leftEquip || eventStr == userEvents->rightEquip)) {
                    int dir = (eventStr == userEvents->leftEquip) ? -5 : 5;
                    if (uiMovie) {
                        RE::GFxValue arg;
                        arg.SetNumber(static_cast<double>(dir));
                        uiMovie->Invoke("_root.adjustSlider", nullptr, &arg, 1);
                    }
                    return RE::UI_MESSAGE_RESULTS::kHandled;
                }

                // Up/Down: navigate buttons
                if (eventStr == userEvents->up || eventStr == userEvents->down) {
                    gamepadHoldDir = 0;
                    // Use navigateButtons to cycle focus
                    if (uiMovie) {
                        RE::GFxValue arg;
                        arg.SetNumber(eventStr == userEvents->up ? -1.0 : 1.0);
                        uiMovie->Invoke("_root.navigateButtons", nullptr, &arg, 1);
                    }
                    return RE::UI_MESSAGE_RESULTS::kHandled;
                }

                // Unrecognized event - pass through to SWF for keyboard input handling
                gamepadHoldDir = 0;
            }
            // Let unhandled events pass to the SWF's internal input handler
            break;
        }
        default:
            break;
    }
    return RE::IMenu::ProcessMessage(a_message);
}

void BarterOfferMenu::SetOfferData(const OfferData& data) {
    if (!uiMovie) {
        logger::error("BarterOfferMenu::SetOfferData - uiMovie is null!");
        return;
    }

    // Reset all state for a fresh offer
    showingCounter = false;
    showingResult = false;
    sliderDragging = false;
    gamepadHoldDir = 0;
    gamepadHoldTimer = 0.0f;
    gamepadGraceDone = false;
    hoveredButton = -1;
    inputCooldown = 0.3f;  // Prevent the activation key from immediately submitting

    logger::info("BarterOfferMenu::SetOfferData - item='{}', basePrice={}, effectivePrice={}, "
        "merchant='{}', personality='{}', relationship={}, slider=[{}%..{}%]",
        data.itemName, data.basePrice, data.effectivePrice,
        data.merchantName, data.personalityName, data.relationship,
        static_cast<int>(data.sliderMin * 100.0), static_cast<int>(data.sliderMax * 100.0));

    RE::GFxValue args[10];
    args[0].SetString(data.itemName.c_str());
    args[1].SetNumber(data.basePrice);
    args[2].SetNumber(data.effectivePrice);
    args[3].SetString(data.merchantName.c_str());
    args[4].SetString(data.personalityName.c_str());
    args[5].SetNumber(data.relationship);
    // Slider: min=0, max=effectivePrice*1.5 (allow offering above market for relationship building)
    args[6].SetNumber(0);
    int maxOffer = static_cast<int>(std::ceil(data.effectivePrice * 1.5));
    if (maxOffer < 2) maxOffer = 2;
    args[7].SetNumber(static_cast<double>(maxOffer));
    args[8].SetBoolean(data.hasIntimidationPerk);
    args[9].SetNumber(data.acceptanceChance);

    logger::debug("BarterOfferMenu: Invoking _root.SetOfferData with 10 args");
    uiMovie->Invoke("_root.SetOfferData", nullptr, args, 10);

    RE::GFxValue stateArg;
    stateArg.SetString("offer");
    logger::debug("BarterOfferMenu: Invoking _root.setButtonState('offer')");
    uiMovie->Invoke("_root.setButtonState", nullptr, &stateArg, 1);

    // Diagnostic: verify button visibility after setButtonState
    RE::GFxValue btnVis;
    if (uiMovie->GetVariable(&btnVis, "_root.btn_submit._visible")) {
        logger::info("BarterOfferMenu: btn_submit._visible = {}", btnVis.GetBool());
        if (!btnVis.GetBool()) {
            // AS function may have failed - force buttons visible from C++
            logger::warn("BarterOfferMenu: Buttons not visible after setButtonState - forcing from C++");
            RE::GFxValue trueVal;
            trueVal.SetBoolean(true);
            uiMovie->SetVariable("_root.btn_submit._visible", trueVal);
            uiMovie->SetVariable("_root.btn_intimidate._visible", trueVal);
            uiMovie->SetVariable("_root.btn_cancel._visible", trueVal);
        }
    } else {
        logger::error("BarterOfferMenu: Cannot read _root.btn_submit._visible - instance may not exist");
    }
    RE::GFxValue btnAlpha;
    if (uiMovie->GetVariable(&btnAlpha, "_root.btn_submit._alpha")) {
        logger::info("BarterOfferMenu: btn_submit._alpha = {}", btnAlpha.GetNumber());
    }

    auto setHtml = [this](const char* fieldPath, const std::string& html) {
        RE::GFxValue val;
        val.SetString(html.c_str());
        bool success = uiMovie->SetVariable(fieldPath, val);
        if (!success) {
            logger::warn("BarterOfferMenu: SetVariable failed for '{}'", fieldPath);
        }
    };

    // Diagnostic: check if text field instances are accessible
    if (!uiMovie->IsAvailable("_root.MerchantName")) {
        logger::error("BarterOfferMenu: _root.MerchantName not available! SWF display list may be empty.");
    }
    if (!uiMovie->IsAvailable("_root.PriceText")) {
        logger::error("BarterOfferMenu: _root.PriceText not available!");
    }

    auto makeHtml = [](const char* font, int size, const char* color, const std::string& text) {
        return std::format(
            R"(<p align="center"><font face="{}" size="{}" color="{}" kerning="1">{}</font></p>)",
            font, size, color, text);
    };

    setHtml("_root.MerchantName.htmlText",
        makeHtml("$EverywhereBoldFont", 20, "#FFFFFF", data.merchantName));
    setHtml("_root.FlavorText.htmlText",
        makeHtml("$EverywhereMediumFont", 10, "#999999", "What did you have in mind?"));

    // Base price comparison
    setHtml("_root.BasePriceText.htmlText",
        makeHtml("$EverywhereMediumFont", 9, "#A09080",
            std::format("Market Price: {} gold", data.basePrice)));

    setHtml("_root.OfferLabel.htmlText",
        makeHtml("$EverywhereMediumFont", 9, "#808080", "Your Offer"));
    setHtml("_root.PriceText.htmlText",
        makeHtml("$EverywhereBoldFont", 22, "#DAA520",
            std::format("{} gold", static_cast<int>(data.effectivePrice))));
    int maxOfferDisplay = static_cast<int>(std::ceil(data.effectivePrice * 1.5));
    if (maxOfferDisplay < 2) maxOfferDisplay = 2;
    setHtml("_root.SliderText.htmlText",
        makeHtml("$EverywhereMediumFont", 9, "#C8C8C8",
            std::format("0 gold                    {} gold", maxOfferDisplay)));

    // Acceptance chance indicator (colored by likelihood)
    {
        std::string acceptText;
        std::string acceptColor;
        int chance = static_cast<int>(data.acceptanceChance);
        if (chance >= 80) {
            acceptText = "Merchant will ACCEPT this offer";
            acceptColor = "#50B050";
        } else if (chance >= 60) {
            acceptText = "Merchant will likely accept";
            acceptColor = "#80B050";
        } else if (chance >= 40) {
            acceptText = "Merchant looks uncertain";
            acceptColor = "#D4B054";
        } else if (chance >= 20) {
            acceptText = "Merchant looks unimpressed";
            acceptColor = "#D08030";
        } else {
            acceptText = "Merchant will likely REFUSE";
            acceptColor = "#B05050";
        }
        setHtml("_root.AcceptanceText.htmlText",
            makeHtml("$EverywhereMediumFont", 10, acceptColor.c_str(), acceptText));
    }

    // Relationship section
    {
        std::string relText;
        if (data.relationship >= 60) relText = "Trusted";
        else if (data.relationship >= 30) relText = "Friendly";
        else if (data.relationship >= 10) relText = "Warm";
        else if (data.relationship >= -10) relText = "Neutral";
        else if (data.relationship >= -30) relText = "Cool";
        else if (data.relationship >= -60) relText = "Hostile";
        else relText = "Despised";

        std::string relColor;
        if (data.relationship >= 30) relColor = "#50B050";
        else if (data.relationship >= 0) relColor = "#D4B054";
        else relColor = "#B05050";

        setHtml("_root.ReactionText.htmlText",
            std::format(R"(<p align="center"><font face="$EverywhereMediumFont" size="9" color="#A09080">{} merchant</font><font face="$EverywhereMediumFont" size="9" color="{}">  |  {}</font></p>)",
                data.personalityName, relColor, relText));
    }

    // Relationship bar fill (scale 0-100% where 50% = neutral)
    {
        double relNorm = (data.relationship + 100.0) / 200.0 * 100.0; // 0-100 scale
        if (relNorm < 5.0) relNorm = 5.0;
        if (relNorm > 95.0) relNorm = 95.0;
        RE::GFxValue xscale;
        xscale.SetNumber(relNorm);
        uiMovie->SetVariable("_root.relBarMC.fill._xscale", xscale);
    }

    // Relationship effect preview (what this offer at 0% would do)
    setHtml("_root.RelEffectText.htmlText",
        makeHtml("$EverywhereMediumFont", 8, "#807060",
            "Adjust the slider to see the effect on your standing"));

    // Button hints (gamepad + keyboard)
    setHtml("_root.ButtonHintText.htmlText",
        makeHtml("$EverywhereMediumFont", 8, "#606060",
            "[E] Confirm    [Tab] Cancel    [LB/RB] +/-5    [D-pad] Adjust"));

    // Deal history synopsis
    {
        std::string historyText;
        std::string historyColor = "#807060";
        if (!data.recentDealsJson.empty() && data.recentDealsJson != "[]") {
            try {
                auto deals = nlohmann::json::parse(data.recentDealsJson);
                if (deals.is_array() && !deals.empty()) {
                    int totalDeals = static_cast<int>(deals.size());
                    int lowballs = 0, fair = 0, generous = 0;
                    for (auto& d : deals) {
                        int base = d.value("basePrice", 0);
                        int offered = d.value("offeredPrice", 0);
                        if (base > 0) {
                            double ratio = static_cast<double>(offered) / base;
                            if (ratio < 0.7) lowballs++;
                            else if (ratio > 1.1) generous++;
                            else fair++;
                        }
                    }
                    if (lowballs > fair && lowballs > generous) {
                        historyText = "You have LOWBALLED this merchant in the past";
                        historyColor = "#B05050";
                    } else if (generous > fair && generous > lowballs) {
                        historyText = "You have been GENEROUS with this merchant before";
                        historyColor = "#50B050";
                    } else if (totalDeals >= 3) {
                        historyText = "You have traded fairly with this merchant";
                        historyColor = "#A09080";
                    } else {
                        historyText = std::format("({} previous deal{})", totalDeals, totalDeals > 1 ? "s" : "");
                        historyColor = "#706050";
                    }
                }
            } catch (...) {}
        }
        if (!historyText.empty()) {
            setHtml("_root.DealHistoryText.htmlText",
                makeHtml("$EverywhereMediumFont", 8, historyColor.c_str(), historyText));
        }
    }

    // Set button labels with proper HTML font tags
    setHtml("_root.btn_submit.labelField.htmlText",
        makeHtml("$EverywhereMediumFont", 9, "#DAA520", "Submit Offer"));
    setHtml("_root.btn_intimidate.labelField.htmlText",
        makeHtml("$EverywhereMediumFont", 9, "#CC4444", "Intimidate"));
    setHtml("_root.btn_cancel.labelField.htmlText",
        makeHtml("$EverywhereMediumFont", 9, "#A0A0A0", "Cancel"));
    setHtml("_root.btn_accept.labelField.htmlText",
        makeHtml("$EverywhereMediumFont", 9, "#66CC66", "Accept"));
    setHtml("_root.btn_reoffer.labelField.htmlText",
        makeHtml("$EverywhereMediumFont", 9, "#DAA520", "Re-Offer"));
    setHtml("_root.btn_walkaway.labelField.htmlText",
        makeHtml("$EverywhereMediumFont", 9, "#A0A0A0", "Walk Away"));
    setHtml("_root.btn_continue.labelField.htmlText",
        makeHtml("$EverywhereMediumFont", 9, "#FFFFFF", "Continue"));

    logger::info("BarterOfferMenu: SetOfferData applied ({})", data.itemName);
}

void BarterOfferMenu::SetCounterOffer(int amount, int patience) {
    if (!uiMovie) return;

    showingCounter = true;
    showingResult = false;
    inputCooldown = 0.3f;  // Prevent lingering accept from instantly accepting counter

    // Call AS to set state
    RE::GFxValue args[2];
    args[0].SetNumber(amount);
    args[1].SetNumber(patience);
    uiMovie->Invoke("_root.ShowCounterOffer", nullptr, args, 2);

    RE::GFxValue stateArg;
    stateArg.SetString("counter");
    uiMovie->Invoke("_root.setButtonState", nullptr, &stateArg, 1);

    // Hide the ENTIRE barter window (same as result screen)
    RE::GFxValue hideVal;
    hideVal.SetBoolean(false);
    uiMovie->SetVariable("_root.panelBG._visible", hideVal);
    uiMovie->SetVariable("_root.sliderMC._visible", hideVal);
    uiMovie->SetVariable("_root.arrowLeft._visible", hideVal);
    uiMovie->SetVariable("_root.arrowRight._visible", hideVal);
    uiMovie->SetVariable("_root.SliderText._visible", hideVal);
    uiMovie->SetVariable("_root.AcceptanceText._visible", hideVal);
    uiMovie->SetVariable("_root.RelEffectText._visible", hideVal);
    uiMovie->SetVariable("_root.DealHistoryText._visible", hideVal);
    uiMovie->SetVariable("_root.MerchantName._visible", hideVal);
    uiMovie->SetVariable("_root.FlavorText._visible", hideVal);
    uiMovie->SetVariable("_root.ornament._visible", hideVal);
    uiMovie->SetVariable("_root.BasePriceText._visible", hideVal);
    uiMovie->SetVariable("_root.OfferLabel._visible", hideVal);
    uiMovie->SetVariable("_root.coinIcon._visible", hideVal);
    uiMovie->SetVariable("_root.relBarMC._visible", hideVal);
    // Hide all counter buttons too (we show hints instead)
    uiMovie->SetVariable("_root.btn_accept._visible", hideVal);
    uiMovie->SetVariable("_root.btn_reoffer._visible", hideVal);
    uiMovie->SetVariable("_root.btn_walkaway._visible", hideVal);

    auto setHtml = [this](const char* fieldPath, const std::string& html) {
        RE::GFxValue val;
        val.SetString(html.c_str());
        uiMovie->SetVariable(fieldPath, val);
    };

    // Show only the counter text fields
    RE::GFxValue showVal;
    showVal.SetBoolean(true);
    uiMovie->SetVariable("_root.PriceText._visible", showVal);
    uiMovie->SetVariable("_root.StatusText._visible", showVal);
    uiMovie->SetVariable("_root.ReactionText._visible", showVal);
    uiMovie->SetVariable("_root.ButtonHintText._visible", showVal);

    // Prominent counter-offer display
    std::string counterHtml = std::format(
        R"(<p align="center"><font face="$EverywhereBoldFont" size="22" color="#FFCC00">Counter: {} gold</font></p>)",
        amount);
    std::string patienceHtml = std::format(
        R"(<p align="center"><font face="$EverywhereMediumFont" size="11" color="#C8C8C8">Patience remaining: {}</font></p>)",
        patience);

    setHtml("_root.PriceText.htmlText", counterHtml);
    setHtml("_root.StatusText.htmlText",
        R"(<p align="center"><font face="$EverywhereMediumFont" size="12" color="#E0C080">The merchant makes a counter-offer</font></p>)");
    setHtml("_root.ReactionText.htmlText", patienceHtml);

    // Button hints for counter state
    RE::GFxValue hintVal;
    if (lastInputWasGamepad) {
        hintVal.SetString(R"(<p align="center"><font face="$EverywhereMediumFont" size="8" color="#606060">[A] Accept    [X] Re-offer    [B] Walk Away</font></p>)");
    } else {
        hintVal.SetString(R"(<p align="center"><font face="$EverywhereMediumFont" size="8" color="#606060">[E] Accept    [R] Re-offer    [Tab] Walk Away</font></p>)");
    }
    uiMovie->SetVariable("_root.ButtonHintText.htmlText", hintVal);

    logger::info("BarterOfferMenu: SetCounterOffer - {} gold, patience {}", amount, patience);
}

void BarterOfferMenu::SetResult(bool accepted, int amount) {
    if (!uiMovie) return;

    showingResult = true;
    showingCounter = false;
    lastResultAccepted = accepted;
    inputCooldown = 0.5f;

    // Hide the ENTIRE barter window (panel, decorations, all elements)
    RE::GFxValue hideVal;
    hideVal.SetBoolean(false);
    uiMovie->SetVariable("_root.panelBG._visible", hideVal);
    uiMovie->SetVariable("_root.sliderMC._visible", hideVal);
    uiMovie->SetVariable("_root.arrowLeft._visible", hideVal);
    uiMovie->SetVariable("_root.arrowRight._visible", hideVal);
    uiMovie->SetVariable("_root.SliderText._visible", hideVal);
    uiMovie->SetVariable("_root.AcceptanceText._visible", hideVal);
    uiMovie->SetVariable("_root.RelEffectText._visible", hideVal);
    uiMovie->SetVariable("_root.DealHistoryText._visible", hideVal);
    uiMovie->SetVariable("_root.ButtonHintText._visible", hideVal);
    uiMovie->SetVariable("_root.MerchantName._visible", hideVal);
    uiMovie->SetVariable("_root.FlavorText._visible", hideVal);
    uiMovie->SetVariable("_root.ornament._visible", hideVal);
    uiMovie->SetVariable("_root.BasePriceText._visible", hideVal);
    uiMovie->SetVariable("_root.OfferLabel._visible", hideVal);
    uiMovie->SetVariable("_root.coinIcon._visible", hideVal);
    uiMovie->SetVariable("_root.relBarMC._visible", hideVal);

    // Hide all buttons
    RE::GFxValue stateArg;
    stateArg.SetString("result");
    uiMovie->Invoke("_root.setButtonState", nullptr, &stateArg, 1);
    // Also hide the continue button (we don't need it)
    uiMovie->SetVariable("_root.btn_continue._visible", hideVal);

    auto setHtml = [this](const char* fieldPath, const std::string& html) {
        RE::GFxValue val;
        val.SetString(html.c_str());
        uiMovie->SetVariable(fieldPath, val);
    };

    // Show only the result text fields (PriceText, StatusText, ReactionText are still visible)
    RE::GFxValue showVal;
    showVal.SetBoolean(true);
    uiMovie->SetVariable("_root.PriceText._visible", showVal);
    uiMovie->SetVariable("_root.StatusText._visible", showVal);
    uiMovie->SetVariable("_root.ReactionText._visible", showVal);

    if (accepted) {
        std::string priceHtml = std::format(
            R"(<p align="center"><font face="$EverywhereBoldFont" size="28" color="#50FF50">ACCEPTED</font></p>)");
        std::string amountHtml = std::format(
            R"(<p align="center"><font face="$EverywhereBoldFont" size="20" color="#FFD700">{} gold</font></p>)",
            amount);
        std::string subHtml =
            R"(<p align="center"><font face="$EverywhereMediumFont" size="10" color="#90C890">Deal complete</font></p>)";

        setHtml("_root.PriceText.htmlText", priceHtml);
        setHtml("_root.StatusText.htmlText", amountHtml);
        setHtml("_root.ReactionText.htmlText", subHtml);
    } else {
        std::string priceHtml =
            R"(<p align="center"><font face="$EverywhereBoldFont" size="28" color="#FF5050">REJECTED</font></p>)";
        std::string relHtml = std::format(
            R"(<p align="center"><font face="$EverywhereMediumFont" size="12" color="#FF8080">Relationship {}</font></p>)",
            amount);
        std::string subHtml;
        if (lastInputWasGamepad) {
            subHtml = R"(<p align="center"><font face="$EverywhereMediumFont" size="9" color="#A0A0A0">Press [A] to retry, [B] to cancel</font></p>)";
        } else {
            subHtml = R"(<p align="center"><font face="$EverywhereMediumFont" size="9" color="#A0A0A0">Press [E] to retry, [Tab] to cancel</font></p>)";
        }

        setHtml("_root.PriceText.htmlText", priceHtml);
        setHtml("_root.StatusText.htmlText", relHtml);
        setHtml("_root.ReactionText.htmlText", subHtml);
    }
}

void BarterOfferMenu::RestoreOfferUI() {
    if (!uiMovie) return;

    // Clear state flags
    showingCounter = false;
    showingResult = false;
    gamepadHoldDir = 0;
    gamepadHoldTimer = 0.0f;
    gamepadGraceDone = false;
    sliderDragging = false;
    hoveredButton = -1;

    // Tell the BarterManager we're retrying
    BarterManager::GetSingleton()->RetryOffer();

    // Restore button state to "offer"
    RE::GFxValue stateArg;
    stateArg.SetString("offer");
    uiMovie->Invoke("_root.setButtonState", nullptr, &stateArg, 1);

    // Show elements that were hidden during counter/result
    RE::GFxValue showVal;
    showVal.SetBoolean(true);
    // Panel structure
    uiMovie->SetVariable("_root.panelBG._visible", showVal);
    uiMovie->SetVariable("_root.MerchantName._visible", showVal);
    uiMovie->SetVariable("_root.FlavorText._visible", showVal);
    uiMovie->SetVariable("_root.ornament._visible", showVal);
    uiMovie->SetVariable("_root.BasePriceText._visible", showVal);
    uiMovie->SetVariable("_root.OfferLabel._visible", showVal);
    uiMovie->SetVariable("_root.coinIcon._visible", showVal);
    uiMovie->SetVariable("_root.relBarMC._visible", showVal);
    uiMovie->SetVariable("_root.PriceText._visible", showVal);
    uiMovie->SetVariable("_root.StatusText._visible", showVal);
    uiMovie->SetVariable("_root.ReactionText._visible", showVal);
    // Offer-specific elements
    uiMovie->SetVariable("_root.sliderMC._visible", showVal);
    uiMovie->SetVariable("_root.arrowLeft._visible", showVal);
    uiMovie->SetVariable("_root.arrowRight._visible", showVal);
    uiMovie->SetVariable("_root.SliderText._visible", showVal);
    uiMovie->SetVariable("_root.AcceptanceText._visible", showVal);
    uiMovie->SetVariable("_root.RelEffectText._visible", showVal);
    uiMovie->SetVariable("_root.DealHistoryText._visible", showVal);
    uiMovie->SetVariable("_root.ButtonHintText._visible", showVal);

    // Clear result/counter-specific text
    RE::GFxValue emptyVal;
    emptyVal.SetString("");
    uiMovie->SetVariable("_root.StatusText.htmlText", emptyVal);
    uiMovie->SetVariable("_root.ReactionText.htmlText", emptyVal);

    // Enforce slider clamp before restoring display
    RE::GFxValue sliderVal, sliderMinV, sliderMaxV;
    uiMovie->GetVariable(&sliderVal, "_root.sliderValue");
    uiMovie->GetVariable(&sliderMinV, "_root.sliderMin");
    uiMovie->GetVariable(&sliderMaxV, "_root.sliderMax");
    if (sliderVal.GetType() == RE::GFxValue::ValueType::kNumber) {
        double val = sliderVal.GetNumber();
        double minV = (sliderMinV.GetType() == RE::GFxValue::ValueType::kNumber) ? sliderMinV.GetNumber() : 0.0;
        double maxV = (sliderMaxV.GetType() == RE::GFxValue::ValueType::kNumber) ? sliderMaxV.GetNumber() : 9999.0;
        double clamped = std::clamp(val, minV, maxV);
        if (val != clamped) {
            RE::GFxValue clampedVal;
            clampedVal.SetNumber(clamped);
            uiMovie->SetVariable("_root.sliderValue", clampedVal);
        }
    }

    // Restore price display from current slider value
    uiMovie->Invoke("_root.updateSliderDisplay", nullptr, nullptr, 0);

    // Restore input hints for offer state
    RE::GFxValue hintVal;
    if (lastInputWasGamepad) {
        hintVal.SetString(R"(<p align="center"><font face="$EverywhereMediumFont" size="8" color="#606060">[A] Confirm    [B] Cancel    [LB/RB] +/-5    [D-pad] Adjust</font></p>)");
    } else {
        hintVal.SetString(R"(<p align="center"><font face="$EverywhereMediumFont" size="8" color="#606060">[E] Confirm    [Tab] Cancel    [LB/RB] +/-5    [Arrow Keys] Adjust</font></p>)");
    }
    uiMovie->SetVariable("_root.ButtonHintText.htmlText", hintVal);

    logger::info("BarterOfferMenu: RestoreOfferUI - back to offer state");
}

void BarterOfferMenu::FxDelegateCallback::Accept(CallbackProcessor* a_cbReg) {
    a_cbReg->Process("OfferSubmit", OnOfferSubmit);
    a_cbReg->Process("CounterResponse", OnCounterResponse);
    a_cbReg->Process("IntimidateAttempt", OnIntimidateAttempt);
    a_cbReg->Process("CloseMenu", OnClose);
}

void BarterOfferMenu::OnOfferSubmit(const RE::FxDelegateArgs& a_params) {
    if (a_params.GetArgCount() < 1) return;
    int offeredPrice = static_cast<int>(a_params[0].GetNumber());
    logger::info("BarterOfferMenu: OfferSubmit received - price={}", offeredPrice);
    SKSE::GetTaskInterface()->AddTask([offeredPrice]() {
        BarterManager::GetSingleton()->OnPlayerOffer(offeredPrice);
    });
}

void BarterOfferMenu::OnCounterResponse(const RE::FxDelegateArgs& a_params) {
    if (a_params.GetArgCount() < 1) return;
    int response = static_cast<int>(a_params[0].GetNumber());
    logger::info("BarterOfferMenu: CounterResponse received - response={}", response);
    SKSE::GetTaskInterface()->AddTask([response]() {
        BarterManager::GetSingleton()->OnCounterResponse(response);
    });
}

void BarterOfferMenu::OnIntimidateAttempt(const RE::FxDelegateArgs&) {
    logger::info("BarterOfferMenu: IntimidateAttempt received");
    SKSE::GetTaskInterface()->AddTask([]() {
        BarterManager::GetSingleton()->OnIntimidateAttempt();
    });
}

void BarterOfferMenu::OnClose(const RE::FxDelegateArgs&) {
    logger::info("BarterOfferMenu: CloseMenu received from SWF");
    SKSE::GetTaskInterface()->AddTask([]() {
        BarterManager::GetSingleton()->OnCancelled();
    });
}

// ScaleformUIImpl

bool ScaleformUIImpl::Initialize() {
    BarterOfferMenu::Register();
    logger::info("ScaleformUIImpl: BarterOfferMenu registered");
    return true;
}

void ScaleformUIImpl::ShowOffer(const OfferData& data) {
    logger::info("ScaleformUIImpl::ShowOffer called for '{}'", data.itemName);

    // Store the data for deferred application - AdvanceMovie will pick it up
    BarterOfferMenu::pendingOfferData = data;

    SKSE::GetTaskInterface()->AddUITask([data]() {
        auto* ui = RE::UI::GetSingleton();
        if (!ui) return;

        auto menu = ui->GetMenu<BarterOfferMenu>(BarterOfferMenu::MENU_NAME);
        if (menu) {
            // Menu already exists, apply data directly
            logger::info("ScaleformUIImpl: Menu already exists, setting offer data directly");
            menu->SetOfferData(data);
            BarterOfferMenu::pendingOfferData.reset();
        } else {
            // Menu doesn't exist yet - Show() will create it, AdvanceMovie will apply pending data
            logger::info("ScaleformUIImpl: Showing menu, data will be applied in AdvanceMovie");
            BarterOfferMenu::Show();
        }
    });
}

void ScaleformUIImpl::ShowCounterOffer(int counterAmount, int patience) {
    SKSE::GetTaskInterface()->AddUITask([counterAmount, patience]() {
        auto* ui = RE::UI::GetSingleton();
        if (ui) {
            auto menu = ui->GetMenu<BarterOfferMenu>(BarterOfferMenu::MENU_NAME);
            if (menu) {
                menu->SetCounterOffer(counterAmount, patience);
            }
        }
    });
}

void ScaleformUIImpl::ShowResult(bool accepted, int relDelta) {
    SKSE::GetTaskInterface()->AddUITask([accepted, relDelta]() {
        auto* ui = RE::UI::GetSingleton();
        if (ui) {
            auto menu = ui->GetMenu<BarterOfferMenu>(BarterOfferMenu::MENU_NAME);
            if (menu) {
                menu->SetResult(accepted, relDelta);
            }
        }
    });
}

void ScaleformUIImpl::Hide() {
    SKSE::GetTaskInterface()->AddUITask([]() {
        // Guard against a stale close: if the player started a new barter
        // interaction while this hide was queued (e.g. clicked another item
        // right after a deal closed), the offer menu is being reused for the
        // new offer. Closing it now would make the freshly-opened window blink
        // and vanish. Only actually close when the barter is truly idle.
        auto st = BarterManager::GetSingleton()->GetState();
        if (st != BarterState::Idle) {
            logger::info("BarterOfferMenu: Skipping stale Hide - new interaction active (state={})",
                static_cast<int>(st));
            return;
        }
        BarterOfferMenu::Hide();
    });
}
