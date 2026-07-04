# godgame — the island finds its voice (M12)

Decisions locked with the owner: SFX + day/night ambience beds, NO music
yet (the .sad overlay will do music justice later); everything synthesized —
no asset files, no new dependencies.

## 1. Architecture

- `src/Sound.*` (`snd::`): SDL audio device + a 24-voice float mixer +
  a PCM bank **synthesized at startup** (`bake()`, deterministic XorShift
  seeds — `bankChecksum()` proves it in the suite). The first
  `Bed::Count` voices belong to the ambience beds forever; one-shots
  steal the quietest free voice. Panning/attenuation happen at `playAt`
  time against a per-frame listener (camera eye + forward).
- **The firewall holds**: the sim NEVER calls `snd::` — it appends to
  `World::events` (the M11 seam, grown new kinds: Thud, Splash, Scream,
  Chop, TreeFall, Clack, Complete) and the app scores them in
  `drainWorldEvents` (positional, capped at 12 one-shots a frame).
  Events stay render/audio data: never a sim input, never checksummed,
  never saved.
- **Silence is a feature**: no device (or `--headless`, which never opens
  one) leaves every call a safe no-op — proven by test [20] calling the
  whole API deviceless.

## 2. The cues

| moment | sound |
|---|---|
| prop / villager lands | ThudSoft / ThudHard by impact, gain by speed |
| something meets the sea | Splash |
| villager grabbed / hurled / blast-flung | Scream (pitch varies by spot) |
| axe bites, trunk falls | Chop, TreeFall |
| scaffold placed / combined, totem upgraded | Clack |
| building completes | Complete (chime) |
| miracle cast | Cast sparkle (rain low-pitched); fireball casts Whoosh |
| forest takes root | Bloom swell |
| fireball lands | Explosion (+ the Scream of the flung) |
| village converts | Bell — yours bright, theirs tolling lower |
| temple collapses | Rumble |
| hand grab / gentle place / throw | soft pluck / Place / Whoosh |
| menu move / select / refused cast | UiMove, UiSelect, UiDeny |

## 3. The beds

Wind (always, low), Birds (daylight), Crickets (night), Fire (night,
swelling near campfires), RainBed (near an active shower), Chant (near a
worshipped totem, louder with more dancers). All loop-cleaned by
crossfade, eased toward per-frame targets so nothing clicks. The chant is
the mana engine made audible; the rain bed makes the M11 miracle felt.

## 4. Volume

Pause menu row `VOLUME: 80%` — arrows step ±10 %, click cycles. Session
state (no settings file yet by design).

## 5. Tests ([20]) & verdict

Bakery determinism (double-bake checksum), deviceless no-op safety, and
the event seams (grab → Scream, landing → Thud, combine → Clack) — plus
[19]'s cast/explosion/bloom events. Suite at 260 checks, still OK; a real
boot without audio hardware logs "the island stays silent" and plays on.
Follow-up when appetite strikes: .sad overlay music via src/bw (needs the
plan-assets §7.3 research), footsteps, per-tribe chant flavors.
