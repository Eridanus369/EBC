#include "KX_FunctionNode.h"
#include "KX_ExecutionContext.h"

KX_RaycastResult KX_ExecutionContext::Raycast(const float[3], const float[3], float)
{
  return KX_RaycastResult();
}

void RegisterB6NetworkFunctionGameNodes() {}
void RegisterB6SensorFunctionGameNodes() {}
void RegisterB6SceneFunctionGameNodes() {}
void RegisterB6UIFunctionGameNodes() {}
void RegisterB6FileFunctionGameNodes() {}
