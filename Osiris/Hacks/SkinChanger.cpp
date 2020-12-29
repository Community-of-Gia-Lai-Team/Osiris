#include <algorithm>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <unordered_set>

#include "../Interfaces.h"

#include "SkinChanger.h"
#include "../Config.h"

#include "../SDK/Client.h"
#include "../SDK/ClientClass.h"
#include "../SDK/ConVar.h"
#include "../SDK/Cvar.h"
#include "../SDK/Engine.h"
#include "../SDK/Entity.h"
#include "../SDK/EntityList.h"
#include "../SDK/FrameStage.h"
#include "../SDK/GameEvent.h"
#include "../SDK/ItemSchema.h"
#include "../SDK/Localize.h"
#include "../SDK/ModelInfo.h"
#include "../SDK/Platform.h"
#include "../SDK/WeaponId.h"

#include "../Helpers.h"

/* This file is part of nSkinz by namazso, licensed under the MIT license:
*
* MIT License
*
* Copyright (c) namazso 2018
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/

static constexpr auto is_knife(WeaponId id)
{
    return (id >= WeaponId::Bayonet && id < WeaponId::GloveStuddedBloodhound) || id == WeaponId::KnifeT || id == WeaponId::Knife;
}

item_setting* get_by_definition_index(const int definition_index)
{
    auto it = std::find_if(std::begin(config->skinChanger), std::end(config->skinChanger), [definition_index](const item_setting& e)
        {
            return e.enabled && e.itemId == definition_index;
        });

    return it == std::end(config->skinChanger) ? nullptr : &*it;
}

static std::vector<SkinChanger::PaintKit> skinKits{ { 0, "-" } };
static std::vector<SkinChanger::PaintKit> gloveKits;

static void initializeKits() noexcept
{
    static bool initalized = false;
    if (initalized)
        return;
    initalized = true;

    const auto itemSchema = memory->itemSystem()->getItemSchema();

    std::vector<std::pair<int, WeaponId>> kitsWeapons;
    kitsWeapons.reserve(2000);

    for (int i = 0; i <= itemSchema->alternateIcons.lastAlloc; ++i) {
        const auto encoded = itemSchema->alternateIcons.memory[i].key;
        kitsWeapons.emplace_back(int((encoded & 0xFFFF) >> 2), WeaponId(encoded >> 16)); // https://github.com/perilouswithadollarsign/cstrike15_src/blob/f82112a2388b841d72cb62ca48ab1846dfcc11c8/game/shared/econ/econ_item_schema.cpp#L325-L329
    }

    std::sort(kitsWeapons.begin(), kitsWeapons.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    skinKits.reserve(itemSchema->paintKits.lastAlloc);
    gloveKits.reserve(itemSchema->paintKits.lastAlloc);
    for (int i = 0; i <= itemSchema->paintKits.lastAlloc; i++) {
        const auto paintKit = itemSchema->paintKits.memory[i].value;

        if (paintKit->id == 0 || paintKit->id == 9001) // ignore workshop_default
            continue;

        if (paintKit->id >= 10000) {
            std::wstring name;

            if (const auto it = std::lower_bound(kitsWeapons.begin(), kitsWeapons.end(), paintKit->id, [](const auto& p, auto id) { return p.first < id; }); it != kitsWeapons.end() && it->first == paintKit->id) {
                if (const auto itemDef = itemSchema->getItemDefinitionInterface(it->second)) {
                    name = interfaces->localize->findSafe(itemDef->getItemBaseName());
                    name += L" | ";
                }
            }

            name += interfaces->localize->findSafe(paintKit->itemName.data() + 1);
            gloveKits.emplace_back(paintKit->id, std::move(name), paintKit->rarity);
        } else {
            std::unordered_set<WeaponId> weapons;

            for (auto it = std::lower_bound(kitsWeapons.begin(), kitsWeapons.end(), paintKit->id, [](const auto& p, auto id) { return p.first < id; }); it != kitsWeapons.end() && it->first == paintKit->id; ++it) {
                weapons.insert(it->second);
            }

            for (auto weapon : weapons) {
                const auto itemDef = itemSchema->getItemDefinitionInterface(weapon);
                if (!itemDef)
                    continue;

                std::wstring name = interfaces->localize->findSafe(itemDef->getItemBaseName());
                name += L" | ";
                name += interfaces->localize->findSafe(paintKit->itemName.data() + 1);
                skinKits.emplace_back(paintKit->id, std::move(name), std::clamp(itemDef->getRarity() + paintKit->rarity - 1, 0, (paintKit->rarity == 7) ? 7 : 6));
            }

            if (weapons.empty()) {
                assert(false);
                std::wstring name = interfaces->localize->findSafe(paintKit->itemName.data() + 1);
                skinKits.emplace_back(paintKit->id, std::move(name));
            }
        }
    }

    std::sort(skinKits.begin() + 1, skinKits.end());
    skinKits.shrink_to_fit();
    std::sort(gloveKits.begin(), gloveKits.end());
    gloveKits.shrink_to_fit();
}

static std::unordered_map<std::string, const char*> iconOverrides;

enum class StickerAttribute {
    Index,
    Wear,
    Scale,
    Rotation
};

static auto s_econ_item_interface_wrapper_offset = std::uint16_t(0);

void apply_sticker_changer(Entity* item) noexcept
{
    /*
    if (constexpr auto hash{ fnv::hash("CBaseAttributableItem->m_Item") }; !s_econ_item_interface_wrapper_offset)
        s_econ_item_interface_wrapper_offset = netvars->operator[](hash) + 0xC;

    static vmt_multi_hook hook;

    const auto econ_item_interface_wrapper = std::uintptr_t(item) + s_econ_item_interface_wrapper_offset;

    if (hook.initialize_and_hook_instance(reinterpret_cast<void*>(econ_item_interface_wrapper))) {
        hook.apply_hook<GetStickerAttributeBySlotIndexFloat>(4);
        hook.apply_hook<GetStickerAttributeBySlotIndexInt>(5);
    }
    */

    if (auto config = get_by_definition_index(item->itemDefinitionIndex())) {
        constexpr auto m_Item = fnv::hash("CBaseAttributableItem->m_Item");
        const auto attributeList = std::uintptr_t(item) + netvars->operator[](m_Item) + /* m_AttributeList = */ WIN32_LINUX(0x244, 0x2F8);

        for (std::size_t i = 0; i < config->stickers.size(); ++i) {
            const auto& sticker = config->stickers[i];
            const auto attributeString = "sticker slot " + std::to_string(i) + ' ';

            memory->setOrAddAttributeValueByName(attributeList, (attributeString + "id").c_str(), sticker.kit);
            memory->setOrAddAttributeValueByName(attributeList, (attributeString + "wear").c_str(), sticker.wear);
            memory->setOrAddAttributeValueByName(attributeList, (attributeString + "scale").c_str(), sticker.scale);
            memory->setOrAddAttributeValueByName(attributeList, (attributeString + "rotation").c_str(), sticker.rotation);
        }
    }
}

