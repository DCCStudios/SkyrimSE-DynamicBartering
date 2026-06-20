#pragma once
#include "UI/UIBridge.h"

typedef uint64_t PrismaView;

namespace PRISMA_UI_API {
    typedef void (*OnDomReadyCallback)(PrismaView view);
    typedef void (*JSCallback)(const char* result);
    typedef void (*JSListenerCallback)(const char* argument);

    class IVPrismaUI1 {
    public:
        virtual PrismaView CreateView(const char* htmlPath, OnDomReadyCallback onDomReadyCallback = nullptr) noexcept = 0;
        virtual void Invoke(PrismaView view, const char* script, JSCallback callback = nullptr) noexcept = 0;
        virtual void InteropCall(PrismaView view, const char* functionName, const char* argument) noexcept = 0;
        virtual void RegisterJSListener(PrismaView view, const char* functionName, JSListenerCallback callback) noexcept = 0;
        virtual bool HasFocus(PrismaView view) noexcept = 0;
        virtual bool Focus(PrismaView view, bool pauseGame = false, bool disableFocusMenu = false) noexcept = 0;
        virtual void Unfocus(PrismaView view) noexcept = 0;
        virtual void Show(PrismaView view) noexcept = 0;
        virtual void Hide(PrismaView view) noexcept = 0;
        virtual bool IsHidden(PrismaView view) noexcept = 0;
        virtual int GetScrollingPixelSize(PrismaView view) noexcept = 0;
        virtual void SetScrollingPixelSize(PrismaView view, int pixelSize) noexcept = 0;
        virtual bool IsValid(PrismaView view) noexcept = 0;
        virtual void Destroy(PrismaView view) noexcept = 0;
        virtual void SetOrder(PrismaView view, int order) noexcept = 0;
        virtual int GetOrder(PrismaView view) noexcept = 0;
        virtual void CreateInspectorView(PrismaView view) noexcept = 0;
        virtual void SetInspectorVisibility(PrismaView view, bool visible) noexcept = 0;
        virtual bool IsInspectorVisible(PrismaView view) noexcept = 0;
        virtual void SetInspectorBounds(PrismaView view, float topLeftX, float topLeftY, unsigned int width, unsigned int height) noexcept = 0;
        virtual bool HasAnyActiveFocus() noexcept = 0;
    };

    enum class InterfaceVersion : uint8_t { V1 };
    typedef void* (*_RequestPluginAPI)(const InterfaceVersion interfaceVersion);
}

class PrismaUIImpl : public IBarterUI {
public:
    bool Initialize() override;
    void ShowOffer(const OfferData& data) override;
    void ShowCounterOffer(int counterAmount, int patience) override;
    void ShowResult(bool accepted, int relDelta) override;
    void Hide() override;
    bool IsAvailable() const override;

private:
    PRISMA_UI_API::IVPrismaUI1* api = nullptr;
    PrismaView view = 0;
    bool initialized = false;

    static void OnDomReady(PrismaView view);
    static void OnBarterResult(const char* argument);
    static void OnCounterResponse(const char* argument);
    static void OnIntimidateAttempt(const char* argument);
};
