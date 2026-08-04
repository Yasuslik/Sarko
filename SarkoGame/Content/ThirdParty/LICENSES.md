# Third-party art

Everything under `Content/ThirdParty/` is generated from the source meshes in
`Art/ThirdParty/` by `Scripts/import-assets.sh`. Do not hand-edit the `.uasset`
files — re-run the script.

Every file below is **CC0 1.0 Universal (Public Domain Dedication)**. CC0
requires no attribution; this file exists so the provenance of a mesh is a fact
in the repository rather than something only the person who downloaded it knows.
Each pack's own `License.txt` is committed next to its meshes.

## Packs

| Pack | Author | Source | Licence |
|---|---|---|---|
| Ultimate Nature Pack | Quaternius | <https://quaternius.com/packs/ultimatenature.html> | CC0 1.0 |
| Zombie Apocalypse Kit | Quaternius | <https://quaternius.com/packs/zombieapocalypsekit.html> | CC0 1.0 |
| Cars Pack | Quaternius | <https://quaternius.com/packs/cars.html> | CC0 1.0 |
| Ultimate Gun Pack | Quaternius | <https://quaternius.com/packs/ultimategun.html> | CC0 1.0 |

Each pack's download button points at a public Google Drive folder holding one
FBX per model; the files below were fetched individually from those folders on
2026-08-02. The folder ids, for a re-fetch:

- Ultimate Nature Pack — `1-Kl0L_Jg8awbh0S5T-z3zxh4mVlnxTpa` (FBX subfolder `1uoIaSvBzm8SrC7g-feRK6ewzHVUGApE0`)
- Zombie Apocalypse Kit — `1mWP6sCHun7OUMHQeDNZLrXTteXlzWg_t` (Environment/FBX `1VvZDkZYU3UZ2r8fA3BUcEpQGxXKP70SU`, Vehicles/FBX `1wwStZozfAamZV76sB0PY6aAGUb3EGxdj`)
- Cars Pack — `1fKlbDry77iY8KlEoxzUxIAZQL_XhzWlA` (FBX subfolder `1cjhc5GgiFR_pqPINeW99XDeWc62FWDlY`)
- Ultimate Gun Pack — `12V-mHNB6bnW2WzgpJfRBQd-TG4pOO3yx` (FBX subfolder
  `1hLw5riEcdvRgExDks3ywcAaTIlTX5J3Z`), fetched 2026-08-04. A Drive folder's
  listing is not in the page HTML; `https://drive.google.com/embeddedfolderview?id=<id>#list`
  returns it, and a file is then fetched from
  `https://drive.usercontent.google.com/download?id=<fileid>&export=download`.

## Files kept

`Art/ThirdParty/UltimateNature/` — Quaternius Ultimate Nature Pack, CC0 1.0

| File | Bytes | Used as |
|---|---|---|
| `CommonTree_3.fbx` | 48396 | `tree` — trunk + canopy (split at import) |
| `CommonTree_4.fbx` | 43500 | `tree_small` — trunk + canopy |
| `CommonTree_Dead_3.fbx` | 33148 | `tree_dead` |
| `PineTree_5.fbx` | 48316 | `tree_tall` — trunk + canopy |
| `Rock_6.fbx` | 16764 | `rock` |
| `Bush_1.fbx` | 22924 | `bush` |
| `WoodLog.fbx` | 26428 | `log` |
| `License.txt` | 374 | pack licence |

`Art/ThirdParty/ZombieApocalypse/` — Quaternius Zombie Apocalypse Kit, CC0 1.0

| File | Bytes | Used as |
|---|---|---|
| `WaterTower.fbx` | 44956 | `water_tower` |
| `Barrel.fbx` | 39756 | `barrel` |
| `Pallet.fbx` | 21436 | `pallet`, `pallet_stack` |
| `Pipes.fbx` | 54780 | `pipe_run` |
| `CinderBlock.fbx` | 24748 | staged, and judged NOT worth placing |
| `Wheels_Stack.fbx` | 65404 | staged, and judged NOT worth placing |

The last two were looked at properly during the procedural prop pass and turned
down rather than left in limbo. `CinderBlock` is 47 cm long: from the game's
1400 uu camera that is about two pixels, so it can only ever be noise. Every
component in `ASarkoPropField` is a draw call, and `Wheels_Stack` costs one for
1824 triangles of prop that resolves to a dark blob at 92 uu tall — the same
draw call spent on `barrel` dresses four locations. They stay imported because
the pack is committed and re-running `Scripts/import-assets.sh` would bring them
back anyway; they are simply not in the kind table.
| `License.txt` | 364 | pack licence |

`Art/ThirdParty/Cars/` — Quaternius Cars Pack, CC0 1.0

| File | Bytes | Used as |
|---|---|---|
| `NormalCar1.fbx` | 72732 | `car_wreck` |
| `License.txt` | 364 | pack licence |

