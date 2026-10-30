# mod-bonus-talent-points

One AzerothCore **3.3.5a / WotLK** module for three **independently configurable** character-progression features:

1. **Bonus talent points**: +1 additional point at levels 15, 20, 25, 30, 35, 40, 45, 50, 55, 60 (81 total at level 80 when `Rate.Talent=1`). **Unchanged** from the previous module version.
2. **Glyph-slot unlock levels**: all six slots configurable by physical bit/slot index; original WotLK levels are the defaults.
3. **Dual specialization**: trainer purchase minimum level and **gold cost** configurable. Uses the original trainer gossip path and server checks, **without permanent changes to the world DB**.

The folder and loader name remain `mod-bonus-talent-points`. There are **no new SQL files** and **no client patch/DBC edits**. Tested with a standalone C++17 stub harness; **compile and in-game validation against your specific AzerothCore commit are still required**.

## Install or upgrade

> **Before replacing an existing module**, back up any customized module source and `mod_bonus_talent_points.conf`. On upgrade, **merge the new configuration options** from `.conf.dist` into the deployed `.conf` if you already have that file. Don't overwrite your existing custom levels without checking them.

1. Extract the archive to `/home/dev/azerothcore/modules/` (or `<your AzerothCore checkout>/modules/`). There must be **one** `modules/mod-bonus-talent-points/` directory; don't keep two versions.
2. Re-run your **existing CMake configuration** and rebuild/install `worldserver` with the same CMake flags/install prefix already in your environment.
3. Confirm the deployed `worldserver` configuration loads `mod_bonus_talent_points.conf`. AzerothCore usually installs it from `conf/mod_bonus_talent_points.conf.dist`; if an old deployed `.conf` exists, merge in the additional lines below.
4. Leave `Rate.Talent = 1` in `worldserver.conf` if you want exactly +10 points at level 80.
5. Restart the worldserver. Search logs for `BonusTalentPoints:`, `GlyphUnlocks:` and `DualSpec:` messages.

Example **only if** your existing build directory is `build/`:

```bash
cd /home/dev/azerothcore
unzip -o /path/to/mod-bonus-talent-points.zip -d modules/
cmake -S . -B build                 # use your existing CMake options/prefix
cmake --build build --target worldserver -j "$(nproc)"
cmake --install build                 # install to your existing prefix
```

The module is **not activated merely by copying files**. It must be compiled into AzerothCore and included in the installed worldserver. `cmake --install` may overwrite a deployed configuration; compare your files before restarting.

## Config reference

Edit `mod_bonus_talent_points.conf`, normally in the deployed server's configuration directory. The `.conf.dist` in this archive has comments and defaults.

```ini
# ---- EXTRA TALENT POINTS ----
BonusTalentPoints.Enable = 1
BonusTalentPoints.Levels = "15,20,25,30,35,40,45,50,55,60"
BonusTalentPoints.PointsPerMilestone = 1
BonusTalentPoints.DeferDeathKnightStarterBonus = 1

# ---- GLYPHS ----
GlyphUnlocks.Enable = 1
GlyphUnlocks.Major1.Level = 15
GlyphUnlocks.Minor1.Level = 15
GlyphUnlocks.Major2.Level = 30
GlyphUnlocks.Minor2.Level = 50
GlyphUnlocks.Minor3.Level = 70
GlyphUnlocks.Major3.Level = 80
GlyphUnlocks.ClearLockedGlyphs = 1

# ---- DUAL SPECIALIZATION ----
DualSpec.Enable = 1
DualSpec.MinLevel = 40
DualSpec.CostGold = 1000
```

You can set, for example, `DualSpec.MinLevel = 25` and `DualSpec.CostGold = 100` to allow purchase from level 25 for 100 gold, or `DualSpec.CostGold = 0` for free. `DualSpec.MinLevel` overrides the built-in `MinDualSpecLevel` while enabled. If `DualSpec.Enable = 0`, the built-in `worldserver.conf` setting determines the minimum level and the database's `BoxMoney` determines the purchase cost.

**Glyph bit order and defaults** follow the actual `Player::InitGlyphsForLevel` WotLK implementation:

| Configuration key | Internal slot | Flag | Original level |
|---|---:|---:|---:|
| `GlyphUnlocks.Major1.Level` | 0 | 0x01 | 15 |
| `GlyphUnlocks.Minor1.Level` | 1 | 0x02 | 15 |
| `GlyphUnlocks.Minor2.Level` | 2 | 0x04 | 50 |
| `GlyphUnlocks.Major2.Level` | 3 | 0x08 | 30 |
| `GlyphUnlocks.Minor3.Level` | 4 | 0x10 | 70 |
| `GlyphUnlocks.Major3.Level` | 5 | 0x20 | 80 |

All six levels accept 1..255; values outside this range fall back to their respective defaults and log an error. Glyph slots are set by `PLAYER_GLYPHS_ENABLED`; visual locked-slot tooltips in the **unmodified client** may still refer to the standard required level.

### IMPORTANT: restricting previously unlocked glyphs