static void apply_config_on_attributable_item(Entity* item, const item_setting& config,
    const unsigned xuid_low) noexcept
{
    // Force fallback values to be used.
    item->itemIDHigh() = -1;

    // Set the owner of the weapon to our lower XUID. (fixes StatTrak)
    item->accountID() = xuid_low;
    item->entityQuality() = config.quality;

    if (config.stat_trak > -1) {
        item->fallbackStatTrak() = config.stat_trak;
        item->entityQuality() = 9;
    }

    if (is_knife(item->itemDefinitionIndex2()))
        item->entityQuality() = 3; // make a star appear on knife

#ifdef _WIN32
    if (config.custom_name[0])
        strcpy_s(item->customName(), config.custom_name);
#endif

    if (config.paintKit)
        item->fallbackPaintKit() = config.paintKit;

    if (config.seed)
        item->fallbackSeed() = config.seed;

    item->fallbackWear() = config.wear;

    auto& definition_index = item->itemDefinitionIndex();

    if (config.definition_override_index // We need to override defindex
        && config.definition_override_index != definition_index) // It is not yet overridden
    {
        if (config.itemId == GLOVE_T_SIDE) {
            definition_index = config.definition_override_index;
            const auto def = memory->itemSystem()->getItemSchema()->getItemDefinitionInterface(WeaponId{ definition_index });
            if (def) {
                item->setModelIndex(interfaces->modelInfo->getModelIndex(def->getWorldDisplayModel()));
                item->preDataUpdate(0);
            }
        } else if (const auto replacement_item = game_data::get_weapon_info(config.definition_override_index)) {
            const auto old_definition_index = definition_index;

            definition_index = config.definition_override_index;

            // Set the weapon model index -- required for paint kits to work on replacement items after the 29/11/2016 update.
            //item->GetModelIndex() = g_model_info->GetModelIndex(k_weapon_info.at(config->definition_override_index).model);
            item->setModelIndex(interfaces->modelInfo->getModelIndex(replacement_item->model));
            item->preDataUpdate(0);

            // We didn't override 0, but some actual weapon, that we have data for
            if (old_definition_index)
                if (const auto original_item = game_data::get_weapon_info(old_definition_index); original_item && original_item->icon && replacement_item->icon)
                    iconOverrides[original_item->icon] = replacement_item->icon;
        }
    }
    apply_sticker_changer(item);
}

