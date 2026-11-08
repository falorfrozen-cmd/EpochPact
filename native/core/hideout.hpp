#pragma once
// The hideout: what stands in the place of our own, kept in a file, changed by the player. monolith.cpp owns the ground and the doors.
#include <string>
#include <vector>

namespace ep::hideout {
struct DoorSpot { int action; float right, up; bool hasLook; float yaw=0,size=1,lift=0; };
struct GroundChoice { int shape; float size, period; bool natural; float light; float u0, u1, v0, v1; };
GroundChoice Ground();
// The ground's material now: of this scene, or the plain sand until a floor of another map has loaded.
void* GroundMaterial();
void* CliffMaterial();
// The doors to build now, in list order (not the ones the player took away).
std::vector<DoorSpot> Doors();
struct StationBounds { float x,y,z,width,height,depth; };
bool GetStationBounds(int action,StationBounds& bounds);
// Everything but the doors' rocks goes up under this object; prefabs of other maps follow as they load.
void Populate(void* holder);
// The place is gone: nothing of it is held any more.
void Clear();
bool Editing();
bool PlayerInside();
int Held();
bool CanRemoveHeld();
bool CanUndo();
bool CanRedo();
unsigned long long HistoryRevision();
int SelectionCount();
int SelectionAnchor();
bool SelectionLocked();
bool SelectionPending();
bool SelectionDuplicable();
float GridStep();
float AngleStep();
bool FloorsSelectable();
std::string HeldName();
float HeldRotation();
bool PlacementValid();
std::string PlacementIssue();
void BlockEditorPointer(bool blocked,bool keyboard=false);
// Read-only model previews. Request is bounded and rendered on the main thread;
// it never creates layout items or writes the player's hideout file.
void RequestPreview(const std::string& key,bool priority=false);
void* PreviewTexture(const std::string& key);
std::string PreviewStatus(const std::string& key);
void ReleasePreviews();
void ReleaseRoots(); // GC handles only, before IL2CPP shutdown
void Frame();  // main thread, each frame
// hideout read|search|add|at|move|here|turn|size|lift|remove|show|hold|edit|go|reset
std::string Command(const std::vector<std::string>& args);
}  // namespace ep::hideout
