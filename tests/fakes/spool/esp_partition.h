#pragma once
constexpr int ESP_PARTITION_TYPE_DATA=1,ESP_PARTITION_SUBTYPE_DATA_SPIFFS=2,ESP_OK=0;
struct esp_partition_t {size_t size=512;};
inline bool partitionBlank=true;
inline const esp_partition_t* esp_partition_find_first(int,int,const char*){static esp_partition_t p;return &p;}
inline int esp_partition_read(const esp_partition_t*,size_t,void* data,size_t size){memset(data,partitionBlank?0xff:0,size);return 0;}
#define taskYIELD() ((void)0)
