enum class SkipGroup {
  None,
  River,
  Heaters,
  Charge
};

struct ScenarioStep {
  const char* name;
  bool (*executeFunction)(uint32_t);
  uint32_t delayMs;
  SkipGroup skipGroup;
};