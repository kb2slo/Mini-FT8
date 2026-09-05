#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "esp_err.h"

enum class StorageOwner : uint8_t {
    UNAVAILABLE,
    FIRMWARE,
    USB_HOST,
    TRANSITION,
};

enum class StorageOpenMode : uint8_t {
    READ,
    WRITE_TRUNCATE,
    APPEND,
};

enum class StorageCopyStatus : uint8_t {
    OK,
    STORAGE_BUSY,
    RADIO_BUSY,
    SD_MOUNT_FAILED,
    LIST_FAILED,
    COPY_FAILED,
};

struct StorageStream;

struct StorageCopyResult {
    esp_err_t err = ESP_OK;
    StorageCopyStatus status = StorageCopyStatus::OK;
    int copied_count = 0;
    int missed_count = 0;
    std::vector<std::string> missed_files;
};

esp_err_t storage_service_init();
StorageOwner storage_service_owner();
bool storage_service_firmware_available();
bool storage_service_usb_drive_enabled();
size_t storage_service_open_stream_count();
esp_err_t storage_service_set_usb_drive_enabled(bool enabled);
bool storage_service_usb_host_attached();

bool storage_file_exists(const std::string& name);
bool storage_file_remove(const std::string& name);
bool storage_file_list(std::vector<std::string>& files);
bool storage_file_read_text(const std::string& name, std::string& content);
bool storage_file_write_atomic(const std::string& name, const std::string& content);
bool storage_file_append(const std::string& name,
                         const std::string& content,
                         const std::string& header_if_new = "",
                         bool sync_to_flash = false);
bool storage_file_append_cabrillo(const std::string& mycall,
                                  const std::string& location,
                                  const std::string& qso_line);

StorageStream* storage_stream_open(const std::string& name, StorageOpenMode mode);
size_t storage_stream_read(StorageStream* stream, void* data, size_t size);
bool storage_stream_read_line(StorageStream* stream, char* line, size_t line_size);
size_t storage_stream_write(StorageStream* stream, const void* data, size_t size);
bool storage_stream_seek(StorageStream* stream, long offset, int whence);
long storage_stream_tell(StorageStream* stream);
long storage_stream_size(StorageStream* stream);
bool storage_stream_sync(StorageStream* stream);
void storage_stream_close(StorageStream* stream);

bool storage_sync_station_from_sd();
bool storage_service_flush_all();
StorageCopyResult storage_copy_all_to_sd(const std::string& priority_file,
                                         const std::string& priority_file2 = "");
