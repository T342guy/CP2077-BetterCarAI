# Better Car AI (Cyberpunk 2077 v2.5)

Documentation and research notes: https://t342guy.github.io/CP2077-BetterCarAI/ (built from `docs/` with mdBook).

<!-- ANCHOR: features -->
- **BCAI Autodrive+** (needs the native plugin): the mod's own driving AI, in three driving styles you pick when it starts: **City** (by the rules), **Aggressive** (fast and hard) and **Madman** (as fast as it can without crashing). See below.
- **Better routes to the map destination** (needs the native plugin): the mod plans its own route to your map pin or tracked quest, for time rather than distance (highways, fewer junctions, fewer left turns across traffic, no needless U-turns), and ends it on the pin's level: a pin under a highway or next to a bridge gets a route to the street, not onto the road above it, and a pin up on a walkway (a quest high in a megabuilding) gets a route to the road you can walk up to it from. It draws the route on the road ahead with the next turn at the top of the screen, and plans again when you leave it. Autodrive+ drives the same planner's route. See *Navigation* below.
- **Calm traffic:** a light tap no longer makes NPC drivers flee, bail out, get knocked out or start a fight, and people on the pavement no longer run from a fender-bender next to them. A tapped driver honks (with a voice line) and keeps driving; tapped again soon after, it honks and flashes its lights; keep tapping the same car (4 taps within 30 s by default) and the driver loses patience and reacts the game's way (drives off in a panic, or gets out). Hard crashes, gunfire, drawn weapons, combat and the police still get the vanilla reactions.
- **Calmer panic driving:** NPCs fleeing a crash or gunfire drive a little slower and look further ahead for bends, so they wreck less.
- **Smoother autodrive:** a longer look-ahead and slightly more confident cornering mean less stop-start jerkiness.
- **Aggressive tuning for the game's autodrive** (in *Older drivers & tuning*, or a hotkey): raises autodrive's speed cap, asks traffic ahead to clear the way, looks about 3× further ahead for obstacles, ignores speed limits, swerves around traffic faster and gets more power and grip. It is built on the car's normal autodrive tuning. Basing it on the race-AI params is an experimental option, because they made the car rev and brake constantly in testing.
- **AI drive** (the default for autodrive): native autodrive holds about 86 km/h no matter what, so when you start autodrive the mod turns it off and drives the car with the AI command police and quest drivers use. That command honours the aggressive max speed and traffic clearing. It drives to the autodrive destination, or straight down the road if none is set, and brakes to a stop on arrival. Steer, accelerate or brake to take back control. Turn off *The game's autodrive starts AI drive* (in *Older drivers & tuning*) to get vanilla autodrive back; Delamain cab rides are never taken over. It also has its own hotkey.
- **Stability governor** (AI drive): slows the car while it slides sideways, weaves after an overcorrection, is airborne, or is about to crest a rise fast enough to take off, and keeps a 2 s gap behind a car straight ahead so a dodge happens at a survivable speed. It also learns **trouble spots**: places where AI drive snapped the wheel or took a bend far too fast with no car around (usually a kink in the game's lane path at a junction). It saves them to `trouble_spots.json` and slows to a set speed (default 60 km/h) before them next time. The *Older drivers & tuning* window's *AI drive* tab shows how many it has learned and has a button to forget them all.
- **NPC traffic tuning:** AI-driven NPC cars get more confident cornering and a longer look-ahead.
<!-- ANCHOR_END: features -->

<!-- ANCHOR: install -->
## Requirements
- Cyber Engine Tweaks
- redscript (+ RED4ext)
- TweakXL is **not** required. TweakDB changes are applied at runtime through CET.

## Install
```bash
./install.sh                     # default game path
./install.sh "/path/to/Cyberpunk 2077"
./install.sh --uninstall
```
Or copy `r6/` and `bin/` into the game folder.
<!-- ANCHOR_END: install -->

## Use
<!-- ANCHOR: use -->
Open the CET overlay. The **Better Car AI** window has:
- **Autodrive+:** whether it is driving (speed, what it is doing, the style) and a *Start/Stop* button.
- **Driving style:** City, Aggressive or Madman, with what each does. Each style keeps its own settings (top speed, speed vs the speed limit, cornering grip, braking, following distance) under *City settings* (and so on); they apply at once, even while it drives.
- *Ask which style each time it starts* (on by default), *The game's autodrive button starts Autodrive+*, and *Show the game's autodrive HUD while it drives* (on by default: the wheel icon on the minimap and the side panel with the distance and time left, as for the game's own autodrive).
- **Navigation:** your own route to the map destination: on or off, drawn on the road, the next turn, and the route it has now.
- **NPC traffic:** calm traffic (and the impact speed that still counts as a crash), honking, people nearby staying calm, how many taps before a driver loses patience, faster traffic, calmer panic driving.
- **Overlay:** show it, how much its panel shows, and which layers it draws.
- *Older drivers & tuning* opens a window for the game's own autodrive (normal/aggressive tuning), NPC traffic tuning, AI drive and motorcycles. *Debug tools* opens the lane map, recorder, vehicle, lanes & routes, Autodrive+ internals and research tools.

Most changes apply when the overlay closes (or with *Apply now*); the style settings apply at once. Everything is saved to `settings.json`.

**Style picker:** when Autodrive+ starts (button, hotkey or the game's autodrive button), a picker shows the three styles for 6 s. Steer left/right to choose, then press the autodrive button (G) again, or accelerate, to start with the highlighted style (it starts once you let go of the controls); brake to cancel. When the time runs out it starts with the highlighted style. With the CET overlay open you can click a style instead. Turn the picker off to start straight away with the chosen style.

**Hotkeys** (CET > Bindings > Better Car AI): *Autodrive+: start / stop*, *next driving style*, one for each style (they also switch styles while it drives), *Overlay: show / hide*, *Overlay: compact / normal / everything*, the lane map, saving a recording, and the older drivers' toggles.
<!-- ANCHOR_END: use -->

<!-- ANCHOR: overlay -->
**Visual debug overlay:** bind *Overlay: show / hide* (or tick it in the main window). It draws, over the game (each layer can be switched off):
- Autodrive+'s planned line, coloured by the speed it plans there against the speed now (blue: as fast; yellow, orange, red: slowing), with the slowest point ahead labelled; the road's edges (grey); what its sensor sees (orange cars, red walls) and what is in its way (magenta, labelled with its distance and speed). Without Autodrive+, the game's route in blue (with the plugin);
- the destination (a tall magenta pillar, with a line to it);
- the line the car actually drove, by slip angle (the body against the way it goes): green under 8°, yellow 8–17° (cornering hard: the game's cars grip best there), red past 17° (sliding), magenta while airborne;
- where its current speed and turn rate carry it in the next 2 s (cyan), plus its heading (white) and actual travel direction (cyan);
- a fan of look-ahead rays: orange stops at a car, red at a wall or railing, yellow at road rising ahead, grey is clear.

Its panel (compact, normal or everything) shows the speed, and while Autodrive+ drives: its target and the road's speed, what it is doing (cruising, following, overtaking, going round, dropping back, waiting at a junction...) and what is holding its speed down, what is ahead, which lane it is in and heading for (counted from the right), the next slow-down, the distance to the destination, its throttle, brake and steering, and its latest decisions (three, or six with *everything*) with how long ago each was. It also shows what is under the car: road, dirt, grass (with the game's name for the surface), or *airborne* only when the game says the car is in the air. *Everything* adds the drive's statistics (metres off the road, replans, lane changes, changes of mind), the grip it corners at (a new car starts at 1 g and builds up to the style's while it takes bends cleanly; less where it slid), the car's own figures from the game's records (mass, size, tyre friction front and rear, how hard its brakes could stop it, engine torque, steering lock) and a colour legend. The world measurements refresh 30 times a second, the panel every frame.

**Drive recorder:** while you're in a vehicle, the mod keeps the last 60 s of driving (10 samples a second): position, heading, velocity, speed, sideways slip, turn rate, ground contact, the look-ahead rays, the AI command's speed caps, the stability governor, RPM, gear, suspension forces and collisions. When AI drive spins out, crashes above 36 km/h or stops dead (hard braking and your own driving after you take over don't count), it saves the recording 5 s later as `rec_<date>_<time>.csv` in the CET mod folder. Bind *Save drive recording* (or use the button in the debug window's *Recorder* tab) to save one by hand. `tools/analyze_recording.py rec_....csv --png out.png` explains in plain words what happened around the wipe-out, lists every steering snap (the driver yanking the wheel at speed) with what was ahead, prints a second-by-second table, and with `--png` draws a map of the incident plus speed, turn-rate-vs-grip and slip-angle graphs with snaps marked (needs Pillow). The AI's actual steering input isn't readable from scripts, so steering is estimated from the turn rate.
<!-- ANCHOR_END: overlay -->

<!-- ANCHOR: autodrive -->
**BCAI Autodrive+ (experimental, needs the native plugin):** the mod's own driving AI, replacing the game's. It plans its route over the city's lane map, to the map destination or along the road without one. It works the car's throttle, brake and wheel itself, so the car still drives on the game's real physics.
- **Flowing line:** it doesn't trace the lanes exactly. It smooths them the way a driver would: rounding sharp junction corners and drifting across lanes gently, but held towards its lane and kept away from the road's edges. Off junctions it stays on its own side of the road.
- **Sensing:** rays find cars and walls on the road ahead, in the lanes beside and behind it (for changing lanes), right beside it (the blind spot), and along the roads across the next junction. Rays tilt with the car on slopes and stay above the road over crests. It groups a car's hits into one outline, remembers the whole outline of a parked car, and measures how fast each car moves along the road and across it.
- **People on foot** (0.31): it asks the game which people are nearby too (the rays don't see them) and works out where each is walking. It stops for someone crossing or stepping into the road in front of it, waits until they are past, and goes round someone standing in the road (after as long as it waits behind a stopped car, at about 15 km/h, keeping half a metre more room). People walking along the pavement don't slow it. *City* also slows for a crosswalk with someone by it, to a speed it can stop from (it doesn't wait there: people stand at crosswalks for the lights). In the simulator, on 300 drives with people crossing (at crosswalks and mid-block, stepping off 25-90 m ahead of it), walking along the pavement and standing at the kerb, the people it could have stopped for and hit anyway fell from 119 to 1 (default car), 139 to 5 (Hellhound), 110 to 0 (Maimai) and 151 to 9 (12 m truck). The other hits were people stepping out too close in front of it to stop, or (the truck) walking into its side. Waiting for people cost 5-7% of the time; people on the pavement cost nothing (City: under 1%, for the crosswalks). Switch it off with *Watch for people on foot* in the main window.
- **Vehicles from the game:** it also asks the game which vehicles are nearby (where each is, how it moves, whether it is parked), so it knows a car's speed the moment it sees it. The rays then only fill in each car's size, and still find everything the game doesn't list.
- **Routes:** the navigator's (see *Navigation*): the quickest way, ending on the pin's level, at a lane it can reach.
- **Fits the car:** it measures the car's length and width when it starts, and learns its grip, braking, acceleration and turning as it drives, so a wide, heavy car and a tiny slow one are each driven within what they can do. Grip: a car new to it corners at up to 1 g, building up to the style's grip while bends hold its speed down and it takes them cleanly; where it slides it drops to what the car held there. Sliding means slipping well past what this car's cornering usually needs (it learns that too): the game's cars corner hardest with the body 8–15° into the bend and the tyres squealing, which isn't sliding. It also learns the tightest the car really turns, and slows for bends tighter than that at speed.
- **Getting round things:** it plans each sideways move by driving it ahead in time against where every car will be, at its current speed, speeding up and slowing down. It overtakes slower cars in another lane going its way when there is room behind and beside, and checks behind before moving over (no cutting in front of a faster car). It uses the oncoming lane to get past something parked, or a car stopped in its lane for a while (City 20 s, Aggressive 5 s, Madman 1.5 s; not one queueing behind another car or just before a junction), only when nothing coming the other way would get there first, giving up and dropping back if something does.
- **Driving styles:** they corner at up to 0.7 g (City), 1.3 g (Aggressive) and 1.7 g (Madman), never more than the car holds; the game's sports cars hold about 2 g. *City* keeps to about 1.3× the roads' traffic speed (top 110 km/h), keeps right on any road with more than one lane going its way, overtakes only what stands still (or a car crawling at under a third of its speed), and leaves a 2 s gap. *Aggressive* (2.2× traffic speed, top 180 km/h) keeps right on roads with three lanes or more, and also overtakes slow cars on the other side of the road when it can see that nothing is coming for as far as the pass takes and no roads cross on the way. *Madman* (3× traffic speed, top 250 km/h, 0.8 s gap) keeps whatever lane is clearest, overtakes whenever nothing is coming, takes junctions faster and only slows for cars it sees crossing. In every style it waits a moment after a lane change before the next, and moves into a lane only once that lane has looked clear for a moment, so it doesn't flip back and forth. It passes parked cars at no more than about 30 km/h, and doesn't swing out round moving traffic in junctions. It gives way to cars crossing, and approaches a junction with cars waiting across it at a speed it can stop from. Boxed in behind something with a gap beside it, it backs up a little to get round; in a jam it waits. In an emergency it swerves harder, even into the oncoming lane.
- **Learning the car:** the game puts curves on the controls (a little steering barely turns the car, a little brake already brakes hard), so Autodrive+ learns how the car you're in answers the wheel and pedals while it drives. Its first minute in a new car can be a little less smooth.

Start it from the main window or with the *Autodrive+: start / stop* hotkey; steering or the pedals take over, and so does the autodrive button. With the game's autodrive HUD shown, the game switching its autodrive off (combat, a drawn weapon, heavy damage, a quest) hands control back too, as it would for its own autodrive.

**Pedals:** it eases onto the throttle (full in about 0.7 s) and leaves a little slowing to coasting before it touches the brake; only a real stop brakes hard at once. Following a car, it keeps the time gap with one steady rule, so it no longer brakes hard and floors it in turn behind a car a little slower than it.

**Lane changes** for the route (to turn off, or where a lane ends) start about 9 s ahead at any speed (150 m at least), and earlier still with more than one lane to cross.

**Large vehicles:** it measures the car's length with rays from 6 m out, then 15, 27 and 42 m when those find nothing (a truck's body starts further out), at heights up to windscreen level, and never falls back to a point inside the body. (0.31.1) (The game's records don't give a vehicle's size: they say 3 × 1 m for cars 5.5 m long.) A vehicle over 7 m long also slows for bends it can't steer round any faster (a long truck turns wide); cars turn far tighter than that, so they don't. (0.31.1: the steering look-ahead and the grip guard now account for the vehicle's turning response and length, so a heavy, slow-steering truck starts its turn earlier instead of swinging lock to lock.) (0.31.2: the steering response is learned at the 5-17° / up to 2 g cornering the driving tests called gripping, not only below 3°, so heavy vehicles no longer have the speed plan floor at 10.8 km/h at every junction; a truck-length kink guard and a fixed plan-gradient reference follow.) **Wide turns** (0.31): a vehicle over 7 m long knows that in a tight turn its front and rear stick out past the outside of the bend (a 12 m truck turning on 10 m: almost 2 m, over the oncoming lanes in a right turn). It keeps that swing clear of other cars, and before a turn that swings its ends into the oncoming lanes it waits while anything coming the other way would get there while it is in the turn. In the simulator a 12 m truck now touches other cars in 35 and 25 of 400 city drives (0.30: 67 and 61), and oncoming cars in 5 and 3 (0.30: 35 and 31; the ones left are mostly simulated cars that don't brake driving into the truck while it stands waiting). Where the only way past a parked car would touch it, it now stays put instead (3 more drives in 800 didn't arrive). Cars drive exactly as before. (The Hellhound, 5.8 m, is under the limit and still clips junction edges now and then.)

It doesn't stop at traffic lights yet, in any style.

The debug window's *Vehicle* tab shows the live autodrive params for the car you're in, next to the vanilla autodrive, generic and race params. *Write to log* copies them into `BetterCarAI.log`.
<!-- ANCHOR_END: autodrive -->

## Navigation
<!-- ANCHOR: navigation -->
The game's GPS goes for the shortest way, and where roads are stacked it can pick the wrong one: a pin under a highway (or a bridge, or a raised road) can get a route up onto it. (Which road the game picks for a pin, and why, isn't confirmed yet: the log's `Route |` lines record where the pin is and where the game put it.) With the native plugin (0.28+) the mod plans its own route over the game's lane map:
- **For time, not distance:** each road at the pace it is driven (highways faster, junction lanes slower), a few seconds for each junction, more for turning left across traffic, a lot for a U-turn.
- **Ending at the right place:** it looks at every road within 120 m of the pin and weighs how far each is from the pin, whether it is on the pin's level (a road more than a couple of metres above the pin, or more than 4 m below it, is another level) and how long it takes to get there. Highways count against stopping, unless the pin is on one. A pin without a real height (far above or below every road) counts as on the lowest road right by it, where the street is.
- **A pin up on a walkway** (a quest high in a megabuilding, a bridge's footpath), with no road on its level: it walks the game's pavement network from the pin (up to 600 m) and ends at the road where that walk is shortest, not at the street right below, which may have no way up. The turn hint then says how far the walk is.
- **On screen:** a yellow ribbon on the road ahead (300 m), starting a few metres in front of the car, thinner and fainter further off, with a chevron every 24 m; and at the top of the screen the next turn (*Turn left in 240 m*, *Keep right*, *Make a U-turn*) with the distance and time left. It shows while you drive (not on foot, in menus, or while the debug overlay draws Autodrive+'s own path). Leave the route by more than 25 m for a moment and it plans a new one; on foot it plans again once you have walked 80 m. Both can be switched off in the main window.
- **Turning round:** while you drive (not Autodrive+, and not in a driving test), it also plans from the lane going the other way beside you (within 12 m, not across a highway) and tells you to *Turn round* when that is quicker by more than the 15 s it allows for turning, instead of sending you on to the next place to turn. On 460 random trips from the middle of a road, it chose to turn round in 145, saving 66 s (830 m) on average there.
- **A dead-end start:** if the lane under the car (or the nearest one, on foot) leads nowhere, it starts from the next nearest instead.
- **Autodrive+** plans with the same navigator, so it drives the route you see.

On 1,500 random trips across the dumped city, against the planner of 0.27 (shortest distance to the lane nearest the pin): the simulator's car got there 8-12% sooner, through 15% fewer junctions and a third fewer left turns, and where roads are stacked over the destination none of its routes ended on the wrong level (the old one: about 1 in 5).

The minimap and the world map still draw the game's own route: the GPS is native code. The debug window's *Research* tab has a scan that looks for where the GPS keeps it (set a map pin first), the first step to drawing our route there too.
<!-- ANCHOR_END: navigation -->

## Driving test
<!-- ANCHOR: drivetest -->
*Debug tools > Driving test* compares your driving with Autodrive+'s on the same route. Put a pin on the map past the bends you want to compare, stop on an ordinary road and press *New test to my map pin* (the game's GPS shows the way). Or press *Random route*: it picks a destination 1-2 km away (or 2-4 km), marks it on the map and shows the route with the yellow line (the minimap's GPS still leads to your own pin, if you have one: follow the yellow line). Drive it; recording starts as you drive off and stops when you arrive. Leaving the route is counted (the drives may then take different roads). Then pick a style and press *Let Autodrive+ drive it*: the car is put back at the start and Autodrive+ drives the same route. Each drive is saved, 20 rows a second (position, heading, velocity, speed, the controls, how far along and off the route, whether the game calls the car skidding), as `test_<id>_player.csv` and `test_<id>_ai_<style>.csv`, with the route in `test_<id>_route.txt`, in the CET mod folder.

`tools/drive_test.py <mod folder>` compares the newest test's drives: the time, average and top speed, the hardest braking and cornering, slip, how long each slid and how much of it was off the route; then the sharpest bends one by one where both drives were on the route (the speed going in, the slowest point, the hardest cornering, the time through, the most slip); then cornering against slip angle, drive by drive. Last, the grip you used without sliding, which is about what the car can really hold. The first tests (0.29, three sports cars) measured about 2 g at 8-15° of slip, falling off past 20°: that is where the styles' cornering grip comes from.
<!-- ANCHOR_END: drivetest -->

## How it works / limits
See [docs/RESEARCH.md](docs/RESEARCH.md), or the [documentation site](https://t342guy.github.io/CP2077-BetterCarAI/). In short, NPC reactions are script logic and are fixed directly. Autodrive steering is native code, so the mod tunes the TweakDB driving parameters it reads. It cannot add new behaviours such as forcing green lights.

## The log
<!-- ANCHOR: log -->
The CET log is at `bin/x64/plugins/cyber_engine_tweaks/mods/BetterCarAI/BetterCarAI.log`. What goes in it is set in the debug window's *Recorder & log* tab:
- `Autodrive+ | ...`: every decision Autodrive+ makes, with seconds into the drive, speed, metres along the route and why: lane changes and swerves (from which lane to which, why, what was in the way, how the move scored), passing (begun, waiting for oncoming traffic, done, given up), what slows it down (following, stopping behind, a junction, going round, losing grip), hard braking, leaving the mapped road, backing up, replanning, the style and settings, and how the drive ended.
- `Autodrive+ raw: ...`: twice a second, the raw numbers (speeds, what is ahead, the line, steering, pedals, what it has learned about the car).
- `Traffic | ...`: what NPC drivers did about a bump (honk, honk and flash, lost patience), crashes, and when people near a lightly dented car stayed calm.
- `Route | ...`: each route planned to the map destination and why (a new destination, off the route, got in a car): its length, time, junctions and turns, how far from the pin it ends and on which level, and for a new destination where the pin is and where the game's autodrive put it.

`tools/drive_log.py BetterCarAI.log` sums each drive up (style, route, how it ended, its decisions counted by reason); `--events` lists every decision, `--traffic` every NPC reaction, `--routes` every route.

Lines starting with `flat not found` mean that a parameter name differs in your game version.
<!-- ANCHOR_END: log -->

## Native plugin (RED4ext, optional, in progress)

<!-- ANCHOR: native -->
`red4ext/` is a RED4ext plugin that gives the mod deeper access to the game's native vehicle AI. The mod works without it. The plugin ships its own script declarations (`BetterCarAINative.reds`), which are compiled in only while it's loaded.

It adds debugging tools to the debug window: the dumps are in the *Lanes & routes* and *Research* tabs, the lane map in *Overlay*.
- **Dump vehicle AI types:** lists the engine's types for vehicles, traffic, autopilot, driving, steering and lanes, with their fields and functions.
- **Dump car, AI component and drive command:** writes the live state of each of those objects, including fields scripts can't see.
- **Lane map: lanes within 60 m of the car:** reads the game's traffic lane table (every lane in the city) and lists the nearby lanes: their outline, kind (road, junction, sidewalk, no-autodrive...), speed limit and which lanes they lead into.
- **Lane map overlay** (*Overlay* tab, or the *Toggle lane map* hotkey): with the visual overlay on, draws every lane within 70 m with an arrow for its driving direction. Colours: pale blue road lanes, amber junction lanes, purple GPS-only lanes, red no-autodrive lanes, green lanes the plugin repaired, grey sidewalks (optional). Lanes near the car are numbered, and a red *dead end* marks driving lanes that lead nowhere.
- **Dump all lanes in the city:** writes the whole traffic lane map to `dumps/lanegraph.txt`, for finding broken junctions offline.
- **Dump the AI's route** (also a hotkey): writes the route the car is following (every lane from start to destination) to a file. With the plugin loaded, the visual debug overlay also draws the next 150 m of that route in blue.
- **Scan for the AI's planned path:** has a hotkey too. Run it while AI drive is running. It follows pointers from the car, its AI component, the drive command and the autodrive/traffic systems. It looks for arrays of points along the road ahead and for chains of linked lanes. The goal is to find where the native driver keeps its route.

With the plugin loaded, the mod also **repairs broken junctions** in the game's lane map (on by default, under AI drive). At 11 junctions, all on roads where the game has normal traffic turned off, the turn lanes are attached to the wrong side of the road. Any route through them makes the car swerve one lane sideways and back at the junction mouth, which is what spun AI drive at a T-junction. Those lanes are also missing their links to the road, so the route planner treats the road as a dead end there and routes around it. The plugin reconnects the lanes to the correct side, redraws them as smooth curves, and adds the missing links when the game loads. Only those junction lanes are changed, nothing is saved to disk, and switching the setting off undoes it.
<!-- ANCHOR_END: native -->

![T-junction lanes before and after the repair](docs/src/images/tjunction_repair.png)

<!-- ANCHOR: native2 -->
AI drive also gets a **route speed planner** (AI drive section, on by default). It reads the route the AI is about to drive from the game's lane map, finds the bends and junction jogs in the next 400 m, and slows down early enough to take each one at the set grip (default 0.8 g). It needs the stability governor on. Unlike trouble spots, it works at junctions the car has never driven.

Output goes to `red4ext/plugins/BetterCarAI/dumps/`.
<!-- ANCHOR_END: native2 -->

<!-- ANCHOR: build -->
Build on Linux by cross-compiling with clang-cl and the MSVC runtime/Windows SDK from [xwin](https://github.com/Jake-Shadle/xwin):
```
git submodule update --init
xwin --accept-license splat --output ~/.local/share/xwin/sdk   # accepts Microsoft's license for the SDK files
./red4ext/build.sh        # needs clang-cl, lld-link, cmake, ninja (e.g. in a toolbox)
./install.sh              # also installs the plugin if red4ext/build/BetterCarAI.dll exists
```
<!-- ANCHOR_END: build -->

