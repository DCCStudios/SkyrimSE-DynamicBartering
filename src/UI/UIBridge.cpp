#include "PCH.h"
#include "UI/UIBridge.h"
#include "UI/ScaleformUI.h"
#include "UI/PrismaUI.h"
#include "Settings.h"
#include "BarterManager.h"

void UIBridge::Initialize() {
    scaleformUI = std::make_unique<ScaleformUIImpl>();
    prismaUI = std::make_unique<PrismaUIImpl>();

    auto* settings = Settings::GetSingleton();
    UIMode mode = settings->uiMode;

    // PrismaUI frontend is deprecated for now (its selector is hidden in the SKSE menu).
    // Redirect any stored PrismaUI preference to the Scaleform renderer so an old INI /
    // save can't strand the player on the unsupported UI with no in-game way to switch.
    // The PrismaUI backend is left intact and can be re-enabled later. We do NOT persist
    // this change, so the original preference returns automatically if PrismaUI is restored.
    if (mode == UIMode::PrismaUI) {
        logger::info("UIBridge: PrismaUI mode is deprecated - using Scaleform renderer instead");
        mode = UIMode::ScaleformSWF;
    }

    logger::info("UIBridge: Initializing with mode={}", static_cast<int>(mode));

    // Always initialize both UIs so runtime switching works
    bool scaleformOk = scaleformUI->Initialize();
    bool prismaOk = prismaUI->Initialize();

    if (scaleformOk) {
        logger::info("UIBridge: ScaleformUI initialized successfully");
    }
    if (prismaOk) {
        logger::info("UIBridge: PrismaUI initialized successfully");
    }

    // Select active UI based on configured mode
    if (mode == UIMode::PrismaUI && prismaOk) {
        activeUI = prismaUI.get();
        logger::info("UIBridge: PrismaUI active");
    } else if (mode == UIMode::ScaleformSWF && scaleformOk) {
        activeUI = scaleformUI.get();
        logger::info("UIBridge: ScaleformUI active");
    } else if (mode == UIMode::Auto) {
        if (scaleformOk) {
            activeUI = scaleformUI.get();
            logger::info("UIBridge: ScaleformUI active (auto)");
        } else if (prismaOk) {
            activeUI = prismaUI.get();
            logger::info("UIBridge: PrismaUI active (auto fallback)");
        }
    } else {
        // Fallback: use whatever is available
        if (scaleformOk) {
            activeUI = scaleformUI.get();
            logger::info("UIBridge: ScaleformUI active (fallback)");
        } else if (prismaOk) {
            activeUI = prismaUI.get();
            logger::info("UIBridge: PrismaUI active (fallback)");
        }
    }

    if (!activeUI) {
        logger::error("UIBridge: No UI backend available!");
    }
}

void UIBridge::ShowOffer(const OfferData& data) {
    if (activeUI) activeUI->ShowOffer(data);
}

void UIBridge::ShowCounterOffer(int counterAmount, int patience) {
    if (activeUI) activeUI->ShowCounterOffer(counterAmount, patience);
}

void UIBridge::ShowResult(bool accepted, int goldAmount, int relDelta) {
    if (activeUI) activeUI->ShowResult(accepted, goldAmount, relDelta);
}

void UIBridge::ShowIntimidationSuccess(int coercedPrice, int relDelta, bool buying) {
    if (activeUI) activeUI->ShowIntimidationSuccess(coercedPrice, relDelta, buying);
}

void UIBridge::UpdateRelationship(int effectiveRelationship) {
    if (activeUI) activeUI->UpdateRelationship(effectiveRelationship);
}

void UIBridge::Hide() {
    if (activeUI) activeUI->Hide();
}

void UIBridge::SwitchMode(UIMode newMode) {
    if (activeUI) {
        activeUI->Hide();
    }

    switch (newMode) {
        case UIMode::PrismaUI:
            if (prismaUI && prismaUI->IsAvailable()) {
                activeUI = prismaUI.get();
                logger::info("UIBridge: Switched to PrismaUI");
            } else {
                logger::warn("UIBridge: PrismaUI not available, keeping current");
            }
            break;
        case UIMode::ScaleformSWF:
            if (scaleformUI && scaleformUI->IsAvailable()) {
                activeUI = scaleformUI.get();
                logger::info("UIBridge: Switched to ScaleformUI");
            }
            break;
        case UIMode::Auto:
        default:
            if (prismaUI && prismaUI->IsAvailable()) {
                activeUI = prismaUI.get();
            } else if (scaleformUI && scaleformUI->IsAvailable()) {
                activeUI = scaleformUI.get();
            }
            logger::info("UIBridge: Auto-selected UI backend");
            break;
    }

    Settings::GetSingleton()->uiMode = newMode;
}
