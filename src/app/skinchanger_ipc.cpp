#include "app/skinchanger_ipc.h"
#include <nlohmann/json.hpp>
#include <windows.h>
#include <fstream>
#include <chrono>

namespace skinchanger_ipc {
using json = nlohmann::json;

std::filesystem::path baseDir() {
    const wchar_t *app = _wgetenv(L"APPDATA");
    std::filesystem::path p = app ? app : L".";
    p /= L"Zoowie";
    std::filesystem::create_directories(p);
    return p;
}

static json readJsonFile(const std::filesystem::path &p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    try { return json::parse(f, nullptr, false); } catch (...) { return {}; }
}

static void writeJsonFile(const std::filesystem::path &p, const json &j) {
    auto tmp = p; tmp += L".tmp";
    std::ofstream f(tmp, std::ios::binary);
    f << j.dump();
    f.close();
    std::error_code ec;
    std::filesystem::rename(tmp, p, ec);
    if (ec) { std::filesystem::remove(p, ec); std::filesystem::rename(tmp, p, ec); }
}

Catalog loadCatalog() {
    Catalog c;
    auto j = readJsonFile(baseDir() / L"skinchanger_catalog.json");
    if (j.is_discarded() || !j.is_object()) return c;
    if (j.contains("Ids")) {
        c.AnyTeam = j["Ids"].value("AnyTeam",(uint8_t)0);
        c.Terrorists = j["Ids"].value("Terrorists",(uint8_t)2);
        c.CounterTerrorists = j["Ids"].value("CounterTerrorists",(uint8_t)3);
    }
    if (j.contains("Items")) for (auto &it : j["Items"]) {
        CatalogItem ci; ci.Name = it.value("Name",""); ci.Definition = it.value("Definition", (uint16_t)0); ci.Slot = it.value("Slot",(uint16_t)0);
        ci.Teams = it.value("Teams",(uint8_t)0); ci.Paintable = it.value("Paintable",false); ci.NameTag = it.value("NameTag",false);
        ci.StatTrak = it.value("StatTrak",false); ci.Legacy = it.value("Legacy",false); ci.StickerSlots = it.value("StickerSlots",(uint8_t)0);
        ci.CharmSlots = it.value("CharmSlots",(uint8_t)0); ci.PatchSlots = it.value("PatchSlots",(uint8_t)0);
        // Kind may be string or number depending on glaze
        if (it.contains("Kind")) {
            if (it["Kind"].is_number()) ci.Kind = it["Kind"].get<int>();
            else { auto s = it["Kind"].get<std::string>(); if(s=="Weapon")ci.Kind=0; else if(s=="Knife")ci.Kind=1; else if(s=="Gloves")ci.Kind=2; else if(s=="Agent")ci.Kind=3; else if(s=="Music")ci.Kind=4; else if(s=="Medal")ci.Kind=5; }
        }
        c.Items.push_back(std::move(ci));
    }
    auto readKits = [&](const char *key, std::vector<Kit> &out){ if(!j.contains(key)) return; for(auto &k: j[key]) out.push_back({k.value("Id",0u), k.value("Name", std::string{})}); };
    if (j.contains("Paints")) for (auto &p : j["Paints"]) {
        Paint pt; pt.Id = p.value("Id",0u); pt.Name = p.value("Name", std::string{});
        pt.WearMin = p.value("WearMin", 0.f); pt.WearMax = p.value("WearMax", 1.f);
        if (p.contains("Items")) for (auto &pi : p["Items"]) pt.Items.push_back({pi.value("Definition",(uint16_t)0), pi.value("Rarity",(uint8_t)0)});
        c.Paints.push_back(std::move(pt));
    }
    readKits("Stickers", c.Stickers); readKits("Patches", c.Patches); readKits("Keychains", c.Keychains); readKits("MusicKits", c.MusicKits);
    return c;
}

State loadState() {
    State s;
    auto j = readJsonFile(baseDir() / L"skinchanger_state.json");
    if (j.is_discarded() || !j.is_object()) return s;
    if (!j.contains("Items")) return s;
    for (auto &it : j["Items"]) {
        ItemRecord r; r.Team=it.value("Team",(uint8_t)0); r.Slot=it.value("Slot",(uint16_t)0); r.Definition=it.value("Definition",(uint16_t)0);
        r.Paint=it.value("Paint",0u); r.Seed=it.value("Seed",0u); r.Wear=it.value("Wear",0.f); r.Legacy=it.value("Legacy",false); r.StatTrak=it.value("StatTrak",false);
        r.StatTrakCount=it.value("StatTrakCount",0u); r.Name=it.value("Name",std::string{});
        s.Items.push_back(std::move(r));
    }
    return s;
}

static json itemToJson(const ItemRecord &r) {
    json j;
    j["Team"]=r.Team; j["Slot"]=r.Slot; j["Definition"]=r.Definition; j["Paint"]=r.Paint; j["Seed"]=r.Seed; j["Wear"]=r.Wear;
    j["Legacy"]=r.Legacy; j["StatTrak"]=r.StatTrak; j["StatTrakCount"]=r.StatTrakCount; j["Name"]=r.Name; j["Shown"]=r.Shown;
    j["SouvenirEvent"]=r.SouvenirEvent; j["Music"]=r.Music;
    j["Stickers"]=json::array(); for(auto &s:r.Stickers){ json x; x["Slot"]=s.Slot; x["Kit"]=s.Kit; x["Wear"]=s.Wear; x["Scale"]=s.Scale; x["Rotation"]=s.Rotation; x["OffsetX"]=s.OffsetX; x["OffsetY"]=s.OffsetY; x["Schema"]=s.Schema; j["Stickers"].push_back(x); }
    j["Patches"]=json::array(); for(auto &p:r.Patches){ json x; x["Slot"]=p.Slot; x["Kit"]=p.Kit; j["Patches"].push_back(x); }
    if (r.Charm) { json c; c["Kit"]=r.Charm->Kit; c["Pattern"]=r.Charm->Pattern; c["Highlight"]=r.Charm->Highlight; c["OffsetX"]=r.Charm->OffsetX; c["OffsetY"]=r.Charm->OffsetY; c["OffsetZ"]=r.Charm->OffsetZ; j["Charm"]=c; } else j["Charm"]=nullptr;
    return j;
}

void sendEquip(const ItemRecord &r) { writeJsonFile(baseDir() / L"skinchanger_cmd.json", json{{"action","equip"},{"item",itemToJson(r)}}); }
void sendUnequip(const ItemRecord &r) { writeJsonFile(baseDir() / L"skinchanger_cmd.json", json{{"action","unequip"},{"item",itemToJson(r)}}); }
void sendApply(const std::vector<ItemRecord> &items) {
    json arr = json::array();
    for (auto &r : items) arr.push_back(itemToJson(r));
    writeJsonFile(baseDir() / L"skinchanger_cmd.json", json{{"action","apply"},{"items",arr}});
}
void requestRefresh() { writeJsonFile(baseDir() / L"skinchanger_cmd.json", json{{"action","refresh"}}); }
void sendUnload() { writeJsonFile(baseDir() / L"skinchanger_cmd.json", json{{"action","unload"}}); }

bool dllConnected() {
    std::error_code ec;
    return std::filesystem::exists(baseDir() / L"skinchanger_catalog.json", ec);
}

std::string loadStatus() {
    std::ifstream f(baseDir() / L"skinchanger_status.txt", std::ios::binary);
    if (!f) return {};
    std::string s((std::istreambuf_iterator<char>(f)), {});
    return s;
}

} // namespace skinchanger_ipc