`Art/ThirdParty/UltimateGuns/` — Quaternius Ultimate Gun Pack, CC0 1.0

Three weapons out of forty. These are the first assets here that are NOT props:
they are scaled uniformly to the real gun's real length and stood on Z=0 by
`Scripts/prepare-assets.py`'s `weapon` mode, not stretched into the -50..50 box,
because a weapon is read by its silhouette and a normalised ПМ would be a
one-metre cube in a fist. They must never be given a prop kind.

| File | Bytes | Used as | Length | Triangles |
|---|---|---|---|---|
| `Pistol_1.fbx` | 36428 | `pistol` — the ПМ | 16.1 uu | 1040 |
| `AssaultRifle_2.fbx` | 39388 | `rifle` — the АКМ | 88 uu | 1306 |
| `Shotgun_2.fbx` | 30012 | `shotgun` | 105 uu | 746 |
| `License.txt` | 364 | pack licence | | |

No textures at all: the pack is flat-shaded and its five material slots are
named after colours. Nothing on iOS's 512-capped world texture group changes.
The slots are repainted at attach time with the sector's own surface materials
(Timber for wood, Structure for everything else) — see the note below, and
`SarkoWeaponVisuals::SetHeldWeapon`.

**The imported materials do not render, and never have.** An imported mesh drawn
with the materials Interchange builds from these FBX files is invisible in game —
the same component, at the same place, with `/Engine/BasicShapes/Cube` in it,
draws. Every prop in the sector has been sidestepping this since the first
import pass without anyone noticing, because `ASarkoPropField::AddPart`
overwrites every material slot with a shared surface material. A held weapon was
simply the first asset to try to use one. Recorded here because the next person
to import a pack and wonder where their mesh went deserves the answer: paint it.

The five "staged, not yet placed" props are imported but referenced by no prop
kind. They are the gas station and rail depot's dressing and are waiting on a
map-authoring pass — placing them means editing `Data/Maps/bridge.json`, which
this change deliberately did not do.

## Also downloaded, then discarded

Fetched while vetting and deleted rather than committed. All CC0 1.0, all from
the same three Drive folders, re-fetchable by name:

- Ultimate Nature Pack: `BirchTree_1`, `BirchTree_2`, `BirchTree_Dead_1`,
  `Bush_2`, `BushBerries_1`, `CommonTree_1`, `CommonTree_2`, `CommonTree_5`,
  `CommonTree_Dead_1`, `CommonTree_Dead_2`, `CommonTree_Dead_4`, `PineTree_1`,
  `PineTree_2`, `PineTree_3`, `Rock_1`…`Rock_5`, `Rock_7`, `TreeStump` — variants
  of kinds that are already covered. Kept out because an imported mesh no kind
  names is weight in the repository and in the cook.
