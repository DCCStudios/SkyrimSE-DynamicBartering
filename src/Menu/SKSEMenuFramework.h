#pragma once
// SKSEMenuFramework.h - Lightweight interface header for SKSE Menu Framework
// This connects to SKSEMenuFramework.dll at runtime via GetProcAddress.
// The full ImGui API is provided by the framework DLL itself.

#include <windows.h>
#include <string>
#include <filesystem>
#include <atomic>

namespace SKSEMenuFramework {

    namespace Model {
        class WindowInterface {
        public:
            std::atomic<bool> IsOpen{false};
        };
        typedef void(__stdcall* RenderFunction)();
        using ActionFunction = void (*)();
        using AddWindowFunction = Model::WindowInterface* (*)(RenderFunction);
        using AddSectionItemFunction = void (*)(const char* path, RenderFunction rendererFunction);
    }

    namespace Internal {
        template <class T>
        T GetFunction(LPCSTR name) {
            static auto menuFramework = GetModuleHandle(L"SKSEMenuFramework");
            if (!menuFramework) return nullptr;
            return reinterpret_cast<T>(GetProcAddress(menuFramework, name));
        }
        inline std::string key;
    }

    inline bool IsInstalled() {
        return GetModuleHandle(L"SKSEMenuFramework") != nullptr;
    }

    inline void SetSection(std::string key) { Internal::key = key; }

    inline void AddSectionItem(std::string menu, Model::RenderFunction rendererFunction) {
        static auto func = Internal::GetFunction<Model::AddSectionItemFunction>("AddSectionItem");
        if (func) func((Internal::key + "/" + menu).c_str(), rendererFunction);
    }

    inline Model::WindowInterface* AddWindow(Model::RenderFunction rendererFunction) {
        static auto func = Internal::GetFunction<Model::AddWindowFunction>("AddWindow");
        if (func) return func(rendererFunction);
        return nullptr;
    }
}
