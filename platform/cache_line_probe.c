/**
 * @file cache_line_probe.c
 * @brief 构建配置时运行的缓存行探针，不编入 nanosig 库。
 */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#elif defined(__linux__)
#include <glob.h>
#include <unistd.h>
#endif

int main(void)
{
    unsigned long line_size = 0u;

#if defined(_WIN32)
    DWORD bytes = 0u;
    unsigned char *buffer;
    size_t offset = 0u;
    const size_t header_size = offsetof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, Cache);
    const size_t cache_size = header_size + offsetof(CACHE_RELATIONSHIP, Type) + sizeof(PROCESSOR_CACHE_TYPE);

    if(GetLogicalProcessorInformationEx(RelationCache, NULL, &bytes) ||
       GetLastError() != ERROR_INSUFFICIENT_BUFFER) return 1;
    buffer = malloc((size_t)bytes);
    if(buffer == NULL) return 1;
    if(!GetLogicalProcessorInformationEx(RelationCache,
            (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)buffer, &bytes)){
        free(buffer);
        return 1;
    }
    while(offset < (size_t)bytes){
        PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX entry;
        if((size_t)bytes - offset < header_size){
            free(buffer);
            return 1;
        }
        entry = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(buffer + offset);
        if((size_t)entry->Size < header_size || (size_t)entry->Size > (size_t)bytes - offset){
            free(buffer);
            return 1;
        }
        if(entry->Relationship == RelationCache){
            if((size_t)entry->Size < cache_size){
                free(buffer);
                return 1;
            }
            if((entry->Cache.Type == CacheData || entry->Cache.Type == CacheUnified) &&
               entry->Cache.LineSize > line_size){
                line_size = entry->Cache.LineSize;
            }
        }
        offset += (size_t)entry->Size;
    }
    free(buffer);
#elif defined(__APPLE__)
    size_t value = 0u;
    size_t bytes = sizeof(value);
    if(sysctlbyname("hw.cachelinesize", &value, &bytes, NULL, 0u) != 0) return 1;
    line_size = (unsigned long)value;
#elif defined(__linux__)
    glob_t paths = {0};
    if(glob("/sys/devices/system/cpu/cpu[0-9]*/cache/index*/coherency_line_size",
            0, NULL, &paths) == 0){
        for(size_t i = 0u; i < paths.gl_pathc; ++i){
            unsigned long value = 0u;
            FILE *file = fopen(paths.gl_pathv[i], "r");
            if(file == NULL) continue;
            if(fscanf(file, "%lu", &value) == 1 && value > line_size) line_size = value;
            fclose(file);
        }
    }
    globfree(&paths);
#ifdef _SC_LEVEL1_DCACHE_LINESIZE
    /* 部分 libc/架构仅返回最小行长；优先使用上面所有 CPU 的 sysfs 最大值。 */
    if(line_size == 0u){
        long value = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
        if(value > 0) line_size = (unsigned long)value;
    }
#endif
#endif

    if(line_size == 0u) return 1;
    printf("%lu\n", line_size);
    return 0;
}
