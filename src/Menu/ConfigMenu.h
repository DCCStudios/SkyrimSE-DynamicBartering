#pragma once

class ConfigMenu {
public:
    static void Register();

private:
    static void RenderMenu();
    static void RenderGeneralTab();
    static void RenderPricingTab();
    static void RenderRelationshipsTab();
    static void RenderPersonalitiesTab();
    static void RenderDebugTab();
};
