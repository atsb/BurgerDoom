#ifndef DOOM3DO_RESOURCEMGR_H
#define DOOM3DO_RESOURCEMGR_H

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t number;
    uint32_t type;
    uint32_t offset;
    uint32_t size;
    void *data;
    void **handle;
} ResourceMgr_Resource;

typedef struct {
    FILE *stream;
    ResourceMgr_Resource *resources;
    size_t count;
    size_t capacity;
    uint32_t resourceBaseOffset;
    int initialized;
} ResourceMgr;

int ResourceMgr_Init(ResourceMgr *resourceManager,const char *rezFilePath);
void ResourceMgr_Destroy(ResourceMgr *resourceManager);
const ResourceMgr_Resource *ResourceMgr_Get(const ResourceMgr *resourceManager,uint32_t number);
const ResourceMgr_Resource *ResourceMgr_Load(ResourceMgr *resourceManager,uint32_t number);
void ResourceMgr_Free(ResourceMgr *resourceManager,uint32_t number);
void **ResourceMgr_LoadHandle(ResourceMgr *resourceManager,uint32_t number);
void *ResourceMgr_LockHandle(void **handle);
uint32_t ResourceMgr_GetHandleSize(void *handle);

#endif
