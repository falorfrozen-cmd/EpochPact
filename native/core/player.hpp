// What the player's own body reads: how fast it moves and how fast abilities come back.
//
//   speed <1-5>       RPGCharacterController.walkSpeed/moveSpeed/runSpeed scaled while
//                     UpdateMovement() turns input into velocity (the fields the rigidbody
//                     movement actually reads, not the NavMeshAgent copy)
//   cooldown <1-10>   PlayerChargeManager.OnUpdateTick's deltaTime scaled: the player's
//                     charges and cooldowns count down that much faster, and
//                     ChargeManager.getCooldown divides the length of new cooldowns
#pragma once

#include <string>

namespace ep::player {

constexpr double kMaxSpeed = 5.0;

bool Init();
std::string SetSpeed(double m);
std::string SetCooldown(double m);
std::string Status();

// The current movement-stat modifier, for the live check.
std::string SpeedProbe();

// Every Movespeed entry in the player's stat list, for the live check. Main thread only.
std::string StatProbe();

// The generic stat command: `stat` lists the supported names, `stat <name>` reads the
// player's entry for it, `stat <name> <value>` writes it (creating the entry if needed).
std::string StatList();
std::string ReadStat(const std::string& name);
std::string SetStat(const std::string& name, double value);

// Dumps every entry whose SP is `sp`, whatever its tags (research). Main thread only.
std::string StatScan(int sp);

}  // namespace ep::player