- Zombie Apocalypse Kit: `Container_Green` (would need a 1.6x stretch to reach
  `freight_car`'s authored 14 m, which is visible on a corrugated wall),
  `Pallet_Broken`, `Vehicle_Pickup`, `Vehicle_Truck` (no kind has their
  proportions — `bus` is 12 m against the truck's 5.3 m).
- Ultimate Gun Pack, the weapon pass of 2026-08-04: seventeen guns were
  downloaded, rendered side-on in Blender at a shared scale, and looked at.
  Three were kept. The other fourteen, by name and by reason:
  - `Pistol_3`, `Pistol_6` — rails top and bottom, and `Pistol_3` has
    compensator ports. Tacticool; a scavenger in a ravine does not own one.
  - `Pistol_4`, `Pistol_5` — modern polymer service pistols with long slides.
    Real guns, wrong decade and wrong side of the map. `Pistol_1` is the one
    with the plain slab slide and the wooden grip, which is what a ПМ is.
  - `Pistol_2` — the closest runner-up, a TT-ish long-barrelled version of
    `Pistol_1`. Turned down only because two near-identical pistols is one
    pistol and a duplicate.
  - `AssaultRifle_1`, `AssaultRifle_3` — AK pattern with a full-length top rail
    and no stock. The rail is the disqualifier.
  - `AssaultRifle_4` (underfolder wire stock), `AssaultRifle_5` (black polymer
    furniture) — both genuinely in period and both fine. `AssaultRifle_2`'s
    wooden furniture reads as post-Soviet from further away, which at 1400 uu
    is the whole argument.
  - `Shotgun_1` (grey furniture), `Shotgun_3` (dark forend), `Shotgun_4` (a
    single-barrel break action) — all three fit; `Shotgun_2` is the wood-stocked
    pump and is 746 triangles, the cheapest mesh in the pack.
  - `SubmachineGun_1`, `SubmachineGun_2` — rejected on theme. Modern Western
    SMGs (MP7/UMP silhouettes) with full-length picatinny rails. The brief asked
    for an SMG "if a good one fits"; neither of these does, and no ППШ, ПП-91 or
    anything else Soviet is in the pack. **No SMG was taken.**
  Twenty-three more were never downloaded, judged from the pack's own preview
  sheet: `AssaultRifle2_1`…`_4` (M4/AR-15 pattern, rails), `Bullpup_1`…`_3`,
  `Revolver_1`…`_5` (a revolver is not a post-Soviet sidearm),
  `SniperRifle_4`…`_6` and `SubmachineGun_3`…`_5`. `SniperRifle_1`…`_3` WERE
  downloaded and are the near miss worth recording: olive bolt-action hunting
  rifles with scopes, entirely in theme, and left out only because the brief
  wanted at most four weapons and a scoped hunting rifle is a third long gun
  after the AKM and the pump. They are the first thing to reach for when the
  catalog wants a fourth.
- Cars Pack: `NormalCar2`, `SUV` — `NormalCar1`'s proportions are the closest to
  `car_wreck`'s authored 230 x 95 x 75: 4% off in width and 17% in height,
  against 17%/6% for NormalCar2 (which is a 3.3 m hatchback, too short for a
  4.6 m extent) and 18%/10% for the SUV.

## Packs vetted and rejected

Judged from their own preview renders, against the sector's brief — an east
European wasteland in muted olive, grey and rust.

- **Quaternius Ultimate Stylized Nature** — rejected on theme. A fairytale wood:
  candy greens, red and pink blossom, blue and violet flowers, a marble arch. It
  is a beautiful pack and it is a different game.
- **Quaternius Ultimate Modular Ruins** — rejected on theme. Medieval castle
  ruins with unicorn statuary; nothing in it is post-industrial.
- **Quaternius Survival Pack** — rejected on scope, not theme. Hand props
  (matches, cans, a first aid kit, firearms) at inventory scale. Worth revisiting
  when loot items get meshes; useless for dressing a sector.
- **Kenney City Kit (Industrial)** — rejected on theme. A clean toy-town
  diorama: white and pale-grey blocks, bright blue windows, lollipop trees. The
  silhouettes are miniature-model, not built. Kenney's warning in the brief was
  correct.
- **Quaternius Zombie Apocalypse Kit — characters and armoured vehicles** —
  rejected on theme. Big-headed cartoon survivors, green zombies, blue blobs,
  a spiked guitar. Its *environment* half is grounded and is where the props
  above come from; nothing else in the pack was taken.
- **Quaternius Universal Base Characters** — rejected on theme, and not
  downloaded. The preview is idealised heroic anatomy in a hero-shooter key:
  gym-built torsos, clean skin, bright hair. Sarko's people are scavengers in a
  ravine. The free tier is also two *unclothed* base bodies — the clothing that
  would make them anything is in the $19.99 Source tier, so even ignoring the
  look it is a body kit rather than characters. Practical note for whoever picks
  this up later: its itch.io page is name-your-own-price, so the download is
  behind an interactive purchase form rather than a direct link. On retargeting
  the page claims only "Humanoid Rig compatible with retargeting in any engine",
  which is a claim about intent and not about bone names — nothing was inspected
  and nothing should be assumed. UE 5.8 retargets across arbitrary skeletons
  through an IK Rig per skeleton, so feasibility is a question of authoring two
  IK Rigs rather than of matching names; the project's existing mannequin
  skeleton and the owner's untracked Fab `Content/QuantumCharacter` are both
  closer starting points than this.

- **Quaternius Sci-Fi Gun Pack** and **Sci-Fi Modular Gun Pack** — rejected on
  theme without downloading. They say what they are in their names.
- **poly.pizza** — searched for AK and pistol meshes and not used, though its
  licences (CC0) and downloads (no account) are fine. Its top AK results are by
  Quaternius and are the same pack this took, so going through an aggregator
  would have meant three meshes from three authors in three styles standing in
  three different hands. One pack, one artist, and the same artist as the trees
  and the barrels already in the sector, is worth more than a marginally better
  individual gun.
- **OpenGameArt.org** — not used, same reason, plus its CC0 weapon packs are
  mostly fantasy or WIP. Nothing was downloaded from it.
- **itch.io free CC0 weapon packs** and **ITHappy Studios' free weapons pack** —
  not taken and not downloaded. Both were in the brief with the warning that
  "free" is not "CC0", and that warning is the finding: itch.io's free packs are
  per-pack licensed and several of the popular ones are name-your-own-price,
  which puts the download behind an interactive purchase form rather than a
  direct link — the same obstacle the Universal Base Characters note above
  records. With a CC0 pack by an artist already in this repository available at
  a direct link, spending the vetting on those was not worth it. If a later pass
  needs a specific weapon this pack lacks, ITHappy is the place to look and its
  actual EULA is the thing to read first.
- **Poly Haven** — not taken. Photoscanned CC0 rocks and logs are real art, but
  one photoreal rock in a stand of flat-shaded low-poly trees reads as a bug
  rather than as detail, and the polycounts are one to two orders of magnitude
  above everything else here. Mixing the two is a decision for a project that has
  chosen photoreal throughout.
