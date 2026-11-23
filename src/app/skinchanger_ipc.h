#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include <filesystem>

namespace skinchanger_ipc {

struct Sticker { uint8_t Slot=0; uint32_t Kit=0; float Wear=0,Scale=1,Rotation=0,OffsetX=0,OffsetY=0; int32_t Schema=-1; };
struct Patch { uint8_t Slot=0; uint32_t Kit=0; };
struct Charm { uint32_t Kit=0,Pattern=0,Highlight=0; float OffsetX=0,OffsetY=0,OffsetZ=0; };
struct ItemRecord {
    uint8_t Team=0; uint16_t Slot=0, Definition=0; uint32_t Paint=0,Seed=0; float Wear=0;
    bool Legacy=false, StatTrak=false; uint32_t StatTrakCount=0; std::string Name;
    std::vector<Sticker> Stickers; std::vector<Patch> Patches; std::optional<Charm> Charm;
    uint32_t SouvenirEvent=0, Music=0; bool Shown=false;
};

struct CatalogItem { std::string Name; uint16_t Definition=0,Slot=0; uint8_t Teams=0; bool Paintable=false,NameTag=false,StatTrak=false,Legacy=false; uint8_t StickerSlots=0,CharmSlots=0,PatchSlots=0; int Kind=0; };
struct PaintItem { uint16_t Definition=0; uint8_t Rarity=0; };
struct Paint { uint32_t Id=0; std::string Name; float WearMin=0.f, WearMax=1.f; std::vector<PaintItem> Items; };
struct Kit { uint32_t Id=0; std::string Name; };
struct Catalog { std::vector<CatalogItem> Items; std::vector<Paint> Paints; std::vector<Kit> Stickers,Patches,Keychains,MusicKits; uint8_t AnyTeam=0, Terrorists=2, CounterTerrorists=3; };

struct State { std::vector<ItemRecord> Items; };

std::filesystem::path baseDir();
Catalog loadCatalog();
State loadState();
void sendEquip(const ItemRecord &r);
void sendUnequip(const ItemRecord &r);
void sendApply(const std::vector<ItemRecord> &items);
void requestRefresh();
void sendUnload();
bool dllConnected();
std::string loadStatus();

} // namespace skinchanger_ipc
