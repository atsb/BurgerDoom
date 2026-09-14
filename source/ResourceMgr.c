#include "ResourceMgr.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

static uint32_t ReadBE32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) |
           bytes[3];
}

static int CompareResources(const void *leftValue,const void *rightValue)
{
    const ResourceMgr_Resource *leftResource =
        (const ResourceMgr_Resource *)leftValue;
    const ResourceMgr_Resource *rightResource =
        (const ResourceMgr_Resource *)rightValue;

    return (leftResource->number > rightResource->number) -
           (leftResource->number < rightResource->number);
}

static int ReserveResources(ResourceMgr *resourceManager)
{
    size_t newCapacity = resourceManager->capacity ?
                         resourceManager->capacity * 2u : 64u;
    ResourceMgr_Resource *resources = (ResourceMgr_Resource *)realloc(
        resourceManager->resources,newCapacity * sizeof(*resources));

    if (!resources)
        return 0;

    resourceManager->resources = resources;
    resourceManager->capacity = newCapacity;
    return 1;
}

int ResourceMgr_Init(ResourceMgr *resourceManager,const char *rezFilePath)
{
    FILE *stream;
    uint8_t header[12];
    uint8_t *table = NULL;
    uint32_t groupCount;
    uint32_t tableSize;
    uint32_t groupIndex;
    size_t tablePosition = 0;
    long fileSize;

    if (!resourceManager || !rezFilePath)
        return 0;

    memset(resourceManager,0,sizeof(*resourceManager));

    /* REZFILE is a standalone host file now.  It is not an ISO/IMG image
     * and must never be routed through the 3DO CD sector reader. */
    stream = fopen(rezFilePath,"rb");
    if (!stream)
        return 0;

    if (fseek(stream,0,SEEK_END) != 0) {
        fclose(stream);
        return 0;
    }
    fileSize = ftell(stream);
    if (fileSize < 0 || (uint64_t)fileSize > UINT32_MAX) {
        fclose(stream);
        return 0;
    }
    rewind(stream);

    resourceManager->stream = stream;
    resourceManager->resourceBaseOffset = 0;

    if (fread(header,1,sizeof(header),stream) != sizeof(header))
        goto fail;
    if (ReadBE32(header) != 0x42524752u)
        goto fail;

    groupCount = ReadBE32(header + 4u);
    tableSize = ReadBE32(header + 8u);

    if (!groupCount || tableSize > (uint32_t)fileSize - 12u)
        goto fail;

    table = (uint8_t *)malloc(tableSize);
    if (!table)
        goto fail;

    if (fread(table,1,tableSize,stream) != tableSize)
        goto fail;

    for (groupIndex = 0; groupIndex < groupCount; ++groupIndex) {
        uint32_t resourceType;
        uint32_t firstResource;
        uint32_t resourceCount;
        uint32_t resourceIndex;

        if (tablePosition + 12u > tableSize)
            goto fail;

        resourceType = ReadBE32(table + tablePosition);
        firstResource = ReadBE32(table + tablePosition + 4u);
        resourceCount = ReadBE32(table + tablePosition + 8u);
        tablePosition += 12u;

        if (resourceCount > UINT32_MAX - firstResource)
            goto fail;

        for (resourceIndex = 0; resourceIndex < resourceCount; ++resourceIndex) {
            ResourceMgr_Resource resource;
            uint32_t rawOffset;
            uint32_t resourceSize;

            if (tablePosition + 12u > tableSize)
                goto fail;

            rawOffset = ReadBE32(table + tablePosition) & 0x3FFFFFFFu;
            resourceSize = ReadBE32(table + tablePosition + 4u);
            tablePosition += 12u;

            if (rawOffset > (uint32_t)fileSize ||
                resourceSize > (uint32_t)fileSize - rawOffset)
                goto fail;

            if (resourceManager->count == resourceManager->capacity &&
                !ReserveResources(resourceManager))
                goto fail;

            memset(&resource,0,sizeof(resource));
            resource.number = firstResource + resourceIndex;
            resource.type = resourceType;
            resource.offset = rawOffset;
            resource.size = resourceSize;
            resourceManager->resources[resourceManager->count++] = resource;
        }
    }

    qsort(resourceManager->resources,resourceManager->count,
          sizeof(*resourceManager->resources),CompareResources);
    resourceManager->initialized = 1;
    free(table);
    return 1;

fail:
    free(table);
    ResourceMgr_Destroy(resourceManager);
    return 0;
}

void ResourceMgr_Destroy(ResourceMgr *resourceManager)
{
    size_t index;

    if (!resourceManager)
        return;

    for (index = 0; index < resourceManager->count; ++index) {
        free(resourceManager->resources[index].data);
        free(resourceManager->resources[index].handle);
    }

    free(resourceManager->resources);
    if (resourceManager->stream)
        fclose(resourceManager->stream);
    memset(resourceManager,0,sizeof(*resourceManager));
}

const ResourceMgr_Resource *ResourceMgr_Get(const ResourceMgr *resourceManager,
                                             uint32_t number)
{
    size_t low = 0;
    size_t high;

    if (!resourceManager)
        return NULL;

    high = resourceManager->count;
    while (low < high) {
        size_t middle = low + (high - low) / 2u;

        if (resourceManager->resources[middle].number < number)
            low = middle + 1u;
        else
            high = middle;
    }

    if (low < resourceManager->count &&
        resourceManager->resources[low].number == number)
        return &resourceManager->resources[low];

    return NULL;
}

const ResourceMgr_Resource *ResourceMgr_Load(ResourceMgr *resourceManager,
                                               uint32_t number)
{
    ResourceMgr_Resource *resource;

    resource = (ResourceMgr_Resource *)(uintptr_t)
        ResourceMgr_Get(resourceManager,number);
    if (!resource || !resourceManager->initialized)
        return NULL;

    if (!resource->data) {
        resource->data = malloc(resource->size ? resource->size : 1u);
        if (!resource->data)
            return NULL;

        if (fseek(resourceManager->stream,(long)resource->offset,SEEK_SET) != 0 ||
            fread(resource->data,1,resource->size,resourceManager->stream) != resource->size) {
            free(resource->data);
            resource->data = NULL;
            return NULL;
        }
    }

    return resource;
}

void ResourceMgr_Free(ResourceMgr *resourceManager,uint32_t number)
{
    (void)resourceManager;
    (void)number;
}

void **ResourceMgr_LoadHandle(ResourceMgr *resourceManager,uint32_t number)
{
    ResourceMgr_Resource *resource;

    resource = (ResourceMgr_Resource *)(uintptr_t)
        ResourceMgr_Load(resourceManager,number);
    if (!resource)
        return NULL;

    if (!resource->handle) {
        resource->handle = (void **)malloc(sizeof(void *));
        if (!resource->handle)
            return NULL;
    }

    *resource->handle = resource->data;
    return resource->handle;
}

void *ResourceMgr_LockHandle(void **handle)
{
    return handle ? *handle : NULL;
}

uint32_t ResourceMgr_GetHandleSize(void *handle)
{
    (void)handle;
    return 0;
}
