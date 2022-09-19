#pragma once

// This is kept for legacy but should not be used in new releases
// as filename an project name don't match (esp32FOTA vs esp32fota)

#ifdef __cplusplus

  #include "esp32FOTA.hpp"

#else

  #error M5Unified requires a C++ compiler, please change file extension to .cc or .cpp

#endif
