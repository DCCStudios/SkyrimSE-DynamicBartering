#pragma once

struct OfferData;
enum class UIMode : int;

class IBarterUI {
public:
    virtual ~IBarterUI() = default;
    virtual bool Initialize() = 0;
    virtual void ShowOffer(const OfferData& data) = 0;
    virtual void ShowCounterOffer(int counterAmount, int patience) = 0;
    virtual void ShowResult(bool accepted, int relDelta) = 0;
    virtual void Hide() = 0;
    virtual bool IsAvailable() const = 0;
};

class UIBridge {
public:
    static UIBridge* GetSingleton() {
        static UIBridge instance;
        return &instance;
    }

    void Initialize();
    void SwitchMode(UIMode newMode);
    void ShowOffer(const OfferData& data);
    void ShowCounterOffer(int counterAmount, int patience);
    void ShowResult(bool accepted, int relDelta);
    void Hide();

    IBarterUI* GetActiveUI() const { return activeUI; }

private:
    UIBridge() = default;
    IBarterUI* activeUI = nullptr;
    std::unique_ptr<IBarterUI> scaleformUI;
    std::unique_ptr<IBarterUI> prismaUI;
};