If you change a slot's required level **upward**, an existing character may have a glyph inserted into that slot already. The default `GlyphUnlocks.ClearLockedGlyphs = 1` **irreversibly removes the now-illegal glyph, and its active aura, upon login, level change or spec activation**. Installed glyph items are **not refunded**. Inactive specs are checked as soon as they are activated. Make a character database backup before imposing later levels on existing characters.

You may set `GlyphUnlocks.ClearLockedGlyphs = 0` to retain existing glyphs, but that **does not enforce removal of an existing glyph's effect** while its slot is locked; use this setting only when you understand the trade-off. Simply **lowering** default unlock levels needs no cleanup and is nondestructive. For straightforward earlier unlocks, keep this option at `1`.

### Dual-spec cost implementation

AzerothCore's native purchase flow reads the menu option's `BoxMoney` (in **copper**) for **both** displaying the confirmation cost and deducting money. The native level check uses `CONFIG_MIN_DUALSPEC_LEVEL`.

On startup, this module reads **only** menu IDs/options with `OptionType=18` from the world DB and changes the `BoxMoney` of their already-loaded in-memory `ObjectMgr` entries. It does **not** write to `gossip_menu_option`, does **not** insert new gossip options and does **not** duplicate the payment. These options remain attached to their existing class trainers. Once disabled via configuration reload, prices are restored to their **initial** in-memory values.

- Configured gold is converted to copper (`gold * 10000`); maximum **214748 gold** because the core uses an **int32** payment check. Values above this limit fall back to 1000 gold. Zero is allowed.
- Price changes apply when a **new** gossip is prepared. If a player has an old confirmation popup open across a config reload, they should close and reopen the trainer dialogue first.
- Scripts that provide their **own** dual-spec purchase menus (not `OptionType=18`) are unaffected. Modules that directly rewrite the same `ObjectMgr` gossip entries may conflict.
- If you run `.reload gossip_menu_option` (or a DB menu reload), repeat `.reload config` to reapply the price. Menu entries added after world startup require a **worldserver restart** for this module to discover them.
- A read-only query is made at worldserver startup; the DB data is not modified.

### Bonus talent points and Death Knights

As before, +1 bonus point is granted cumulatively at each of the ten configured levels. Default at 60: **61** total; at 80: **81**. The points are calculated dynamically, with no additional character table. The module avoids DK starting map **609** by default to preserve normal introductory quest progression. When a DK exits that map its bonus points are calculated. Existing characters get points on login; dual specs use their own native calculation.

## Configuration reload

`.reload config` reloads module options. The **dual-spec level** and **newly prepared gossip prices** refresh immediately. For **already-online players**, glyph masks and available talent points refresh at the next **login/level change/spec switch** (talents also refresh when their usual AzerothCore code recalculates). A relog is recommended after changing the rules. When disabling `GlyphUnlocks.Enable`, a relog returns the player to the core's native glyph mask.

## Manual acceptance checks

- [ ] Start worldserver after rebuilding; three module log prefixes appear.
- [ ] Non-DK levels 14/15/20/60/80 give 5/7/13/61/81 points respectively (before spending).
- [ ] DK inside map 609 keeps intro quest progression; leaving provides the bonus.
- [ ] With original glyph defaults: level 15 mask `0x03`, level 30 `0x0B`, level 50 `0x0F`, level 70 `0x1F`, level 80 `0x3F`.
- [ ] Set `GlyphUnlocks.Major2.Level = 25`; at level 25 second major slot unlocks and accepts a glyph; it survives re-log and dual spec switches.
- [ ] Test `DualSpec.MinLevel = 25`, `DualSpec.CostGold = 100` on a new character with exactly 100 gold. Check popup **and** actual deduction; a level 24 character must not be able to purchase.
- [ ] With `DualSpec.CostGold = 0`, purchase is free; with `DualSpec.Enable = 0`, original cost returns after config reload.
- [ ] For later unlocks: test occupied slot relocking in **development** only, after backing up the character DB; test both active and inactive specs.
- [ ] Check any other module that customizes glyphs, trainers or dual spec (notably progression/level restrictions).

## Verification

Run isolated (mock/stub) tests with:

```bash
cd modules/mod-bonus-talent-points
./tests/run_stub_tests.sh
```

They compile all three features and check milestones, DK exception, glyph bit masks and removal, trainer cost in copper, level override, config reload and price restoration. These isolated tests **are not equivalent to linking against your running AzerothCore**.

## Implementation links

- [AzerothCore `Player::InitGlyphsForLevel`](https://www.azerothcore.org/doxygen/d2/d4b/classPlayer.html)
- [AzerothCore `PlayerGossip.cpp`](https://github.com/azerothcore/azerothcore-wotlk/blob/master/src/server/game/Entities/Player/PlayerGossip.cpp)
- [AzerothCore `World.h` (`setIntConfig`)](https://github.com/azerothcore/azerothcore-wotlk/blob/master/src/server/game/World/World.h)

License: see `LICENSE`.