static Entity* make_glove(int entry, int serial) noexcept
{
    static std::add_pointer_t<Entity* __CDECL(int, int)> createWearable = nullptr;

    if (!createWearable) {
        createWearable = []() -> decltype(createWearable) {
            for (auto clientClass = interfaces->client->getAllClasses(); clientClass; clientClass = clientClass->next)
                if (clientClass->classId == ClassId::EconWearable)
                    return clientClass->createFunction;
            return nullptr;
        }();
    }

    if (!createWearable)
        return nullptr;

    createWearable(entry, serial);
    return interfaces->entityList->getEntity(entry);
}

static void post_data_update_start(int localHandle) noexcept
{
    const auto local = interfaces->entityList->getEntityFromHandle(localHandle);
    if (!local)
        return;

    const auto local_index = local->index();

    if (!local->isAlive())
        return;

    PlayerInfo player_info;
    if (!interfaces->engine->getPlayerInfo(local_index, player_info))
        return;

    // Handle glove config
    {
        const auto wearables = local->wearables();

        const auto glove_config = get_by_definition_index(GLOVE_T_SIDE);

        static int glove_handle;

        auto glove = interfaces->entityList->getEntityFromHandle(wearables[0]);

        if (!glove) // There is no glove
        {
            // Try to get our last created glove
            const auto our_glove = interfaces->entityList->getEntityFromHandle(glove_handle);

            if (our_glove) // Our glove still exists
            {
                wearables[0] = glove_handle;
                glove = our_glove;
            }
        }

        if (glove_config && glove_config->definition_override_index)
        {
            // We don't have a glove, but we should
            if (!glove)
            {
                auto entry = interfaces->entityList->getHighestEntityIndex() + 1;
#define HIJACK_ENTITY 1
#if HIJACK_ENTITY == 1
                for (int i = 65; i <= interfaces->entityList->getHighestEntityIndex(); i++) {
                    auto entity = interfaces->entityList->getEntity(i);

                    if (entity && entity->getClientClass()->classId == ClassId{ 70 }) {
                        entry = i;
                        break;
                    }
                }
#endif
                const auto serial = rand() % 0x1000;

                glove = make_glove(entry, serial);
                if (glove) {
                    glove->initialized() = true;

                    wearables[0] = entry | serial << 16;

                    // Let's store it in case we somehow lose it.
                    glove_handle = wearables[0];
                }
            }

            if (glove) {
                memory->equipWearable(glove, local);
                local->body() = 1;

                apply_config_on_attributable_item(glove, *glove_config, player_info.xuidLow);
            }
        }
    }

    // Handle weapon configs
    {
        auto& weapons = local->weapons();

        for (auto weapon_handle : weapons) {
            if (weapon_handle == -1)
                break;

            auto weapon = interfaces->entityList->getEntityFromHandle(weapon_handle);

            if (!weapon)
                continue;

            auto& definition_index = weapon->itemDefinitionIndex();

            // All knives are terrorist knives.
            if (const auto active_conf = get_by_definition_index(is_knife(weapon->itemDefinitionIndex2()) ? WEAPON_KNIFE : definition_index))
                apply_config_on_attributable_item(weapon, *active_conf, player_info.xuidLow);
        }
    }

    const auto view_model = interfaces->entityList->getEntityFromHandle(local->viewModel());

    if (!view_model)
        return;

    const auto view_model_weapon = interfaces->entityList->getEntityFromHandle(view_model->weapon());

    if (!view_model_weapon)
        return;

    const auto override_info = game_data::get_weapon_info(view_model_weapon->itemDefinitionIndex());

    if (!override_info)
        return;

    const auto override_model_index = interfaces->modelInfo->getModelIndex(override_info->model);
    view_model->modelIndex() = override_model_index;

    const auto world_model = interfaces->entityList->getEntityFromHandle(view_model_weapon->weaponWorldModel());

    if (!world_model)
        return;

    world_model->modelIndex() = override_model_index + 1;
}

