/*
 * mod-bonus-talent-points for AzerothCore 3.3.5a
 * License: MIT (see LICENSE).
 */

#include "Config.h"
#include "Log.h"
#include "Player.h"
#include "ScriptMgr.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>
#include <utility>

namespace BonusTalentPoints
{
namespace
{
    constexpr uint32 DEATH_KNIGHT_START_MAP = 609; // Acherus / Ebon Hold

    struct Settings
    {
        bool enabled = true;
        bool deferDkStartingZone = true;
        uint32 pointsPerMilestone = 1;
        std::vector<uint32> milestones {15, 20, 25, 30, 35, 40, 45, 50, 55, 60};
    };

    Settings settings;

    bool ParseMilestones(std::string const& value, std::vector<uint32>& result)
    {
        std::istringstream input(value);
        std::string token;
        std::vector<uint32> levels;

        while (std::getline(input, token, ','))
        {
            // Accept surrounding whitespace, but never silently accept invalid text.
            auto const first = std::find_if_not(token.begin(), token.end(), [](unsigned char c) { return std::isspace(c); });
            auto const last = std::find_if_not(token.rbegin(), token.rend(), [](unsigned char c) { return std::isspace(c); }).base();
            if (first >= last)
                return false;

            std::string const trimmed(first, last);
            if (!std::all_of(trimmed.begin(), trimmed.end(), [](unsigned char c) { return std::isdigit(c); }))
                return false;

            try
            {
                unsigned long const level = std::stoul(trimmed);
                if (level < 10 || level > 255)
                    return false;
                levels.push_back(static_cast<uint32>(level));
            }
            catch (...)
            {
                return false;
            }
        }

        if (levels.empty())
            return false;

        std::sort(levels.begin(), levels.end());
        levels.erase(std::unique(levels.begin(), levels.end()), levels.end());
        result = std::move(levels);
        return true;
    }

    void LoadSettings()
    {
        Settings next;
        next.enabled = sConfigMgr->GetOption<bool>("BonusTalentPoints.Enable", true);
        next.deferDkStartingZone = sConfigMgr->GetOption<bool>("BonusTalentPoints.DeferDeathKnightStarterBonus", true);
        next.pointsPerMilestone = sConfigMgr->GetOption<uint32>("BonusTalentPoints.PointsPerMilestone", 1);

        std::string const levels = sConfigMgr->GetOption<std::string>(
            "BonusTalentPoints.Levels", "15,20,25,30,35,40,45,50,55,60");

        if (!ParseMilestones(levels, next.milestones))
        {
            LOG_ERROR("module", "BonusTalentPoints: invalid Levels='{}'. Using defaults.", levels);
            next.milestones = {15, 20, 25, 30, 35, 40, 45, 50, 55, 60};
        }

        // Keep the total within a reasonable limit for clients / configuration mistakes.
        // Zero is supported as an easy way to disable just the point reward.
        if (next.pointsPerMilestone > 10)
        {
            LOG_ERROR("module", "BonusTalentPoints: PointsPerMilestone={} is too high (max 10). Using 1.",
                next.pointsPerMilestone);
            next.pointsPerMilestone = 1;
        }

        settings = std::move(next);
        LOG_INFO("module", "BonusTalentPoints: enabled={}, milestones={}, points each={}, DK starting zone deferred={}",
            settings.enabled, settings.milestones.size(), settings.pointsPerMilestone, settings.deferDkStartingZone);
    }

    bool IsDeferredDeathKnight(Player const* player)
    {
        return settings.deferDkStartingZone &&
            player->getClass() == CLASS_DEATH_KNIGHT &&
            player->GetMapId() == DEATH_KNIGHT_START_MAP;
    }

    uint32 CalculateBonus(uint32 level)
    {
        auto const end = std::upper_bound(settings.milestones.begin(), settings.milestones.end(), level);
        auto const count = static_cast<uint32>(std::distance(settings.milestones.begin(), end));
        return count * settings.pointsPerMilestone; // count <= 246; points <= 10
    }
}

class BonusTalentPointsWorldScript final : public WorldScript
{
public:
    BonusTalentPointsWorldScript()
        : WorldScript("BonusTalentPointsWorldScript", {WORLDHOOK_ON_AFTER_CONFIG_LOAD}) { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        LoadSettings();
        // For already-online players after a .reload config, the new rules apply
        // on the next level change, login, map change (DK) or talent reset.
    }
};

class BonusTalentPointsPlayerScript final : public PlayerScript
{
public:
    BonusTalentPointsPlayerScript()
        : PlayerScript("BonusTalentPointsPlayerScript", {
            PLAYERHOOK_ON_CALCULATE_TALENTS_POINTS,
            PLAYERHOOK_ON_LOGIN,
            PLAYERHOOK_ON_MAP_CHANGED
        }) { }

    void OnPlayerCalculateTalentsPoints(Player const* player, uint32& totalPoints) override
    {
        if (!settings.enabled || !settings.pointsPerMilestone || IsDeferredDeathKnight(player))
            return;

        uint32 const bonus = CalculateBonus(player->GetLevel());
        // The core may have already granted points from quests or other modules.
        if (bonus > std::numeric_limits<uint32>::max() - totalPoints)
            totalPoints = std::numeric_limits<uint32>::max();
        else
            totalPoints += bonus;
    }

    void OnPlayerLogin(Player* player) override
    {
        // The core also calculates on player load; this makes retroactive
        // grants explicit and keeps the result correct for existing characters.
        player->InitTalentForLevel();
    }

    void OnPlayerMapChanged(Player* player) override
    {
        // Entering/leaving the Death Knight starting map changes the rule.
        // Recalculate when the map changes, without database writes.
        if (player->getClass() == CLASS_DEATH_KNIGHT && settings.deferDkStartingZone)
            player->InitTalentForLevel();
    }
};

void AddBonusTalentPointsScripts()
{
    new BonusTalentPointsWorldScript();
    new BonusTalentPointsPlayerScript();
}
}

// Export the conventional module loader entry point from a separate file.
void AddBonusTalentPointsScripts()
{
    BonusTalentPoints::AddBonusTalentPointsScripts();
}
