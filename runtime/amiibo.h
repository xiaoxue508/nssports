#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AMIIBO_DUMP_SIZE 540

typedef enum {
    AMIIBO_DEV_STATE_INITIALIZED = 0,
    AMIIBO_DEV_STATE_SEARCHING   = 1,
    AMIIBO_DEV_STATE_TAG_FOUND   = 2,
    AMIIBO_DEV_STATE_TAG_REMOVED = 3,
    AMIIBO_DEV_STATE_TAG_MOUNTED = 4,
    AMIIBO_DEV_STATE_UNAVAILABLE = 5
} AmiiboDeviceState;

typedef struct {
    uint8_t uuid[10];
    uint8_t uuid_len;
    uint8_t character_id[3];
    uint8_t series_id;
    uint16_t numbering_id;
    uint8_t nfp_type;
    uint8_t version;
    char path[512];
    char filename[128];
} AmiiboInfo;

void amiibo_init(void);
void amiibo_set_device_present(bool present);
bool amiibo_is_device_present(void);
void amiibo_start_detection(void);
void amiibo_stop_detection(void);
bool amiibo_is_detecting(void);
bool amiibo_mount(void);
void amiibo_unmount(void);
bool amiibo_is_mounted(void);
AmiiboDeviceState amiibo_get_device_state(void);
AmiiboDeviceState amiibo_get_reader_state(void);
bool amiibo_load_file(const char *path);
bool amiibo_scan_and_tap(const char *path);
void amiibo_tap_tag(void);
void amiibo_remove_tag(void);
void amiibo_tick(void);
void amiibo_set_auto_remove(bool enable);
bool amiibo_get_auto_remove(void);
void amiibo_set_auto_remove_delay_s(float seconds);
float amiibo_get_auto_remove_delay_s(void);
bool amiibo_has_tag(void);
const AmiiboInfo *amiibo_get_current_info(void);
const uint8_t *amiibo_get_raw_data(void);
void amiibo_get_tag_info(void *out_tag_info);
void amiibo_get_model_info(void *out_model_info);
void amiibo_get_common_info(void *out_common_info);
void amiibo_get_register_info(void *out_register_info);
int amiibo_get_application_area(void *out_buf, uint32_t max_size);

#ifdef __cplusplus
}
#endif
