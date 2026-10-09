// Benign isolated dependency fixture. Never distribute or install in a real game.
#ifndef PROBE_VALUE
#define PROBE_VALUE 1
#endif
extern "C" __declspec(dllexport) int ProbeValue() { return PROBE_VALUE; }
