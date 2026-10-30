/*
 * Configurable glyph slot unlock levels for AzerothCore 3.3.5a.
 * License: MIT (see LICENSE).
 *
 * Bit/slot mapping matches Player::InitGlyphsForLevel in AzerothCore:
 *  slot 0/bit 0 (major 1) at 15; slot 1/bit 1 (minor 1) at 15;
 *  slot 3/bit 3 (major 2) at 30; slot 2/bit 2 (minor 2) at 50;
 *  slot 4/bit 4 (minor 3) at 70; slot 5/bit 5 (major 3) at 80.
 */

#include "Config.h"
#include "DBCStores.h"
#include "Log.h"
#include "Player.h"
#include "ScriptMgr.h"

#include <array>
#include <cstdint>

namespace GlyphUnlocks
{
namespace
{
    // These keys deliberately match physical glyph slot indices (bits 0..5).
    constexpr std::array<char const*, 6> LEVEL_KEYS = {{
        "GlyphUnlocks.Major1.Level", // bit 0
        "GlyphUnlocks.Minor1.Level", // bit 1
        "GlyphUnlocks.Minor2.Level", // bit 2
        "GlyphUnlocks.Major2.Level", // bit 3
        "GlyphUnlocks.Minor3.Level", // bit 4
        "GlyphUnlocks.Major3.Level"  // bit 5
    }};
    constexpr std::array<uint32, 6> DEFAULT_LEVELS = {{15, 15, 50, 30, 70, 80}};

    bool enabled = true;
    bool clearLockedGlyphs = true;
    std::array<uint32, 6> unlockLevels = DEFAULT_LEVELS;

    void Load()
    {
        enabled = sConfigMgr->GetOption<bool>("GlyphUnlocks.Enable", true);
        clearLockedGlyphs = sConfigMgr->GetOption<bool>("GlyphUnlocks.ClearLockedGlyphs", true);
        for (uint32 i = 0; i < unlockLevels.size(); ++i)
        {
            uint32 level = sConfigMgr->GetOption<uint32>(LEVEL_KEYS[i], DEFAULT_LEVELS[i]);
            if (level < 1 || level > 255)
            {
                LOG_ERROR("module", "GlyphUnlocks: invalid {}={} (valid 1..255). Defaulting to {}.",
                    LEVEL_KEYS[i], level, DEFAULT_LEVELS[i]);
                level = DEFAULT_LEVELS[i];
            }
            unlockLevels[i] = level;
        }
        LOG_INFO("module", "GlyphUnlocks: enabled={}, clear locked glyphs={}", enabled, clearLockedGlyphs);
    }

    uint32 EnabledMask(uint32 level)
    {
        uint32 mask = 0;
        for (uint32 i = 0; i < unlockLevels.size(); ++i)
            if (level >= unlockLevels[i])
                mask |= (uint32(1) << i);
        return mask;
    }

    void Apply(Player* player)
    {
        if (!enabled)
            return; // The core manages the normal glyph mask in this mode.

        uint32 mask = EnabledMask(player->GetLevel());
        if (clearLockedGlyphs)
        {
            // Discard glyphs that are no longer legal after a stricter level config,
            // including the matching aura on the ACTIVE spec. Inactive spec glyphs
            // are verified upon switching to that spec (see hook below).
            for (uint8 slot = 0; slot < 6; ++slot)
            {
                if ((mask & (uint32(1) << slot)) || !player->GetGlyph(slot))
                    continue;

                uint32 glyph = player->GetGlyph(slot);
                if (GlyphPropertiesEntry const* entry = sGlyphPropertiesStore.LookupEntry(glyph))
                    player->RemoveAurasDueToSpell(entry->SpellId);

                player->SetGlyph(slot, 0, true);
            }
        }
        // Do this after vanilla initialization on login/level change.
        player->SetUInt32Value(PLAYER_GLYPHS_ENABLED, mask);
    }
}

class GlyphUnlocksWorldScript final : public WorldScript
{
public:
    GlyphUnlocksWorldScript() : WorldScript("GlyphUnlocksWorldScript", {WORLDHOOK_ON_AFTER_CONFIG_LOAD}) { }
    void OnAfterConfigLoad(bool /*reload*/) override { Load(); }
};

class GlyphUnlocksPlayerScript final : public PlayerScript
{
public:
    GlyphUnlocksPlayerScript() : PlayerScript("GlyphUnlocksPlayerScript", {
        PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_LEVEL_CHANGED, PLAYERHOOK_ON_AFTER_SPEC_SLOT_CHANGED
    }) { }

    void OnPlayerLogin(Player* player) override { Apply(player); }
    void OnPlayerLevelChanged(Player* player, uint8 /*oldLevel*/) override { Apply(player); }
    void OnPlayerAfterSpecSlotChanged(Player* player, uint8 /*newSlot*/) override { Apply(player); }
};
}

void AddGlyphUnlocksScripts()
{
    new GlyphUnlocks::GlyphUnlocksWorldScript();
    new GlyphUnlocks::GlyphUnlocksPlayerScript();
}
