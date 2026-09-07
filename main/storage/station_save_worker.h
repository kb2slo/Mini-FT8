#pragma once

#include <string>

// Low-priority FATFS write of Station.txt. Serialize on the caller, then submit
// the blob. Last snapshot wins while a write is in flight.

void station_save_worker_init();
void station_save_worker_submit(std::string blob);
void station_save_worker_flush();
