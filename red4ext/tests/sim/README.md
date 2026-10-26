# Driving simulator scripts

Batch runs of the native Linux simulator (`../build_linux_sim.sh` builds
`build-linux/drive_sim`), 24 at a time. Single runs are chaotic (tiny numeric
differences flip a run between a touch and a clean pass), so compare builds
over these batches, on two seeds, against a baseline built from the previous
`Driver.cpp` (set `SIMBIN` to it).

- `score.sh label`: one obstacle every 25 m on both city routes, five kinds (1,080 runs).
- `traffic_score.sh label [N]`: N random city scenes (`SEED`, default 7).
- `hwy_score.sh label [N]`: N random highway scenes (`SEED`, default 3).
- `sim.sh`, `one.sh`: a single drive; `BCAI_TELE=<from s> BCAI_TELE_LEN=<s>` prints the driver's state.
- `battery.sh label [cars]`: all of the above for each car (default, hellhound, maimai) with the game's vehicle list (`LIST=0`: rays only); summary in `battery_<label>.txt`.
- Pedal smoothness: each drive prints `pedals:` (how far the pedals moved per minute, seconds per minute on a hard brake, and quick flips: a pedal held under a second before the other); `pedals.sh results.txt` averages them, and `traffic_score.sh` and `hwy_score.sh` add that to their summary line.
- `BCAI_CAR=truck`: a 12 m, 2.6 m wide truck (slow to speed up and stop, turns wide; its grip is a guess).
- `crossings.sh`: refreshes `crossings_D*.txt`.
- `scene.sh city|hwy SEED LABEL [ENV=..]`: one scene of a random set again on its own (e.g. `scene.sh city 7 T0071 BCAI_TELE=24`), printing the whole drive.
- `same.sh BIN_A BIN_B [N]`: whether two builds drive exactly the same (for refactors and logging): N city and N/2 highway scenes for every car and style, compared by `BCAI_DIGEST` (a hash of every control output). Build both with `OUTBIN=build-linux/<name>/drive_sim EXTRA_FLAGS=-ffp-contract=off ../build_linux_sim.sh`, or the compiler fuses multiply-adds differently in each and every hash differs.

Routes (the navigator, `src/Navigator.cpp`):

- `drive_sim lanegraph.txt nav N [seed]`: N random trips planned by the 0.27 planner (`tests/nav_old.hpp`) and the navigator, compared: length, the planner's time, junctions, left turns, U-turns, how far from the pin each ends, and how often on the wrong level where roads are stacked. `BCAI_NAV_Z=none` gives the pins no height; `BCAI_NAV_SHOW=1` prints every trip; `BCAI_NAV_PATH=k` writes trip k's two routes to `dumps/nav_old.txt` and `dumps/nav_new.txt`.
- `drive_sim lanegraph.txt navone x0 y0 hx hy x1 y1`: one trip, both planners, every leg and turn (`BCAI_GOAL_Z=<m>`: the pin's own height, e.g. a quest pin's from the log).
- `drive_sim lanegraph.txt navwalk N [seed]`: N pins on raised pavements (no road on their level within 25 m): how often the route ends a walk away along the pavements, how long the walks are, and how far from the pin it ends.
- `drive_sim lanegraph.txt navturn N [seed]`: N random trips from the middle of an ordinary road lane, planned on from there and with the route guide's option of turning round first (0.30): how often turning round wins and what it saves.
- `BCAI_GRIP=g`: a style's cornering grip instead of its default. `BCAI_CAR=slippery`: the default car with the old 9 m/s² grip, less than the styles ask for (grip learning). The cars' grip and body slip follow the 0.29 driving tests (about 2 g at 13° for the default car).
- `BCAI_ROUTES=0.27`: Autodrive+ plans with the 0.27 planner. `score.sh` and `traffic_score.sh` set it (`BCAI_ROUTES=` to drop it): their cars are laid out along the old routes (D2's crossings and its multi-lane road), and the navigator takes D2 another way. Highway scenes use the navigator.
- `navdrive.sh N SEED BASEBIN`: drives N random trips (`navtrips.py`) with `SIMBIN` and with a build using another planner, and compares how long they took to arrive (no traffic). A baseline with the 0.27 planner: copy `src` to `build-linux/oldnav/src`, make `DriverRoute.cpp` call `OldNav::Plan` (include `nav_old.hpp`), copy `scan_harness.cpp`, `nav_old.hpp` and `linux/` to `build-linux/oldnav/tests`, and build from there.

`BCAI_STYLE=city|aggressive|madman` drives in that style with its in-game settings (without it: style aggressive with each scene's settings, as before). Set it for a whole battery to judge a style; a baseline built before styles existed still takes the style's settings, which makes it a fair comparison. Each drive also counts *changes of mind*: a sideways move of 1 m or more undone within 3 s (the flip-flopping between lanes); the score scripts report them per drive.

Simulated cars on the route brake for this car when it is ahead of them in their lane (`BCAI_BLIND=1`: they don't). Cars past the route's end leave it, and a car far from this one along the route but close by (where the route passes near itself) is hidden. In `BCAI_CARS`, an eighth field of 1 makes a standing car a traffic car stopped in its lane (listed as not parked). `BCAI_OTHERS_AT=<s>` prints where this car and every other car are at that moment. `BCAI_EVENTS=1` prints the driver's decisions as it makes them (the same lines as `Autodrive+ |` in the game's log): the quickest way to see why a scene went wrong. `BCAI_TRACE=<from s> BCAI_TRACE_LEN=<s>` prints position and controls; a build with `-DBCAI_DEBUG_AVOID` prints the planner's choices from `BCAI_DBG=<s>` (`BCAI_DBG_OPT=1`: every option at whole metres, with `BCAI_DBG_ALL=1` every half metre).

The lane map comes from the game (`BCAI_LANEGRAPH`, default: the dump in the game folder).
Results are written to `build-linux/sim-results/`.
