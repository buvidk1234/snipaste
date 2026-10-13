#pragma once

namespace snipaste::app {

void initializeLogging();
// 0 = warnings/errors only, 1 = normal information, 2 = verbose/debug.
void setLoggingLevel(int level);

} // namespace snipaste::app
