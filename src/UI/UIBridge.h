#pragma once

struct OfferData;
enum class UIMode : int;

class IBarterUI {
public:
    virtual ~IBarterUI() = default;
    virtual bool Initialize() = 0;
    virtual void ShowOffer(const OfferData& data) = 0;
    virtual void ShowCounterOffer(int counterAmount, int patience) = 0;
    virtual void ShowResult(bool accepted, int goldAmount, int relDelta) = 0;
    // Dramatic "the merchant caved" screen shown after a SUCCESSFUL intimidation (the
    // deal is already done). Backends that don't customize it fall back to the standard
    // accepted-result screen. buying=true means the player coerced a cheaper purchase;
    // false means a richer sale.
    virtual void ShowIntimidationSuccess(int coercedPrice, int relDelta, bool buying) {
        ShowResult(true, coercedPrice, relDelta);
    }
    // Live-update the relationship meter while an offer window is open (e.g. after a
    // failed intimidation or an SKSE-menu debug change). Default no-op for backends
    // that don't render a meter.
    virtual void UpdateRelationship(int effectiveRelationship) {}
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
    void ShowResult(bool accepted, int goldAmount, int relDelta);
    void ShowIntimidationSuccess(int coercedPrice, int relDelta, bool buying);
    void UpdateRelationship(int effectiveRelationship);
    void Hide();

    IBarterUI* GetActiveUI() const { return activeUI; }

private:
    UIBridge() = default;
    IBarterUI* activeUI = nullptr;
    std::unique_ptr<IBarterUI> scaleformUI;
    std::unique_ptr<IBarterUI> prismaUI;
};
