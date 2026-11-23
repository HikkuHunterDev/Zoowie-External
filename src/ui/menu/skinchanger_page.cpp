#include <stdafx.hpp>
#include <ui/menu/menu.hpp>
#include <ui/menu/internal.hpp>
#include <ui/menu/card_metrics.hpp>
#include <app/skinchanger_ipc.h>
#include <config/settings.hpp>
#include <imgui.h>
#include <algorithm>
#include <string>
#include <vector>
#include <cstring>
#include <nlohmann/json.hpp>

namespace ui
{
using namespace ui::menu::detail;

namespace {

struct ScState {
    int cat = 0; // 0 Weapons,1 Knives,2 Gloves
    int item = -1;
    int paint = -1;
    int sticker = -1;
    int patch = -1;
    int charm = -1;
    float wear = 0.f;
    int seed = 0;
    bool stattrak = false;
    int stattrak_count = 0;
    char name[64]{};
    skinchanger_ipc::Catalog catalog;
    skinchanger_ipc::State state;
    std::string status;
    double last_refresh = 0.0;
};
ScState &sc() { static ScState s; return s; }

int kindForCat(int cat) { switch(cat){ case 0: return 0; case 1: return 1; case 2: return 2; default: return 0; } }

std::vector<const skinchanger_ipc::CatalogItem*> itemsFor(int cat) {
    std::vector<const skinchanger_ipc::CatalogItem*> out;
    int k = kindForCat(cat);
    for (auto &it : sc().catalog.Items) if (it.Kind == k) out.push_back(&it);
    std::sort(out.begin(), out.end(), [](auto*a,auto*b){ return a->Name < b->Name; });
    return out;
}

const skinchanger_ipc::CatalogItem* findCatalogItem(uint16_t def) {
    for (auto &it : sc().catalog.Items) if (it.Definition == def) return &it;
    return nullptr;
}

uint8_t pickTeam(const skinchanger_ipc::CatalogItem* it) {
    auto &c = sc().catalog;
    if (!it || it->Teams == 0) return c.AnyTeam;
    if (it->Teams & (1u << c.CounterTerrorists)) return c.CounterTerrorists;
    if (it->Teams & (1u << c.Terrorists)) return c.Terrorists;
    return c.AnyTeam;
}

bool isRu() { return config::general_settings.language == 1; }
const char* tr(const char* en, const char* ru) { return isRu() ? ru : en; }

static std::string ruPaintName(const std::string &internal) {
    static const std::pair<const char*, const char*> m[] = {
        {"fade","Градиент"},{"doppler","Допплер"},{"marble_fade","Мраморный градиент"},{"tiger_tooth","Зуб тигра"},
        {"crimson_web","Кровавая паутина"},{"slaughter","Бойня"},{"case_hardened","Закалённое в боях"},
        {"blue_steel","Воронёная сталь"},{"stained","Пятнистый"},{"scorched","Опалённый"},{"boreal_forest","Северный лес"},
        {"urban_masked","Городская маскировка"},{"forest_ddpat","Лесной DDPAT"},{"night","Ночь"},{"safari_mesh","Сетка сафари"},
        {"rust_coat","Ржавое покрытие"},{"ultraviolet","Ультрафиолет"},{"lore","Лор"},{"autotronic","Автотроника"},
        {"gamma_doppler","Гамма-допплер"},{"freehand","Ручная роспись"},{"black_laminate","Чёрный глянец"},
        {"bright_water","Яркая вода"},{"damascus_steel","Дамасская сталь"},{"emerald","Изумруд"},{"ruby","Рубин"},
        {"sapphire","Сапфир"},{"vanilla","Ваниль"},{"asiimov","Азимов"},{"redline","Красная линия"},
        {"hyper_beast","Гиперзверь"},{"howl","Вой"},{"dragon_lore","История о драконе"},{"medusa","Медуза"},
        {"gungnir","Гунгнир"},{"neo_noir","Неонуар"},{"printstream","Поток информации"},{"vulcan","Вулкан"},
        {"cyrex","Сайрекс"},{"bloodsport","Кровавый спорт"},{"aquamarine_revenge","Аквамариновая месть"},
        {"desert_hydra","Пустынная гидра"},{"wildfire","Пожар"},{"the_empress","Императрица"},{"nightmare","Кошмар"},
        {"code_red","Код красный"},{"golden_coil","Золотая спираль"},{"kill_confirmed","Подтверждённое убийство"},
        {"chanticos_fire","Огонь Чантико"},{"fuel_injector","Топливный инжектор"},{"mecha_industries","Механо-пушка"},
        {"point_disarray","Точка расстройства"},{"neon_revolution","Неоновая революция"},{"roll_cage","Каркас"},
        {"fever_dream","Лихорадочный сон"},{"bullet_rain","Дождь из пуль"},{"jaguar","Ягуар"},
        {"lightning_strike","Удар молнии"},{"dark_water","Тёмная вода"}
    };
    std::string k = internal;
    for (auto &c : k) c = (char)tolower((unsigned char)c);
    for (auto &p : m) if (k.find(p.first) != std::string::npos) return p.second;
    return {};
}

static std::string ruWords(std::string s) {
    static const std::pair<const char*,const char*> w[] = {
        {"Black","Чёрный"},{"White","Белый"},{"Red","Красный"},{"Blue","Синий"},{"Green","Зелёный"},{"Yellow","Жёлтый"},
        {"Gold","Золотой"},{"Silver","Серебряный"},{"Dark","Тёмный"},{"Light","Светлый"},{"Blood","Кровавый"},
        {"Night","Ночной"},{"Forest","Лесной"},{"Urban","Городской"},{"Desert","Пустынный"},{"Ice","Ледяной"},
        {"Fire","Огненный"},{"Dragon","Дракон"},{"Tiger","Тигр"},{"Steel","Сталь"},{"Carbon","Карбон"},
        {"Neon","Неоновый"},{"Ghost","Призрак"},{"Hunter","Охотник"},{"Elite","Элитный"},{"Royal","Королевский"},
        {"Army","Армейский"},{"Digital","Цифровой"},{"Camo","Камуфляж"},{"Sand","Песочный"},{"Storm","Шторм"},
        {"Shadow","Тень"},{"Crimson","Багровый"},{"Emerald","Изумрудный"},{"Ruby","Рубиновый"},{"Sapphire","Сапфировый"},
        {"Marble","Мраморный"},{"Rust","Ржавый"}
    };
    for (auto &p : w) {
        std::string from=p.first, to=p.second;
        size_t pos=0;
        while((pos=s.find(from,pos))!=std::string::npos){ s.replace(pos,from.size(),to); pos+=to.size(); }
    }
    return s;
}

std::string prettyPaintName(std::string n) {
    if (n.empty() || n == "default") return isRu() ? "Стандарт" : "Default";
    std::string internal = n;
    for (auto &c : n) if (c == '_') c = ' ';
    static const char* prefixes[] = {"cu ","am ","so ","aq ","sp ","hy ","gs ","an ","aa ","gsch ","ht ","sop ","mk ","hm "};
    for (auto pfx : prefixes) {
        size_t len = std::strlen(pfx);
        if (n.rfind(pfx, 0) == 0 && n.size() > len + 2) { n = n.substr(len); break; }
    }
    bool cap = true;
    for (auto &c : n) { if (cap && c >= 'a' && c <= 'z') c = c - 'a' + 'A'; cap = (c == ' '); }
    if (isRu()) {
        auto ru = ruPaintName(internal);
        if (!ru.empty()) return ru;
        return ruWords(n);
    }
    return n;
}

std::string paintNameById(uint32_t id) {
    if (!id) return isRu() ? "Стандарт" : "Default";
    for (auto &p : sc().catalog.Paints) if (p.Id == id) return prettyPaintName(p.Name);
    return std::to_string(id);
}

std::string prettyItemName(const skinchanger_ipc::CatalogItem* it) {
    if (!it) return {};
    std::string n = it->Name;
    struct Map { const char* key; const char* en; const char* ru; };
    static const Map map[] = {
        {"weapon_deagle","Desert Eagle","Дигл"},
        {"weapon_elite","Dual Berettas","Две Беретты"},
        {"weapon_fiveseven","Five-SeveN","Файв-Севен"},
        {"weapon_glock","Glock-18","Глок-18"},
        {"weapon_ak47","AK-47","АК-47"},
        {"weapon_aug","AUG","АУГ"},
        {"weapon_awp","AWP","АВП"},
        {"weapon_famas","FAMAS","ФАМАС"},
        {"weapon_g3sg1","G3SG1","G3SG1"},
        {"weapon_galilar","Galil AR","Галил AR"},
        {"weapon_m249","M249","M249"},
        {"weapon_m4a1","M4A4","M4A4"},
        {"weapon_mac10","MAC-10","МАС-10"},
        {"weapon_p90","P90","P90"},
        {"weapon_mp5sd","MP5-SD","MP5-SD"},
        {"weapon_ump45","UMP-45","UMP-45"},
        {"weapon_xm1014","XM1014","XM1014"},
        {"weapon_bizon","PP-Bizon","ПП-Бизон"},
        {"weapon_mag7","MAG-7","МАГ-7"},
        {"weapon_negev","Negev","Негев"},
        {"weapon_sawedoff","Sawed-Off","Сawed-Off"},
        {"weapon_tec9","Tec-9","Тек-9"},
        {"weapon_hkp2000","P2000","P2000"},
        {"weapon_mp7","MP7","MP7"},
        {"weapon_mp9","MP9","MP9"},
        {"weapon_nova","Nova","Нова"},
        {"weapon_p250","P250","P250"},
        {"weapon_scar20","SCAR-20","SCAR-20"},
        {"weapon_sg556","SG 553","SG 553"},
        {"weapon_ssg08","SSG 08","SSG 08"},
        {"weapon_m4a1_silencer","M4A1-S","M4A1-S"},
        {"weapon_usp_silencer","USP-S","USP-S"},
        {"weapon_cz75a","CZ75-Auto","CZ75-Авто"},
        {"weapon_revolver","R8 Revolver","Револьвер R8"},
        {"weapon_knife","Knife","Нож"},
        {"weapon_knife_t","Knife","Нож"},
    };
    for (auto &m : map) if (n == m.key) return isRu() ? m.ru : m.en;
    std::string raw = n;
    if (raw.rfind("weapon_",0)==0) raw = raw.substr(7);
    for (auto &c : raw) if (c == '_') c = ' ';
    bool cap = true;
    for (auto &c : raw) { if (cap && c >= 'a' && c <= 'z') c = c - 'a' + 'A'; cap = (c == ' ' || c == '-'); }
    if (n.rfind("weapon_knife",0)==0 || n.rfind("weapon_bayonet",0)==0) return raw + (isRu() ? " (Нож)" : " (Knife)");
    return raw;
}

ImU32 rarityColor(uint8_t r) {
    switch (r) {
        case 1: return IM_COL32(176,195,217,255); // consumer
        case 2: return IM_COL32(94,152,217,255);  // industrial
        case 3: return IM_COL32(75,105,255,255);  // mil-spec
        case 4: return IM_COL32(136,71,255,255);  // restricted
        case 5: return IM_COL32(211,44,230,255);  // classified
        case 6: return IM_COL32(235,75,75,255);   // covert
        case 7: return IM_COL32(228,174,57,255);  // contraband/gold
        default: return IM_COL32(180,180,180,255);
    }
}

struct PaintOption { int index; uint8_t rarity; };
std::vector<PaintOption> paintsForItem(const skinchanger_ipc::CatalogItem* it) {
    std::vector<PaintOption> out;
    if (!it) return out;
    for (int i=0;i<(int)sc().catalog.Paints.size();++i) {
        auto &p = sc().catalog.Paints[i];
        for (auto &pi : p.Items) if (pi.Definition == it->Definition) { out.push_back({i, pi.Rarity}); break; }
    }
    std::sort(out.begin(), out.end(), [](auto&a,auto&b){ if (a.rarity!=b.rarity) return a.rarity>b.rarity; return sc().catalog.Paints[a.index].Name < sc().catalog.Paints[b.index].Name; });
    return out;
}

static nlohmann::json stateToJson(const skinchanger_ipc::State &st) {
    nlohmann::json arr = nlohmann::json::array();
    for (auto &r : st.Items) {
        nlohmann::json j;
        j["Team"]=r.Team; j["Slot"]=r.Slot; j["Definition"]=r.Definition; j["Paint"]=r.Paint; j["Seed"]=r.Seed; j["Wear"]=r.Wear;
        j["Legacy"]=r.Legacy; j["StatTrak"]=r.StatTrak; j["StatTrakCount"]=r.StatTrakCount; j["Name"]=r.Name; j["Shown"]=r.Shown;
        j["SouvenirEvent"]=r.SouvenirEvent; j["Music"]=r.Music;
        j["Stickers"]=nlohmann::json::array(); for(auto &s:r.Stickers){ nlohmann::json x; x["Slot"]=s.Slot; x["Kit"]=s.Kit; x["Wear"]=s.Wear; x["Scale"]=s.Scale; x["Rotation"]=s.Rotation; x["OffsetX"]=s.OffsetX; x["OffsetY"]=s.OffsetY; x["Schema"]=s.Schema; j["Stickers"].push_back(x); }
        j["Patches"]=nlohmann::json::array(); for(auto &p:r.Patches){ nlohmann::json x; x["Slot"]=p.Slot; x["Kit"]=p.Kit; j["Patches"].push_back(x); }
        if (r.Charm) { nlohmann::json c; c["Kit"]=r.Charm->Kit; c["Pattern"]=r.Charm->Pattern; c["Highlight"]=r.Charm->Highlight; c["OffsetX"]=r.Charm->OffsetX; c["OffsetY"]=r.Charm->OffsetY; c["OffsetZ"]=r.Charm->OffsetZ; j["Charm"]=c; } else j["Charm"]=nullptr;
        arr.push_back(std::move(j));
    }
    return arr;
}

void refreshIfNeeded() {
    auto now = ImGui::GetTime();
    if (now - sc().last_refresh > 1.5) {
        sc().last_refresh = now;
        sc().catalog = skinchanger_ipc::loadCatalog();
        sc().state = skinchanger_ipc::loadState();
        sc().status = skinchanger_ipc::loadStatus();
        config::skinchanger_settings = stateToJson(sc().state);
    }
}

} // namespace

void menu_t::draw_skinchanger()
{
    refreshIfNeeded();
    auto &s = sc();
    const bool connected = skinchanger_ipc::dllConnected();
    const char* cats[] = {tr("Weapons","Оружие"), tr("Knives","Ножи"), tr("Gloves","Перчатки")};
    auto items = itemsFor(s.cat);

    ImGui::SetCursorPos({14.0f, 14.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::BeginChild("##sc_cards", {0.0f, 0.0f}, false);
    begin_cards("##sc_grid");

    card_in_column("sc_status", tr("SKINCHANGER","СКИНЧЕНДЖЕР"), 4, 0, [&] {
        begin_row(tr("Status","Статус"), 160.0f);
        ImGui::TextUnformatted(connected ? tr("Connected","Подключено") : tr("Waiting for DLL...","Ожидание DLL..."));
        end_row();
        begin_row(tr("Last action","Действие"), 160.0f);
        ImGui::TextUnformatted(s.status.empty() ? "-" : s.status.c_str());
        end_row();
        begin_row(tr("Category","Категория"), 180.0f);
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::Combo("##sc_cat", &s.cat, cats, 3)) { s.item = -1; s.paint = -1; }
        end_row();
        if (ImGui::Button(tr("Refresh","Обновить"), {140, 28})) {
            s.catalog = skinchanger_ipc::loadCatalog(); s.state = skinchanger_ipc::loadState(); s.status = skinchanger_ipc::loadStatus();
            skinchanger_ipc::requestRefresh(); s.item = -1; s.paint = -1; s.sticker = -1; s.patch = -1; s.charm = -1; items = itemsFor(s.cat);
        }
    });

    card_in_column("sc_applied", tr("APPLIED","ПРИМЕНЕНО"), 6, 0, [&] {
        if (s.state.Items.empty()) { ImGui::TextDisabled("%s", tr("No applied skins","Нет применённых скинов")); return; }
        ImGui::BeginChild("##sc_applied", {0, 200}, false);
        for (size_t i = 0; i < s.state.Items.size(); ++i) {
            auto &r = s.state.Items[i];
            auto *ci = findCatalogItem(r.Definition);
            std::string name = ci ? prettyItemName(ci) : std::to_string(r.Definition);
            std::string line = name + " | " + paintNameById(r.Paint);
            ImGui::PushID((int)i);
            ImGui::TextWrapped("%s", line.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(tr("Unequip","Снять"))) {
                skinchanger_ipc::ItemRecord ur{}; ur.Team=r.Team; ur.Slot=r.Slot; ur.Definition=r.Definition;
                skinchanger_ipc::sendUnequip(ur);
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
    });

    card_in_column("sc_items", tr("ITEMS","ПРЕДМЕТЫ"), 8, 1, [&] {
        ImGui::BeginChild("##sc_items", {0, 260}, false);
        for (int i = 0; i < (int)items.size(); ++i) {
            auto *it = items[i];
            const std::string label = prettyItemName(it);
            if (ImGui::Selectable(label.c_str(), s.item == i)) { s.item = i; s.paint = -1; }
        }
        ImGui::EndChild();
    });

    card_in_column("sc_config", tr("CONFIG","НАСТРОЙКА"), 10, 1, [&] {
        if (s.item < 0 || s.item >= (int)items.size()) { ImGui::TextDisabled("%s", tr("Select an item","Выберите предмет")); return; }
        auto *it = items[s.item];
        ImGui::TextUnformatted(prettyItemName(it).c_str());
        ImGui::TextDisabled("Def: %u  Slot: %u", it->Definition, it->Slot);

        if (it->Paintable) {
            std::string paintName = tr("None","Нет");
            uint8_t selRarity = 0;
            if (s.paint >= 0 && s.paint < (int)s.catalog.Paints.size()) {
                paintName = prettyPaintName(s.catalog.Paints[s.paint].Name);
                for (auto &pi : s.catalog.Paints[s.paint].Items) if (pi.Definition == it->Definition) selRarity = pi.Rarity;
            }
            auto opts = paintsForItem(it);
            begin_row(tr("Paint","Скин"), 220.0f); ImGui::SetNextItemWidth(220.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, rarityColor(selRarity));
            if (ImGui::BeginCombo("##sc_paint", paintName.c_str())) {
                ImGui::PopStyleColor();
                if (ImGui::Selectable(tr("None","Нет"), s.paint < 0)) s.paint = -1;
                for (auto &o : opts) {
                    auto pn = prettyPaintName(s.catalog.Paints[o.index].Name);
                    ImGui::PushStyleColor(ImGuiCol_Text, rarityColor(o.rarity));
                    if (ImGui::Selectable(pn.c_str(), s.paint==o.index)) s.paint=o.index;
                    ImGui::PopStyleColor();
                }
                ImGui::EndCombo();
            } else {
                ImGui::PopStyleColor();
            }
            end_row();
            begin_row(tr("Wear","Износ"), 220.0f); ImGui::SetNextItemWidth(220.0f); ImGui::SliderFloat("##sc_wear",&s.wear,0.f,1.f,"%.3f"); end_row();
            begin_row(tr("Seed","Паттерн"), 220.0f); ImGui::SetNextItemWidth(220.0f); ImGui::InputInt("##sc_seed",&s.seed); end_row();
        }
        if (it->StatTrak) {
            begin_row("StatTrak", 46.0f); ImGui::Checkbox("##sc_st",&s.stattrak); end_row();
            if (s.stattrak) { begin_row(tr("Count","Счётчик"), 220.0f); ImGui::SetNextItemWidth(220.0f); ImGui::InputInt("##sc_stc",&s.stattrak_count); end_row(); }
        }
        begin_row(tr("Name Tag","Именная метка"), 220.0f); ImGui::SetNextItemWidth(220.0f); ImGui::InputText("##sc_name", s.name, sizeof(s.name)); end_row();

        if (it->StickerSlots > 0) {
            std::string n = (s.sticker>=0 && s.sticker<(int)s.catalog.Stickers.size()) ? prettyPaintName(s.catalog.Stickers[s.sticker].Name) : tr("None","Нет");
            begin_row(tr("Sticker","Наклейка"), 220.0f); ImGui::SetNextItemWidth(220.0f);
            if (ImGui::BeginCombo("##sc_stk", n.c_str())) { if(ImGui::Selectable(tr("None","Нет"), s.sticker<0)) s.sticker=-1; for(int i=0;i<(int)s.catalog.Stickers.size();++i) { auto sn=prettyPaintName(s.catalog.Stickers[i].Name); if(ImGui::Selectable(sn.c_str(), s.sticker==i)) s.sticker=i; } ImGui::EndCombo(); } end_row();
        }
        if (it->PatchSlots > 0) {
            std::string n = (s.patch>=0 && s.patch<(int)s.catalog.Patches.size()) ? prettyPaintName(s.catalog.Patches[s.patch].Name) : tr("None","Нет");
            begin_row(tr("Patch","Патч"), 220.0f); ImGui::SetNextItemWidth(220.0f);
            if (ImGui::BeginCombo("##sc_patch", n.c_str())) { if(ImGui::Selectable(tr("None","Нет"), s.patch<0)) s.patch=-1; for(int i=0;i<(int)s.catalog.Patches.size();++i) { auto pn=prettyPaintName(s.catalog.Patches[i].Name); if(ImGui::Selectable(pn.c_str(), s.patch==i)) s.patch=i; } ImGui::EndCombo(); } end_row();
        }
        if (it->CharmSlots > 0) {
            std::string n = (s.charm>=0 && s.charm<(int)s.catalog.Keychains.size()) ? prettyPaintName(s.catalog.Keychains[s.charm].Name) : tr("None","Нет");
            begin_row(tr("Charm","Брелок"), 220.0f); ImGui::SetNextItemWidth(220.0f);
            if (ImGui::BeginCombo("##sc_charm", n.c_str())) { if(ImGui::Selectable(tr("None","Нет"), s.charm<0)) s.charm=-1; for(int i=0;i<(int)s.catalog.Keychains.size();++i) { auto cn=prettyPaintName(s.catalog.Keychains[i].Name); if(ImGui::Selectable(cn.c_str(), s.charm==i)) s.charm=i; } ImGui::EndCombo(); } end_row();
        }

        if (ImGui::Button(tr("Equip","Применить"), {140,30})) {
            skinchanger_ipc::ItemRecord r{};
            r.Team = pickTeam(it); r.Slot = it->Slot; r.Definition = it->Definition;
            r.Paint = (s.paint>=0 && s.paint<(int)s.catalog.Paints.size()) ? s.catalog.Paints[s.paint].Id : 0;
            r.Wear = s.wear;
            if (r.Paint && s.paint>=0 && s.paint<(int)s.catalog.Paints.size()) {
                auto &p = s.catalog.Paints[s.paint];
                r.Wear = std::clamp(r.Wear, p.WearMin, p.WearMax);
            }
            r.Seed = (uint32_t)std::max(0,s.seed); r.StatTrak=s.stattrak; r.StatTrakCount=(uint32_t)std::max(0,s.stattrak_count); r.Name=s.name;
            if (s.sticker>=0 && s.sticker<(int)s.catalog.Stickers.size() && it->StickerSlots>0) r.Stickers.push_back({0, s.catalog.Stickers[s.sticker].Id,0.f,1.f,0.f,0.f,0.f,-1});
            if (s.patch>=0 && s.patch<(int)s.catalog.Patches.size() && it->PatchSlots>0) r.Patches.push_back({0, s.catalog.Patches[s.patch].Id});
            if (s.charm>=0 && s.charm<(int)s.catalog.Keychains.size() && it->CharmSlots>0) r.Charm = skinchanger_ipc::Charm{s.catalog.Keychains[s.charm].Id,0,0,0,0,0};
            skinchanger_ipc::sendEquip(r);
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Unequip","Снять"), {140,30})) {
            skinchanger_ipc::ItemRecord r{}; r.Team=pickTeam(it); r.Slot=it->Slot; r.Definition=it->Definition;
            skinchanger_ipc::sendUnequip(r);
        }
    });

    end_cards();
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

} // namespace ui