static bool hudUpdateRequired{ false };

static void updateHud() noexcept
{
    if (auto hud_weapons = memory->findHudElement(memory->hud, "CCSGO_HudWeaponSelection") - WIN32_LINUX(0x28, 62)) {
        for (int i = 0; i < *(hud_weapons + WIN32_LINUX(32, 52)); i++)
            i = memory->clearHudWeapon(hud_weapons, i);
    }
    hudUpdateRequired = false;
}

void SkinChanger::run(FrameStage stage) noexcept
{
    static int localPlayerHandle = -1;

    if (localPlayer)
        localPlayerHandle = localPlayer->handle();

    if (stage == FrameStage::NET_UPDATE_POSTDATAUPDATE_START) {
        post_data_update_start(localPlayerHandle);
        if (hudUpdateRequired && localPlayer && !localPlayer->isDormant())
            updateHud();
    }
}

void SkinChanger::scheduleHudUpdate() noexcept
{
    interfaces->cvar->findVar("cl_fullupdate")->changeCallback();
    hudUpdateRequired = true;
}

void SkinChanger::overrideHudIcon(GameEvent& event) noexcept
{
    if (!localPlayer)
        return;

    if (event.getInt("attacker") != localPlayer->getUserId())
        return;

    if (const auto weapon = std::string_view{ event.getString("weapon") }; weapon == "knife" || weapon == "knife_t")
        return;

    if (const auto active_conf = get_by_definition_index(WEAPON_KNIFE)) {
        if (const auto def = memory->itemSystem()->getItemSchema()->getItemDefinitionInterface(WeaponId(active_conf->definition_override_index))) {
            if (const auto defName = def->getDefinitionName(); defName && std::string_view{ defName }.starts_with("weapon_"))
                event.setString("weapon", defName + 7);
        }
    }
}

void SkinChanger::updateStatTrak(GameEvent& event) noexcept
{
    if (!localPlayer)
        return;

    if (const auto localUserId = localPlayer->getUserId(); event.getInt("attacker") != localUserId || event.getInt("userid") == localUserId)
        return;

    const auto weapon = localPlayer->getActiveWeapon();
    if (!weapon)
        return;

    if (const auto conf = get_by_definition_index(is_knife(weapon->itemDefinitionIndex2()) ? WEAPON_KNIFE : weapon->itemDefinitionIndex()); conf && conf->stat_trak > -1) {
        weapon->fallbackStatTrak() = ++conf->stat_trak;
        weapon->postDataUpdate(0);
    }
}

const std::vector<SkinChanger::PaintKit>& SkinChanger::getSkinKits() noexcept
{
    initializeKits();
    return skinKits;
}

const std::vector<SkinChanger::PaintKit>& SkinChanger::getGloveKits() noexcept
{
    initializeKits();
    return gloveKits;
}

