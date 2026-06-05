#ifndef CABLE_CONFIGS_H
#define CABLE_CONFIGS_H

struct Mapping {
  int endA_mux_start;
  int endB_mux_start;
  int mapA[8];
  int mapB[8];
  int length;
  const char *config_name;
};

extern Mapping configs[];
extern const int NUM_CONFIGS;

#endif
