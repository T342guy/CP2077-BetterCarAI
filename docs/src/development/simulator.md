# The driving simulator

`red4ext/tests/build_linux_sim.sh` builds `red4ext/build-linux/drive_sim`: the plugin's driver, navigator and lane map, built natively for Linux, driving a simple car model over the city's real lane map (dumped from the game with *Dump all lanes in the city*). A 2.5 km city drive takes about 0.3 s, so changes are judged over hundreds of drives, 24 at a time, against a baseline build of the previous version.

{{#include ../../../red4ext/tests/sim/README.md}}
