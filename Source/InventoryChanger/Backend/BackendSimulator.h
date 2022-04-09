#pragma once

#include <queue>
#include <variant>

#include "ItemIDMap.h"
#include "Loadout.h"
#include "Response.h"
#include "ToolUser.h"

#include <InventoryChanger/StaticData.h>

namespace inventory_changer::backend
{

struct UseToolRequest {
    enum class Action {
        None,
        Use,
        WearSticker,
        RemoveNameTag
    };

    std::uint64_t toolItemID = 0;
    std::uint64_t destItemID = 0;
    std::uint64_t statTrakSwapItem1 = 0;
    std::uint64_t statTrakSwapItem2 = 0;
    Action action = Action::None;
    float useTime = 0.0f;
    int stickerSlot = 0;
    std::string nameTag;
};

class BackendSimulator {
public:
    [[nodiscard]] const Loadout& getLoadout() const noexcept
    {
        return loadout;
    }

    [[nodiscard]] const std::list<inventory::Item_v2>& getInventory() const noexcept
    {
        return inventory;
    }

    void equipItemCT(Loadout::InventoryItemIndex index, Loadout::Slot slot)
    {
        loadout.equipItemCT(index, slot);
    }

    void equipItemTT(Loadout::InventoryItemIndex index, Loadout::Slot slot)
    {
        loadout.equipItemTT(index, slot);
    }

    void equipItemNoTeam(Loadout::InventoryItemIndex index, Loadout::Slot slot)
    {
        loadout.equipItemNoTeam(index, slot);
    }

    [[nodiscard]] static BackendSimulator& instance()
    {
        static BackendSimulator backendSimulator;
        return backendSimulator;
    }

    void clearInventory()
    {
        inventory.clear();
    }

    std::list<inventory::Item_v2>::const_iterator addItem(inventory::Item_v2 item)
    {
        inventory.push_back(std::move(item));
        const auto added = std::prev(inventory.end());
        responses.emplace(Response::ItemAdded{ added });
        return added;
    }

    std::list<inventory::Item_v2>::const_iterator removeItem(std::list<inventory::Item_v2>::const_iterator it)
    {
        const auto itemID = itemIDMap.remove(it);
        const auto newIterator = inventory.erase(it);
        if (itemID.has_value())
            responses.emplace(Response::ItemRemoved{ *itemID });
        return newIterator;
    }

    void updateStatTrak(std::list<inventory::Item_v2>::const_iterator it, int newStatTrak)
    {
        if (!updateStatTrak(*removeConstness(it), newStatTrak))
            return;

        if (const auto itemID = getItemID(it); itemID.has_value())
            responses.emplace(Response::StatTrakUpdated{ *itemID, newStatTrak });
    }

    void moveToFront(std::list<inventory::Item_v2>::const_iterator it)
    {
        inventory.splice(inventory.end(), inventory, it);
        if (const auto itemID = getItemID(it); itemID.has_value())
            responses.emplace(Response::ItemMovedToFront{ *itemID });
    }

    void assignItemID(std::list<inventory::Item_v2>::const_iterator it, std::uint64_t itemID)
    {
        itemIDMap.add(itemID, it);
    }

    void updateItemID(std::uint64_t oldItemID, std::uint64_t newItemID)
    {
        itemIDMap.update(oldItemID, newItemID);
    }

    [[nodiscard]] std::optional<std::list<inventory::Item_v2>::const_iterator> itemFromID(std::uint64_t itemID) const
    {
        return itemIDMap.get(itemID);
    }

    [[nodiscard]] std::optional<std::uint64_t> getItemID(std::list<inventory::Item_v2>::const_iterator it) const
    {
        return itemIDMap.getItemID(it);
    }

    void useTool(const UseToolRequest& request)
    {
        if (const auto response = processUseToolRequest(request); response.has_value())
            responses.push(*response);
    }

    template <typename Visitor>
    void run(Visitor visitor)
    {
        while (!responses.empty()) {
            std::visit(visitor, responses.front().data);
            responses.pop();
        }
    }

private:
    std::optional<Response> processUseToolRequest(const UseToolRequest& request)
    {
        const auto destItem = itemIDMap.get(request.destItemID);
        const auto tool = itemIDMap.get(request.toolItemID);

        if (request.action == UseToolRequest::Action::Use) {
            if (destItem.has_value() && (*destItem)->gameItem().isCase())
                return ToolUser{ *this, StaticData::lookup() }.openContainer(*destItem, tool);

            if (!tool.has_value())
                return {};

            if ((*tool)->gameItem().isSticker()) {
                if (!destItem.has_value())
                    return {};

                return ToolUser{ *this, StaticData::lookup() }.applySticker(removeConstness(*destItem), *tool, request.stickerSlot);
            } else if ((*tool)->gameItem().isOperationPass()) {
                ToolUser{ *this, StaticData::lookup() }.activateOperationPass(*tool);
            } else if ((*tool)->gameItem().isViewerPass()) {
                return ToolUser{ *this, StaticData::lookup() }.activateViewerPass(*tool);
            } else if ((*tool)->gameItem().isNameTag()) {
                if (!destItem.has_value())
                    return {};

                return ToolUser{ *this, StaticData::lookup() }.addNameTag(removeConstness(*destItem), *tool, request.nameTag);
            } else if ((*tool)->gameItem().isPatch()) {
                if (!destItem.has_value())
                    return {};

                return ToolUser{ *this, StaticData::lookup() }.applyPatch(removeConstness(*destItem), *tool, request.stickerSlot);
            }
        } else if (request.action == UseToolRequest::Action::WearSticker) {
            if (!destItem.has_value())
                return {};
            return ToolUser{ *this, StaticData::lookup() }.wearSticker(removeConstness(*destItem), request.stickerSlot);
        }

        return {};
    }

    static bool updateStatTrak(inventory::Item_v2& item, int newStatTrak)
    {
        if (const auto skin = item.get<inventory::Skin>()) {
            skin->statTrak = newStatTrak;
            return true;
        }

        if (const auto music = item.get<inventory::Music>()) {
            music->statTrak = newStatTrak;
            return true;
        }

        return false;
    }

    [[nodiscard]] std::list<inventory::Item_v2>::iterator removeConstness(std::list<inventory::Item_v2>::const_iterator it)
    {
        return inventory.erase(it, it);
    }

    std::list<inventory::Item_v2> inventory;
    Loadout loadout;
    std::queue<Response> responses;
    ItemIDMap itemIDMap;
};

}
