// The in-game interface's embedded data, images and fonts (see tools/build_ingame_assets.py).
//
// Start() runs once, off the game's main thread: it parses ui.json and decodes every image with
// the Windows Imaging Component, so the main thread only uploads finished pixels. Textures,
// sprites and font assets are made on the main thread the first time they are asked for and
// kept for the whole game (marked DontUnloadUnusedAsset, so scene changes never drop them).
#pragma once
#include "ingame_json.hpp"
#include <string>
#include <string_view>

namespace ep::ingame::assets {

// Starts the background preparation; false if the embedded data is missing or broken.
bool Start(std::string* why);
// True once Start's background work has finished (successfully or not); `ok` says which.
bool Prepared(bool* ok = nullptr);
const json::Value& Data();

// Main thread only. A sprite for an embedded image (2x the layout size). `sliced` sprites get
// the 9-slice border named in ui.json "slices", in the same css pixels as the layout.
void* Sprite(const std::string& file, bool sliced = false);
enum class Face { Body, BodySemi, Serif, Symbol };
// Throws "font not ready: ..." while a face cannot be loaded yet; FontsReady makes all four.
void* Font(Face face);
bool FontsReady(std::string* why);
// For "panel read": each face, its fallbacks, and whether ⚙ is found (alone / with fallbacks).
std::string FontReport();
// Line metrics of a font, per point of size: line height, ascent and descent (descent < 0).
struct Metrics { float line = 1.2f, ascent = 0.95f, descent = -0.25f; };
Metrics FontMetrics(Face face);

// Drops every reference (runtime shutdown; Unity objects are not destroyed here).
void Release();

}  // namespace ep::ingame::assets
