#ifndef HALYARD_SEQUENCES_H
#define HALYARD_SEQUENCES_H

#include "FSMController.h"

class HalyardSequences {
public:
  static void buildLoweringSequence(FSMController& fsm);
  static void buildTestSequence(FSMController& fsm);
};

#endif
