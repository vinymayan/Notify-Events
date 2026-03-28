#include <unordered_set>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>

struct BSFixedStringHash {
    std::size_t operator()(const RE::BSFixedString& a_string) const noexcept {
        return RE::BSCRC32_<RE::BSFixedString>()(a_string);
    }
};

class NotifyAnimationGraphHook
{
public:
    static inline std::unordered_set<RE::BSFixedString, BSFixedStringHash> FakeEventsList;

    static void LoadCustomEvents()
    {
        namespace fs = std::filesystem;
        std::string bdiPath = "Data/SKSE/Plugins/BehaviorDataInjector";

        // Verifica se a pasta existe
        if (!fs::exists(bdiPath) || !fs::is_directory(bdiPath)) {
            SKSE::log::warn("A pasta do BDI nao foi encontrada: {}", bdiPath);
            return;
        }

        // Itera sobre todos os arquivos dentro da pasta
        for (const auto& entry : fs::directory_iterator(bdiPath)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {

                std::ifstream ifs(entry.path().string());
                if (!ifs.is_open()) {
                    SKSE::log::error("Nao foi possivel abrir o arquivo: {}", entry.path().string());
                    continue;
                }

                // Carrega o arquivo no RapidJSON
                rapidjson::IStreamWrapper isw(ifs);
                rapidjson::Document doc;
                doc.ParseStream(isw);

                if (doc.HasParseError() || !doc.IsArray()) {
                    SKSE::log::warn("Arquivo JSON invalido ou nao e um Array: {}", entry.path().filename().string());
                    continue;
                }

                for (const auto& item : doc.GetArray()) {
                    if (item.IsObject()) {

                        if (item.HasMember("type") && item["type"].IsString() &&
                            item.HasMember("notify") && item["notify"].IsBool() &&
                            item.HasMember("name") && item["name"].IsString()) {

                            std::string type = item["type"].GetString();
                            bool notify = item["notify"].GetBool();

                            if (type == "kEvent" && notify) {
                                std::string eventName = item["name"].GetString();
                                FakeEventsList.insert(eventName);
                                //SKSE::log::info("-> Evento BDI registrado para Fake Dispatch: {}", eventName);
                            }
                        }
                    }
                }
            }
        }

        SKSE::log::info("Eventsloaded: {}", FakeEventsList.size());
    }
    static void Install()
    {
        // 1. Hook para Objetos Animados (Móveis, Portas, etc)
        REL::Relocation<std::uintptr_t> refrVtbl{ RE::VTABLE_TESObjectREFR[3] };
        _NotifyAnimationGraph_REFR = refrVtbl.write_vfunc(0x1, NotifyAnimationGraph_REFR);

        // 2. Hook para NPCs, Criaturas, Montarias (tudo que herda de Character)
        REL::Relocation<std::uintptr_t> charVtbl{ RE::VTABLE_Character[3] };
        _NotifyAnimationGraph_Char = charVtbl.write_vfunc(0x1, NotifyAnimationGraph_Char);

        // 3. Hook para o Player (que tem sua própria VTable que herda de Character)
        REL::Relocation<std::uintptr_t> playerVtbl{ RE::VTABLE_PlayerCharacter[3] };
        _NotifyAnimationGraph_Player = playerVtbl.write_vfunc(0x1, NotifyAnimationGraph_Player);

        //SKSE::log::info("Hooks de NotifyAnimationGraph instalados para REFR, Character e Player.");
    }

private:

    static bool NotifyAnimationGraph_REFR(RE::IAnimationGraphManagerHolder* a_this, const RE::BSFixedString& a_eventName)
    {
        ProcessFakeEvent("REFR", a_this, a_eventName);
        return _NotifyAnimationGraph_REFR(a_this, a_eventName);
    }

    static bool NotifyAnimationGraph_Char(RE::IAnimationGraphManagerHolder* a_this, const RE::BSFixedString& a_eventName)
    {
        ProcessFakeEvent("Character", a_this, a_eventName);
        return _NotifyAnimationGraph_Char(a_this, a_eventName);
    }

    static bool NotifyAnimationGraph_Player(RE::IAnimationGraphManagerHolder* a_this, const RE::BSFixedString& a_eventName)
    {
        ProcessFakeEvent("Player", a_this, a_eventName);
        return _NotifyAnimationGraph_Player(a_this, a_eventName);
    }

    static void ProcessFakeEvent(const char* a_type, RE::IAnimationGraphManagerHolder* a_this, const RE::BSFixedString& a_eventName)
    {
        if (a_eventName.empty()) return;

        if (FakeEventsList.contains(a_eventName)) {
            auto* refr = skyrim_cast<RE::TESObjectREFR*>(a_this);

            if (refr) {
                RE::BSAnimationGraphEvent fakeEvent{ a_eventName, refr, "" };

                RE::BSTSmartPointer<RE::BSAnimationGraphManager> graphManager;
                if (a_this->GetAnimationGraphManager(graphManager) && graphManager) {
                    for (auto& graph : graphManager->graphs) {
                        if (graph) {
                            auto* eventSource = static_cast<RE::BSTEventSource<RE::BSAnimationGraphEvent>*>(graph.get());
                            if (eventSource) {
                                eventSource->SendEvent(&fakeEvent);
                            }
                        }
                    }
                }
            }
        }
    }

    static inline REL::Relocation<decltype(&NotifyAnimationGraph_REFR)> _NotifyAnimationGraph_REFR;
    static inline REL::Relocation<decltype(&NotifyAnimationGraph_Char)> _NotifyAnimationGraph_Char;
    static inline REL::Relocation<decltype(&NotifyAnimationGraph_Player)> _NotifyAnimationGraph_Player;
};