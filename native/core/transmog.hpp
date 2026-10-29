// Item looks (transmog, offline play only): an equipped item can look like another item of the same type, with
// the game's own art for that base item or Unique. Shop cosmetics are not touched.
//
//   transmogread                    the loaded character's visible slots: the equipped item, its look and the looks
//                                   that fit (base items and Uniques of the same type)
//   transmog <slot> <look|none>     slot: helmet, body, gloves, boots, belt, weapon, offhand; look: b<type>.<sub> or
//                                   u<unique id> of the equipped item's type; none = the item's own look
//   transmogpreview <slot> <look|none>  the Wardrobe's try-on: worn at once, never stored; it ends by itself when the
//                                   Wardrobe stops keeping it alive (KeepPreview), or with transmogpreview clear all
//
// The character's visuals come from PlayerActorSync.ReceiveEquipGear (armour) and ReceiveEquipWeapon (weapons and
// off-hands); a look replaces their subtype and Unique there, so it stays through zone changes and new items of the
// same type. Looks are kept per character in <game>/EpochPact/transmog.txt.
#pragma once

#include <string>

namespace ep::transmog {

bool Init();
std::string Read();
std::string Set(const std::string& slot, const std::string& look);
std::string Preview(const std::string& slot, const std::string& look);
void KeepPreview();  // the Wardrobe is on screen (main thread, every frame): its previews stay
void Frame();        // convenience::Frame: a preview no longer kept alive ends
std::string Status();

}  // namespace ep::transmog
