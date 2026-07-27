# Sound files

Audio files go here. Nothing in this folder is required for the game to run: a
sound whose file is missing is simply silent, and the editor's toolbar shows a
`snd?` badge listing which ones are absent.

The names the game looks for are defined in `assets/scripts/sounds.lua`, not
here — that file is the place to change a volume, a pitch range, or a filename.

Currently expected:

| File | Used for |
|---|---|
| `shot.wav` | the player's gun firing |
| `impact.wav` | a bullet striking an enemy |
| `explosion.wav` | anything being destroyed |
| `hit_taken.wav` | the player being hit |
| `engine.ogg` | the jet's engine, looping; pitch follows the throttle |

**Formats:** raylib reads `.wav`, `.ogg`, `.mp3` and `.flac`. Short effects are
best as `.wav` (no decoding cost per play); the looping engine note is better as
`.ogg`, which is far smaller for a long sample.

**The engine loop must be seamless** — a sample whose start and end do not match
will click audibly every few seconds.

Free sources that suit this project: [kenney.nl](https://kenney.nl/assets) (CC0
game audio), [freesound.org](https://freesound.org) (check each licence), and the
annual Sonniss GDC audio bundles.
