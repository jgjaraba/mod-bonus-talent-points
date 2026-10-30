/*
 * Configurable dual-specialization minimum level and NPC gossip cost.
 * License: MIT (see LICENSE).
 *
 * The core already supports CONFIG_MIN_DUALSPEC_LEVEL. Cost is BoxMoney on
 * OptionType=GOSSIP_OPTION_LEARNDUALSPEC (18) in gossip_menu_option.
 * We patch the already-loaded ObjectMgr template in memory, not the DB.
 * No custom NPC, no duplicate payment, and the confirmation shows the
 * EXACT price the unmodified core deducts on purchase.
 */

#include "Config.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "ScriptMgr.h"
#include "World.h"

#include <cstdint>
#include <limits>
#include <map>
#include <utility>
#include <vector>

namespace DualSpecCustomization
{
namespace
{
    constexpr uint32 STANDARD_LEVEL = 40;
    constexpr uint32 STANDARD_PRICE_GOLD = 1000;
    constexpr uint32 COPPER_PER_GOLD = 10000;
    // Player::OnGossipSelect casts BoxMoney to int32 before checking/deducting.
    constexpr uint32 MAX_SAFE_PRICE_GOLD = uint32(std::numeric_limits<std::int32_t>::max()) / COPPER_PER_GOLD;
    constexpr uint32 GOSSIP_OPTION_LEARN_DUAL_SPEC = 18;

    bool enabled = true;
    uint32 requiredLevel = STANDARD_LEVEL;
    uint32 priceGold = STANDARD_PRICE_GOLD;
    bool worldStarted = false;

    using MenuKey = std::pair<uint32, uint32>; // MenuID, OptionID
    std::map<MenuKey, uint32> originalPrices;

    void ApplyPrice()
    {
        if (!worldStarted)
            return;

        uint32 modified = 0;
        for (auto const& [key, originalPrice] : originalPrices)
        {
            auto const bounds = sObjectMgr->GetGossipMenuItemsMapBounds(key.first);
            for (auto it = bounds.first; it != bounds.second; ++it)
            {
                if (it->second.OptionID != key.second || it->second.OptionType != GOSSIP_OPTION_LEARN_DUAL_SPEC)
                    continue;

                // ObjectMgr owns writable entries; its public lookup exposes a
                // const_iterator. We only mutate BoxMoney, on the world thread.
                // Templates are neither freed nor replaced here.
                auto& option = const_cast<GossipMenuItems&>(it->second);
                option.BoxMoney = enabled ? priceGold * COPPER_PER_GOLD : originalPrice;
                ++modified;
            }
        }
        LOG_INFO("module", "DualSpec: adjusted {} gossip purchase entries in memory ({} gold); DB unchanged.",
            modified, enabled ? priceGold : 0);
    }

    void Load()
    {
        enabled = sConfigMgr->GetOption<bool>("DualSpec.Enable", true);
        requiredLevel = sConfigMgr->GetOption<uint32>("DualSpec.MinLevel", STANDARD_LEVEL);
        priceGold = sConfigMgr->GetOption<uint32>("DualSpec.CostGold", STANDARD_PRICE_GOLD);

        if (requiredLevel < 1 || requiredLevel > 255)
        {
            LOG_ERROR("module", "DualSpec: invalid MinLevel={}, using {}.", requiredLevel, STANDARD_LEVEL);
            requiredLevel = STANDARD_LEVEL;
        }
        if (priceGold > MAX_SAFE_PRICE_GOLD)
        {
            LOG_ERROR("module", "DualSpec: CostGold={} exceeds safe max {}; using {}.",
                priceGold, MAX_SAFE_PRICE_GOLD, STANDARD_PRICE_GOLD);
            priceGold = STANDARD_PRICE_GOLD;
        }

        // Built-in AzerothCore check controls BOTH visibility and purchase.
        // World reload loads MinDualSpecLevel first; this module then overrides
        // it when enabled. When disabled, native worldserver.conf wins.
        if (enabled)
            sWorld->setIntConfig(CONFIG_MIN_DUALSPEC_LEVEL, requiredLevel);
        ApplyPrice();
        LOG_INFO("module", "DualSpec: enabled={}, level={}, cost={} gold.", enabled, requiredLevel, priceGold);
    }

    void InitializePrices()
    {
        originalPrices.clear();

        // Enumerate all DB-defined dual-spec purchase menu options, including
        // custom trainers that use the core's OptionType 18. Read-only query.
        QueryResult rows = WorldDatabase.Query(
            "SELECT MenuID, OptionID FROM gossip_menu_option WHERE OptionType = 18");
        if (rows)
        {
            do
            {
                Field* fields = rows->Fetch();
                MenuKey key {fields[0].Get<uint32>(), fields[1].Get<uint32>()};
                auto bounds = sObjectMgr->GetGossipMenuItemsMapBounds(key.first);
                for (auto it = bounds.first; it != bounds.second; ++it)
                {
                    if (it->second.OptionID == key.second && it->second.OptionType == GOSSIP_OPTION_LEARN_DUAL_SPEC)
                        originalPrices.emplace(key, it->second.BoxMoney);
                }
            } while (rows->NextRow());
        }
        if (originalPrices.empty())
            LOG_ERROR("module", "DualSpec: no gossip_menu_option purchase entries (OptionType=18) found; price override inactive.");
        worldStarted = true;
        ApplyPrice();
    }
}

class DualSpecWorldScript final : public WorldScript
{
public:
    DualSpecWorldScript() : WorldScript("DualSpecWorldScript", {
        WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP
    }) { }

    void OnAfterConfigLoad(bool /*reload*/) override { Load(); }
    void OnStartup() override { InitializePrices(); }
};
}

void AddDualSpecCustomizationScripts()
{
    new DualSpecCustomization::DualSpecWorldScript();
}
