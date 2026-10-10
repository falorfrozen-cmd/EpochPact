#pragma once
#include <string>
namespace ep::cof {
bool Init();
struct TuningContext { void* actor; void* faction; void* slots; bool member; };
TuningContext CurrentForTuning(); // Called on the game thread; retains normal offline/identity checks.
std::string Read();
std::string Join(const std::string& id, bool switchFromMerchant);
std::string Rank(const std::string& id, int rank);
std::string Favor(const std::string& id, int favor);
std::string Reputation(const std::string& id, int amount);
std::string UnlockLenses(const std::string& id);
std::string Configure(const std::string& id, int slot, int reward, int lens, bool preview);
std::string Charges(const std::string& id, int slot, int value);
std::string Multiplier(double value);
std::string ReputationMultiplier(double value);
std::string Status();
#ifdef EPOCHPACT_RESEARCH
std::string TestGain(const std::string& id, int amount, bool includeReputation = false);
std::string TestReputation(const std::string& id, int amount, bool otherFaction = false);
std::string TestSpend(const std::string& id, int amount);
std::string TestCharge(const std::string& id, int slot, int amount);
std::string TestReward(const std::string& id, int slot, int target);
std::string TestLoot(const std::string& id, const std::string& kind, int count);
std::string TestGround(const std::string& id);
#endif
}
