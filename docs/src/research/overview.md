# Research notes

{{#include ../../RESEARCH.md:intro}}

In short, NPC reactions are script logic and are fixed directly. Autodrive steering is native code, so the mod tunes the TweakDB driving parameters it reads, and from plugin 0.14 drives the car itself with Autodrive+. It cannot add new behaviours to the game's own drivers, such as forcing green lights.

The pages that follow are the notes as they were written, version by version: what was found in the game, what was tried, and what the simulator and the in-game tests measured.