const std::vector<SkinChanger::PaintKit>& SkinChanger::getStickerKits() noexcept
{
    static std::vector<SkinChanger::PaintKit> stickerKits;
    if (stickerKits.empty()) {
        stickerKits.emplace_back(0, "None");

        const auto itemSchema = memory->itemSystem()->getItemSchema();
        stickerKits.reserve(itemSchema->stickerKits.lastAlloc);
        for (int i = 0; i <= itemSchema->stickerKits.lastAlloc; i++) {
            const auto stickerKit = itemSchema->stickerKits.memory[i].value;
            if (std::string_view name{ stickerKit->name.data() }; name.starts_with("spray") || name.starts_with("patch"))
                continue;
            std::wstring name = interfaces->localize->findSafe(stickerKit->id != 242 ? stickerKit->itemName.data() + 1 : "StickerKit_dhw2014_teamdignitas_gold");
            stickerKits.emplace_back(stickerKit->id, std::move(name), stickerKit->rarity);
        }

        std::sort(stickerKits.begin() + 1, stickerKits.end());
        stickerKits.shrink_to_fit();
    }
    return stickerKits;
}

const std::vector<SkinChanger::Quality>& SkinChanger::getQualities() noexcept
{
    static std::vector<Quality> qualities;
    if (qualities.empty()) {
        const auto itemSchema = memory->itemSystem()->getItemSchema();
        for (int i = 0; i <= itemSchema->qualities.lastAlloc; ++i) {
            const auto quality = itemSchema->qualities.memory[i].value;
            if (const auto localizedName = interfaces->localize->findAsUTF8(quality.name); localizedName != quality.name)
                qualities.emplace_back(quality.value, localizedName);
        }

        if (qualities.empty()) // fallback
            qualities.emplace_back(0, "Default");
    }

    return qualities;
}

const std::vector<SkinChanger::Item>& SkinChanger::getGloveTypes() noexcept
{
    static std::vector<SkinChanger::Item> gloveTypes;
    if (gloveTypes.empty()) {
        gloveTypes.emplace_back(WeaponId{}, "Default");

        const auto itemSchema = memory->itemSystem()->getItemSchema();
        for (int i = 0; i <= itemSchema->itemsSorted.lastAlloc; i++) {
            const auto item = itemSchema->itemsSorted.memory[i].value;
            if (std::strcmp(item->getItemTypeName(), "#Type_Hands") == 0 && item->isPaintable())
                gloveTypes.emplace_back(item->getWeaponId(), interfaces->localize->findAsUTF8(item->getItemBaseName()));
        }
    }

    return gloveTypes;
}

const std::vector<SkinChanger::Item>& SkinChanger::getKnifeTypes() noexcept
{
    static std::vector<SkinChanger::Item> knifeTypes;
    if (knifeTypes.empty()) {
        knifeTypes.emplace_back(WeaponId{}, "Default");

        const auto itemSchema = memory->itemSystem()->getItemSchema();
        for (int i = 0; i <= itemSchema->itemsSorted.lastAlloc; i++) {
            const auto item = itemSchema->itemsSorted.memory[i].value;
            if (std::strcmp(item->getItemTypeName(), "#CSGO_Type_Knife") == 0 && item->getRarity() == 6)
                knifeTypes.emplace_back(item->getWeaponId(), interfaces->localize->findAsUTF8(item->getItemBaseName()));
        }
    }

    return knifeTypes;
}

SkinChanger::PaintKit::PaintKit(int id, const std::string& name, int rarity) noexcept : id{ id }, name{ name }, rarity{ rarity }
{
    nameUpperCase = Helpers::toUpper(Helpers::toWideString(name));
}

SkinChanger::PaintKit::PaintKit(int id, std::string&& name, int rarity) noexcept : id{ id }, name{ std::move(name) }, rarity{ rarity }
{
    nameUpperCase = Helpers::toUpper(Helpers::toWideString(this->name));
}

SkinChanger::PaintKit::PaintKit(int id, std::wstring&& name, int rarity) noexcept : id{ id }, nameUpperCase{ std::move(name) }, rarity{ rarity }
{
    this->name = interfaces->localize->convertUnicodeToAnsi(nameUpperCase.c_str());
    nameUpperCase = Helpers::toUpper(nameUpperCase);
}
