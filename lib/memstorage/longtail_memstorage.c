#include "longtail_memstorage.h"

#include "../longtail_platform.h"
#include "../../src/ext/stb_ds.h"

#include <errno.h>
#include <inttypes.h>
#include <ctype.h>

#if defined(__clang__) || defined(__GNUC__)
#if defined(WIN32)
    #include <malloc.h>
#else
    #include <alloca.h>
#endif
#elif defined(_MSC_VER)
    #include <malloc.h>
    #define alloca _alloca
#endif

static const uint32_t Prime = 0x01000193;
static const uint32_t Seed  = 0x811C9DC5;

static uint32_t fnv1a(const void* data, uint32_t numBytes)
{
    uint32_t hash = Seed;
    const unsigned char* ptr = (const unsigned char*)data;
    while (numBytes--)
    {
        hash = ((*ptr++) ^ hash) * Prime;
    }
    return hash;
}

struct PathEntry
{
    char* m_FileName;
    uint32_t m_ParentHash;
    uint8_t* m_Content;
    uint16_t m_Permissions;
    uint8_t m_IsOpenWrite;
    uint32_t m_IsOpenRead;
};

struct Lookup
{
    uint32_t key;
    uint32_t value;
};

struct InMemStorageAPI
{
    struct Longtail_StorageAPI m_InMemStorageAPI;
    struct Lookup* m_PathHashToContent;
    struct PathEntry* m_PathEntries;
    HLongtail_SpinLock m_SpinLock;
};

static void InMemStorageAPI_Dispose(struct Longtail_API* storage_api)
{
    struct InMemStorageAPI* in_mem_storage_api = (struct InMemStorageAPI*)storage_api;
    size_t c = (size_t)arrlen(in_mem_storage_api->m_PathEntries);
    while(c--)
    {
        struct PathEntry* path_entry = &in_mem_storage_api->m_PathEntries[c];
        Longtail_Free(path_entry->m_FileName);
        path_entry->m_FileName = 0;
        arrfree(path_entry->m_Content);
        path_entry->m_Content = 0;
    }
    Longtail_DeleteSpinLock(in_mem_storage_api->m_SpinLock);
    hmfree(in_mem_storage_api->m_PathHashToContent);
    in_mem_storage_api->m_PathHashToContent = 0;
    arrfree(in_mem_storage_api->m_PathEntries);
    in_mem_storage_api->m_PathEntries = 0;
    Longtail_Free(storage_api);
}

static void InMemStorageAPI_ToLowerCase(char *str)
{
    for ( ; *str; ++str)
    {
        *str = tolower(*str);
    }
}

static uint32_t InMemStorageAPI_GetPathHash(const char* path)
{
    uint32_t pathlen = (uint32_t)strlen(path);
    char* buf = (char*)alloca(pathlen + 1);
    memcpy(buf, path, pathlen + 1);
    InMemStorageAPI_ToLowerCase(buf);
    return fnv1a((void*)buf, pathlen);
}

static int InMemStorageAPI_OpenReadFile(struct Longtail_StorageAPI* storage_api, const char* path, Longtail_StorageAPI_HOpenFile* out_open_file)
{
    LONGTAIL_VALIDATE_INPUT(storage_api != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(path != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(out_open_file != 0, return EINVAL);
    struct InMemStorageAPI* instance = (struct InMemStorageAPI*)storage_api;
    uint32_t path_hash = InMemStorageAPI_GetPathHash(path);
    Longtail_LockSpinLock(instance->m_SpinLock);
    intptr_t it = hmgeti(instance->m_PathHashToContent, path_hash);
    if (it != -1)
    {
        struct PathEntry* path_entry = &instance->m_PathEntries[instance->m_PathHashToContent[it].value];
        if ((path_entry->m_Permissions & (Longtail_StorageAPI_OtherReadAccess | Longtail_StorageAPI_GroupReadAccess | Longtail_StorageAPI_UserReadAccess)) == 0)
        {
            Longtail_UnlockSpinLock(instance->m_SpinLock);
            LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_INFO, "InMemStorageAPI_OpenReadFile(%p, %s, %p) failed with %d",
                storage_api, path, out_open_file,
                EACCES)
            return EACCES;
        }
        if (path_entry->m_IsOpenWrite)
        {
            Longtail_UnlockSpinLock(instance->m_SpinLock);
            LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_INFO, "InMemStorageAPI_OpenReadFile(%p, %s, %p) failed with %d",
                storage_api, path, out_open_file,
                EPERM)
            return EPERM;
        }
        ++path_entry->m_IsOpenRead;
        Longtail_UnlockSpinLock(instance->m_SpinLock);
        *out_open_file = (Longtail_StorageAPI_HOpenFile)(uintptr_t)path_hash;
        return 0;
    }
    Longtail_UnlockSpinLock(instance->m_SpinLock);
    LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_INFO, "InMemStorageAPI_OpenReadFile(%p, %s, %p) failed with %d",
        storage_api, path, out_open_file,
        ENOENT)
    return ENOENT;
}

static int InMemStorageAPI_GetSize(struct Longtail_StorageAPI* storage_api, Longtail_StorageAPI_HOpenFile f, uint64_t* out_size)
{
    LONGTAIL_VALIDATE_INPUT(storage_api != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(f != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(out_size != 0, return EINVAL);
    struct InMemStorageAPI* instance = (struct InMemStorageAPI*)storage_api;
    Longtail_LockSpinLock(instance->m_SpinLock);
    uint32_t path_hash = (uint32_t)(uintptr_t)f;
    intptr_t it = hmgeti(instance->m_PathHashToContent, path_hash);
    if (it == -1) {
        Longtail_UnlockSpinLock(instance->m_SpinLock);
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "InMemStorageAPI_GetSize(%p, %p, %p) failed with %d",
            storage_api, f, out_size,
            ENOENT)
        return ENOENT;
    }
    struct PathEntry* path_entry = (struct PathEntry*)&instance->m_PathEntries[instance->m_PathHashToContent[it].value];
    uint64_t size = (uint64_t)arrlen(path_entry->m_Content);
    Longtail_UnlockSpinLock(instance->m_SpinLock);
    *out_size = size;
    return 0;
}

static int InMemStorageAPI_Read(struct Longtail_StorageAPI* storage_api, Longtail_StorageAPI_HOpenFile f, uint64_t offset, uint64_t length, void* output)
{
    LONGTAIL_VALIDATE_INPUT(storage_api != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(f != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(output != 0, return EINVAL);
    struct InMemStorageAPI* instance = (struct InMemStorageAPI*)storage_api;
    Longtail_LockSpinLock(instance->m_SpinLock);
    uint32_t path_hash = (uint32_t)(uintptr_t)f;
    intptr_t it = hmgeti(instance->m_PathHashToContent, path_hash);
    if (it == -1) {
        Longtail_UnlockSpinLock(instance->m_SpinLock);
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "InMemStorageAPI_Read(%p, %p, %" PRIu64 ", %" PRIu64 ", %p) failed with %d",
            storage_api, f, offset, length, output,
            EINVAL)
        return EINVAL;
    }
    struct PathEntry* path_entry = (struct PathEntry*)&instance->m_PathEntries[instance->m_PathHashToContent[it].value];
    if ((ptrdiff_t)(offset + length) > arrlen(path_entry->m_Content))
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "InMemStorageAPI_Read(%p, %p, %" PRIu64 ", %" PRIu64 ", %p) failed with %d",
            storage_api, f, offset, length, output,
            EIO)
        Longtail_UnlockSpinLock(instance->m_SpinLock);
        return EIO;
    }
    void* content_ptr = &path_entry->m_Content[offset];
    // A bit dangerous - we assume nobody is writing to the file while we are reading (which is unsupported here)
    Longtail_UnlockSpinLock(instance->m_SpinLock);
    memcpy(output, content_ptr, length);
    return 0;
}

static uint32_t InMemStorageAPI_GetParentPathHash(const char* path)
{
    const char* dir_path_begin = strrchr(path, '/');
    if (!dir_path_begin)
    {
        return 0;
    }
    size_t dir_length = (uintptr_t)dir_path_begin - (uintptr_t)path;
    char* dir_path = (char*)alloca(dir_length + 1);
    strncpy(dir_path, path, dir_length);
    dir_path[dir_length] = '\0';
    uint32_t hash = InMemStorageAPI_GetPathHash(dir_path);
    return hash;
}

static const char* InMemStorageAPI_GetFileNamePart(const char* path)
{
    const char* file_name = strrchr(path, '/');
    if (file_name == 0)
    {
        return path;
    }
    return &file_name[1];
}

static int InMemStorageAPI_OpenWriteFile(struct Longtail_StorageAPI* storage_api, const char* path, uint64_t initial_size, Longtail_StorageAPI_HOpenFile* out_open_file)
{
    LONGTAIL_VALIDATE_INPUT(storage_api != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(path != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(out_open_file != 0, return EINVAL);
    struct InMemStorageAPI* instance = (struct InMemStorageAPI*)storage_api;
    uint32_t path_hash = InMemStorageAPI_GetPathHash(path);
    uint32_t parent_path_hash = InMemStorageAPI_GetParentPathHash(path);
    Longtail_LockSpinLock(instance->m_SpinLock);
    if (parent_path_hash != 0 && hmgeti(instance->m_PathHashToContent, parent_path_hash) == -1)
    {
        Longtail_UnlockSpinLock(instance->m_SpinLock);
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_INFO, "InMemStorageAPI_OpenWriteFile(%p, %s, %" PRIu64 ", %p) failed with %d",
            storage_api, path, initial_size, out_open_file,
            ENOENT)
        return ENOENT;
    }
    struct PathEntry* path_entry = 0;
    intptr_t it = hmgeti(instance->m_PathHashToContent, path_hash);
    if (it != -1)
    {
        path_entry = &instance->m_PathEntries[instance->m_PathHashToContent[it].value];
        if ((path_entry->m_Permissions & (Longtail_StorageAPI_OtherWriteAccess | Longtail_StorageAPI_GroupWriteAccess | Longtail_StorageAPI_UserWriteAccess)) == 0)
        {
            Longtail_UnlockSpinLock(instance->m_SpinLock);
            LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_INFO, "InMemStorageAPI_OpenWriteFile(%p, %s, %" PRIu64 ", %p) failed with %d",
                storage_api, path, initial_size, out_open_file,
                EACCES)
            return EACCES;
        }
        if (path_entry->m_IsOpenWrite)
        {
            Longtail_UnlockSpinLock(instance->m_SpinLock);
            LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_INFO, "InMemStorageAPI_OpenWriteFile(%p, %s, %p) failed with %d",
                storage_api, path, out_open_file,
                EPERM)
                return EPERM;
        }

        if (path_entry->m_IsOpenRead)
        {
            Longtail_UnlockSpinLock(instance->m_SpinLock);
            LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_INFO, "InMemStorageAPI_OpenReadFile(%p, %s, %p) failed with %d",
                storage_api, path, out_open_file,
                EPERM)
            return EPERM;
        }
        path_entry->m_IsOpenWrite = 1;
    }
    else
    {
        ptrdiff_t entry_index = arrlen(instance->m_PathEntries);
        arrsetlen(instance->m_PathEntries, (size_t)(entry_index + 1));
        path_entry = &instance->m_PathEntries[entry_index];
        path_entry->m_ParentHash = parent_path_hash;
        path_entry->m_FileName = Longtail_Strdup(InMemStorageAPI_GetFileNamePart(path));
        path_entry->m_Content = 0;
        path_entry->m_Permissions = 0644;
        path_entry->m_IsOpenRead = 0;
        path_entry->m_IsOpenWrite = 1;
        hmput(instance->m_PathHashToContent, path_hash, (uint32_t)entry_index);
    }
    arrsetcap(path_entry->m_Content, initial_size == 0 ? 16 : (uint32_t)initial_size);
    arrsetlen(path_entry->m_Content, (uint32_t)initial_size);
    Longtail_UnlockSpinLock(instance->m_SpinLock);
    *out_open_file = (Longtail_StorageAPI_HOpenFile)(uintptr_t)path_hash;
    return 0;
}

static int InMemStorageAPI_Write(struct Longtail_StorageAPI* storage_api, Longtail_StorageAPI_HOpenFile f, uint64_t offset, uint64_t length, const void* input)
{
    LONGTAIL_VALIDATE_INPUT(storage_api != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(f != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(input != 0, return EINVAL);
    struct InMemStorageAPI* instance = (struct InMemStorageAPI*)storage_api;
    Longtail_LockSpinLock(instance->m_SpinLock);
    uint32_t path_hash = (uint32_t)(uintptr_t)f;
    intptr_t it = hmgeti(instance->m_PathHashToContent, path_hash);
    if (it == -1)
    {
        Longtail_UnlockSpinLock(instance->m_SpinLock);
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "InMemStorageAPI_Write(%p, %p, %" PRIu64 ", %" PRIu64 ", %p) failed with %d",
            storage_api, f, offset, length, input,
            EINVAL)
        return EINVAL;
    }
    struct PathEntry* path_entry = &instance->m_PathEntries[instance->m_PathHashToContent[it].value];
    ptrdiff_t size = arrlen(path_entry->m_Content);
    if ((ptrdiff_t)offset > size)
    {
        Longtail_UnlockSpinLock(instance->m_SpinLock);
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "InMemStorageAPI_Write(%p, %p, %" PRIu64 ", %" PRIu64 ", %p) failed with %d",
            storage_api, f, offset, length, input,
            EIO)
        return EIO;
    }
    if ((ptrdiff_t)(offset + length) > size)
    {
        size = offset + length;
    }
    arrsetcap(path_entry->m_Content, size == 0 ? 16 : (uint32_t)size);
    arrsetlen(path_entry->m_Content, (uint32_t)size);
    memcpy(&(path_entry->m_Content)[offset], input, length);
    Longtail_UnlockSpinLock(instance->m_SpinLock);
    return 0;
}

static int InMemStorageAPI_SetSize(struct Longtail_StorageAPI* storage_api, Longtail_StorageAPI_HOpenFile f, uint64_t length)
{
    LONGTAIL_VALIDATE_INPUT(storage_api != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(f != 0, return EINVAL);
    struct InMemStorageAPI* instance = (struct InMemStorageAPI*)storage_api;
    Longtail_LockSpinLock(instance->m_SpinLock);
    uint32_t path_hash = (uint32_t)(uintptr_t)f;
    intptr_t it = hmgeti(instance->m_PathHashToContent, path_hash);
    if (it == -1)
    {
        Longtail_UnlockSpinLock(instance->m_SpinLock);
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "InMemStorageAPI_SetSize(%p, %p, %" PRIu64 ") failed with %d",
            storage_api, f, length,
            EINVAL)
        return EINVAL;
    }
    struct PathEntry* path_entry = &instance->m_PathEntries[instance->m_PathHashToContent[it].value];
    arrsetlen(path_entry->m_Content, (uint32_t)length);
    Longtail_UnlockSpinLock(instance->m_SpinLock);
    return 0;
}

static int InMemStorageAPI_SetPermissions(struct Longtail_StorageAPI* storage_api, const char* path, uint16_t permissions)
{
    LONGTAIL_VALIDATE_INPUT(storage_api != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(path != 0, return EINVAL);
    struct InMemStorageAPI* instance = (struct InMemStorageAPI*)storage_api;
    uint32_t path_hash = InMemStorageAPI_GetPathHash(path);
    Longtail_LockSpinLock(instance->m_SpinLock);
    intptr_t it = hmgeti(instance->m_PathHashToContent, path_hash);
    if (it == -1)
    {
        Longtail_UnlockSpinLock(instance->m_SpinLock);
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_INFO, "InMemStorageAPI_SetPermissions(%p, %s, %u) failed with %d",
            storage_api, path, permissions,
            ENOENT)
        return ENOENT;
    }
    struct PathEntry* path_entry = &instance->m_PathEntries[instance->m_PathHashToContent[it].value];
    path_entry->m_Permissions = permissions;
    Longtail_UnlockSpinLock(instance->m_SpinLock);
    return 0;
}

static int InMemStorageAPI_GetPermissions(struct Longtail_StorageAPI* storage_api, const char* path, uint16_t* out_permissions)
{
    LONGTAIL_VALIDATE_INPUT(storage_api != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(path != 0, return EINVAL);
    struct InMemStorageAPI* instance = (struct InMemStorageAPI*)storage_api;
    uint32_t path_hash = InMemStorageAPI_GetPathHash(path);
    Longtail_LockSpinLock(instance->m_SpinLock);
    intptr_t it = hmgeti(instance->m_PathHashToContent, path_hash);
    if (it == -1)
    {
        Longtail_UnlockSpinLock(instance->m_SpinLock);
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_INFO, "InMemStorageAPI_GetPermissions(%p, %s, %u) failed with %d",
            storage_api, path, out_permissions,
            ENOENT)
        return ENOENT;
    }
    struct PathEntry* path_entry = &instance->m_PathEntries[instance->m_PathHashToContent[it].value];
    *out_permissions = path_entry->m_Permissions;
    Longtail_UnlockSpinLock(instance->m_SpinLock);
    return 0;
}

static void InMemStorageAPI_CloseFile(struct Longtail_StorageAPI* storage_api, Longtail_StorageAPI_HOpenFile f)
{
    struct InMemStorageAPI* instance = (struct InMemStorageAPI*)storage_api;
    Longtail_LockSpinLock(instance->m_SpinLock);
    uint32_t path_hash = (uint32_t)(uintptr_t)f;
    intptr_t it = hmgeti(instance->m_PathHashToContent, path_hash);
    if (it == -1)
    {
        Longtail_UnlockSpinLock(instance->m_SpinLock);
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "InMemStorageAPI_CloseFile(%p, %p) failed with %d",
            storage_api, f,
            EINVAL)
    }
    struct PathEntry* path_entry = &instance->m_PathEntries[instance->m_PathHashToContent[it].value];
    if (path_entry->m_IsOpenRead > 0)
    {
        LONGTAIL_FATAL_ASSERT(path_entry->m_IsOpenWrite == 0, return);
        --path_entry->m_IsOpenRead;
    }
    else
    {
        LONGTAIL_FATAL_ASSERT(path_entry->m_IsOpenRead == 0, return);
        LONGTAIL_FATAL_ASSERT(path_entry->m_IsOpenWrite == 1, return);
        path_entry->m_IsOpenWrite = 0;
    }

    Longtail_UnlockSpinLock(instance->m_SpinLock);
}

static int InMemStorageAPI_CreateDir(struct Longtail_StorageAPI* storage_api, const char* path)
{
    LONGTAIL_VALIDATE_INPUT(storage_api != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(path != 0, return EINVAL);
    struct InMemStorageAPI* instance = (struct InMemStorageAPI*)storage_api;
    uint32_t parent_path_hash = InMemStorageAPI_GetParentPathHash(path);
    uint32_t path_hash = InMemStorageAPI_GetPathHash(path);
    Longtail_LockSpinLock(instance->m_SpinLock);
    if (parent_path_hash != 0 && hmgeti(instance->m_PathHashToContent, parent_path_hash) == -1)
    {
        Longtail_UnlockSpinLock(instance->m_SpinLock);
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_INFO, "InMemStorageAPI_CreateDir(%p, %s) failed with %d",
            storage_api, path,
            EINVAL)
        return EINVAL;
    }
    intptr_t source_path_ptr = hmgeti(instance->m_PathHashToContent, path_hash);
    if (source_path_ptr != -1)
    {
        struct PathEntry* path_entry = &instance->m_PathEntries[instance->m_PathHashToContent[source_path_ptr].value];
        if ((path_entry->m_Permissions & (Longtail_StorageAPI_OtherWriteAccess | Longtail_StorageAPI_GroupWriteAccess | Longtail_StorageAPI_UserWriteAccess)) == 0)
        {
            Longtail_UnlockSpinLock(instance->m_SpinLock);
            LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_INFO, "InMemStorageAPI_CreateDir(%p, %s) failed with %d",
                storage_api, path,
                EACCES)
            return EACCES;
        }
        if (path_entry->m_Content == 0)
        {
            Longtail_UnlockSpinLock(instance->m_SpinLock);
            return EEXIST;
        }
        Longtail_UnlockSpinLock(instance->m_SpinLock);
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_INFO, "InMemStorageAPI_CreateDir(%p, %s) failed with %d",
            storage_api, path,
            EIO)
        return EIO;
    }

    ptrdiff_t entry_index = arrlen(instance->m_PathEntries);
    arrsetlen(instance->m_PathEntries, (size_t)(entry_index + 1));
    struct PathEntry* path_entry = &instance->m_PathEntries[entry_index];
    path_entry->m_ParentHash = parent_path_hash;
    path_entry->m_FileName = Longtail_Strdup(InMemStorageAPI_GetFileNamePart(path));
    path_entry->m_Content = 0;
    path_entry->m_Permissions = 0775;
    hmput(instance->m_PathHashToContent, path_hash, (uint32_t)entry_index);
    Longtail_UnlockSpinLock(instance->m_SpinLock);
    return 0;
}

static int InMemStorageAPI_RenameFile(struct Longtail_StorageAPI* storage_api, const char* source_path, const char* target_path)
{
    LONGTAIL_VALIDATE_INPUT(storage_api != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(source_path != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(target_path != 0, return EINVAL);
    struct InMemStorageAPI* instance = (struct InMemStorageAPI*)storage_api;
    uint32_t source_path_hash = InMemStorageAPI_GetPathHash(source_path);
    uint32_t target_path_hash = InMemStorageAPI_GetPathHash(target_path);
    uint32_t target_parent_path_hash = InMemStorageAPI_GetParentPathHash(target_path);
    Longtail_LockSpinLock(instance->m_SpinLock);
    intptr_t source_path_ptr = hmgeti(instance->m_PathHashToContent, source_path_hash);
    if (source_path_ptr == -1)
    {
        Longtail_UnlockSpinLock(instance->m_SpinLock);
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_INFO, "InMemStorageAPI_RenameFile(%p, %s, %s) failed with %d",
            storage_api, source_path, target_path,
            ENOENT)
        return ENOENT;
    }
    struct PathEntry* source_entry = &instance->m_PathEntries[instance->m_PathHashToContent[source_path_ptr].value];

    intptr_t target_path_ptr = hmgeti(instance->m_PathHashToContent, target_path_hash);
    if (target_path_ptr != -1)
    {
        Longtail_UnlockSpinLock(instance->m_SpinLock);
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_INFO, "InMemStorageAPI_RenameFile(%p, %s, %s) failed with %d",
            storage_api, source_path, target_path,
            EEXIST)
        return EEXIST;
    }
    source_entry->m_ParentHash = target_parent_path_hash;
    Longtail_Free(source_entry->m_FileName);
    source_entry->m_FileName = Longtail_Strdup(InMemStorageAPI_GetFileNamePart(target_path));
    hmput(instance->m_PathHashToContent, target_path_hash, instance->m_PathHashToContent[source_path_ptr].value);
    hmdel(instance->m_PathHashToContent, source_path_hash);
    Longtail_UnlockSpinLock(instance->m_SpinLock);
    return 0;
}

static char* InMemStorageAPI_ConcatPath(struct Longtail_StorageAPI* storage_api, const char* root_path, const char* sub_path)
{
    LONGTAIL_VALIDATE_INPUT(storage_api != 0, return 0);
    LONGTAIL_VALIDATE_INPUT(root_path != 0, return 0);
    LONGTAIL_VALIDATE_INPUT(sub_path != 0, return 0);
    if (root_path[0] == 0)
    {
        return Longtail_Strdup(sub_path);
    }
    size_t path_len = strlen(root_path) + 1 + strlen(sub_path) + 1;
    char* path = (char*)Longtail_Alloc(path_len);
    if (path == 0)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "InMemStorageAPI_ConcatPath(%p, %s, %s) failed with %d",
            storage_api, root_path, sub_path,
            ENOMEM)
        return 0;
    }
    strcpy(path, root_path);
    strcat(path, "/");
    strcat(path, sub_path);
    return path;
}

static int InMemStorageAPI_IsDir(struct Longtail_StorageAPI* storage_api, const char* path)
{
    LONGTAIL_VALIDATE_INPUT(storage_api != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(path != 0, return EINVAL);
    struct InMemStorageAPI* instance = (struct InMemStorageAPI*)storage_api;
    uint32_t source_path_hash = InMemStorageAPI_GetPathHash(path);
    Longtail_LockSpinLock(instance->m_SpinLock);
    intptr_t source_path_ptr = hmgeti(instance->m_PathHashToContent, source_path_hash);
    if (source_path_ptr == -1)
    {
        Longtail_UnlockSpinLock(instance->m_SpinLock);
        return 0;
    }
    struct PathEntry* path_entry = &instance->m_PathEntries[instance->m_PathHashToContent[source_path_ptr].value];
    int is_dir = path_entry->m_Content == 0;
    Longtail_UnlockSpinLock(instance->m_SpinLock);
    return is_dir;
}
static int InMemStorageAPI_IsFile(struct Longtail_StorageAPI* storage_api, const char* path)
{
    LONGTAIL_VALIDATE_INPUT(storage_api != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(path != 0, return EINVAL);
    struct InMemStorageAPI* instance = (struct InMemStorageAPI*)storage_api;
    uint32_t path_hash = InMemStorageAPI_GetPathHash(path);
    Longtail_LockSpinLock(instance->m_SpinLock);
    intptr_t source_path_ptr = hmgeti(instance->m_PathHashToContent, path_hash);
    if (source_path_ptr == -1)
    {
        Longtail_UnlockSpinLock(instance->m_SpinLock);
        return 0;
    }
    struct PathEntry* path_entry = &instance->m_PathEntries[instance->m_PathHashToContent[source_path_ptr].value];
    int is_file = path_entry->m_Content != 0;
    Longtail_UnlockSpinLock(instance->m_SpinLock);
    return is_file;
}

static int InMemStorageAPI_RemoveDir(struct Longtail_StorageAPI* storage_api, const char* path)
{
    LONGTAIL_VALIDATE_INPUT(storage_api != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(path != 0, return EINVAL);
    struct InMemStorageAPI* instance = (struct InMemStorageAPI*)storage_api;
    uint32_t path_hash = InMemStorageAPI_GetPathHash(path);
    Longtail_LockSpinLock(instance->m_SpinLock);
    intptr_t source_path_ptr = hmgeti(instance->m_PathHashToContent, path_hash);
    if (source_path_ptr == -1)
    {
        Longtail_UnlockSpinLock(instance->m_SpinLock);
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_INFO, "InMemStorageAPI_RemoveDir(%p, %s) failed with %d",
            storage_api, path,
            ENOENT)
        return ENOENT;
    }
    struct PathEntry* path_entry = &instance->m_PathEntries[instance->m_PathHashToContent[source_path_ptr].value];
    if (path_entry->m_Content)
    {
        // Not a directory
        Longtail_UnlockSpinLock(instance->m_SpinLock);
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_INFO, "InMemStorageAPI_RemoveDir(%p, %s) failed with %d",
            storage_api, path,
            EINVAL)
        return EINVAL;
    }
    Longtail_Free(path_entry->m_FileName);
    path_entry->m_FileName = 0;
    arrfree(path_entry->m_Content);
    path_entry->m_Content = 0;
    path_entry->m_ParentHash = 0;
    hmdel(instance->m_PathHashToContent, path_hash);
    Longtail_UnlockSpinLock(instance->m_SpinLock);
    return 0;
}

static int InMemStorageAPI_RemoveFile(struct Longtail_StorageAPI* storage_api, const char* path)
{
    LONGTAIL_VALIDATE_INPUT(storage_api != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(path != 0, return EINVAL);
    struct InMemStorageAPI* instance = (struct InMemStorageAPI*)storage_api;
    uint32_t path_hash = InMemStorageAPI_GetPathHash(path);
    Longtail_LockSpinLock(instance->m_SpinLock);
    intptr_t source_path_ptr = hmgeti(instance->m_PathHashToContent, path_hash);
    if (source_path_ptr == -1)
    {
        Longtail_UnlockSpinLock(instance->m_SpinLock);
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_INFO, "InMemStorageAPI_RemoveFile(%p, %s) failed with %d",
            storage_api, path,
            ENOENT)
        return ENOENT;
    }
    struct PathEntry* path_entry = &instance->m_PathEntries[instance->m_PathHashToContent[source_path_ptr].value];
    if (!path_entry->m_Content)
    {
        // Not a file
        Longtail_UnlockSpinLock(instance->m_SpinLock);
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_INFO, "InMemStorageAPI_RemoveFile(%p, %s) failed with %d",
            storage_api, path,
            EINVAL)
        return EINVAL;
    }
    if (path_entry->m_IsOpenRead || path_entry->m_IsOpenWrite)
    {
        Longtail_UnlockSpinLock(instance->m_SpinLock);
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_INFO, "InMemStorageAPI_RemoveFile(%p, %s) failed with %d",
            storage_api, path,
            EPERM)
        return EPERM;
    }
    Longtail_Free(path_entry->m_FileName);
    path_entry->m_FileName = 0;
    arrfree(path_entry->m_Content);
    path_entry->m_Content = 0;
    path_entry->m_ParentHash = 0;
    hmdel(instance->m_PathHashToContent, path_hash);
    Longtail_UnlockSpinLock(instance->m_SpinLock);
    return 0;
}

static int InMemStorageAPI_StartFind(struct Longtail_StorageAPI* storage_api, const char* path, Longtail_StorageAPI_HIterator* out_iterator)
{
    LONGTAIL_VALIDATE_INPUT(storage_api != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(path != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(out_iterator != 0, return EINVAL);
    struct InMemStorageAPI* instance = (struct InMemStorageAPI*)storage_api;
    uint32_t path_hash = path[0] ? InMemStorageAPI_GetPathHash(path) : 0;
    Longtail_LockSpinLock(instance->m_SpinLock);
    ptrdiff_t* i = (ptrdiff_t*)Longtail_Alloc(sizeof(ptrdiff_t));
    if (!i)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "InMemStorageAPI_StartFind(%p, %s, %p) failed with %d",
            storage_api, path, out_iterator,
            ENOMEM)
        return ENOMEM;
    }
    *i = 0;
    while (*i < arrlen(instance->m_PathEntries))
    {
        if (instance->m_PathEntries[*i].m_ParentHash == path_hash)
        {
            *out_iterator = (Longtail_StorageAPI_HIterator)i;
            return 0;
        }
        *i += 1;
    }
    Longtail_Free(i);
    Longtail_UnlockSpinLock(instance->m_SpinLock);
    return ENOENT;
}

static int InMemStorageAPI_FindNext(struct Longtail_StorageAPI* storage_api, Longtail_StorageAPI_HIterator iterator)
{
    LONGTAIL_VALIDATE_INPUT(storage_api != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(iterator != 0, return EINVAL);
    struct InMemStorageAPI* instance = (struct InMemStorageAPI*)storage_api;
    ptrdiff_t* i = (ptrdiff_t*)iterator;
    uint32_t path_hash = instance->m_PathEntries[*i].m_ParentHash;
    *i += 1;
    while (*i < arrlen(instance->m_PathEntries))
    {
        if (instance->m_PathEntries[*i].m_ParentHash == path_hash)
        {
            return 0;
        }
        *i += 1;
    }
    return ENOENT;
}
static void InMemStorageAPI_CloseFind(struct Longtail_StorageAPI* storage_api, Longtail_StorageAPI_HIterator iterator)
{
    LONGTAIL_VALIDATE_INPUT(storage_api != 0, return);
    LONGTAIL_VALIDATE_INPUT(iterator != 0, return);
    struct InMemStorageAPI* instance = (struct InMemStorageAPI*)storage_api;
    ptrdiff_t* i = (ptrdiff_t*)iterator;
    Longtail_Free(i);
    Longtail_UnlockSpinLock(instance->m_SpinLock);
}

static const char* InMemStorageAPI_GetFileName(struct Longtail_StorageAPI* storage_api, Longtail_StorageAPI_HIterator iterator)
{
    LONGTAIL_VALIDATE_INPUT(storage_api != 0, return 0);
    LONGTAIL_VALIDATE_INPUT(iterator != 0, return 0);
    struct InMemStorageAPI* instance = (struct InMemStorageAPI*)storage_api;
    ptrdiff_t* i = (ptrdiff_t*)iterator;
    if (instance->m_PathEntries[*i].m_Content == 0)
    {
        return 0;
    }
    const char* file_name = instance->m_PathEntries[*i].m_FileName;
    return file_name;
}

static const char* InMemStorageAPI_GetDirectoryName(struct Longtail_StorageAPI* storage_api, Longtail_StorageAPI_HIterator iterator)
{
    LONGTAIL_VALIDATE_INPUT(storage_api != 0, return 0);
    LONGTAIL_VALIDATE_INPUT(iterator != 0, return 0);
    struct InMemStorageAPI* instance = (struct InMemStorageAPI*)storage_api;
    uint32_t* i = (uint32_t*)iterator;
    if (instance->m_PathEntries[*i].m_Content != 0)
    {
        return 0;
    }
    return instance->m_PathEntries[*i].m_FileName;
}

static int InMemStorageAPI_GetEntryProperties(struct Longtail_StorageAPI* storage_api, Longtail_StorageAPI_HIterator iterator, struct Longtail_StorageAPI_EntryProperties* out_properties)
{
    LONGTAIL_VALIDATE_INPUT(storage_api != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(iterator != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(out_properties != 0, return EINVAL);
    struct InMemStorageAPI* instance = (struct InMemStorageAPI*)storage_api;
    uint32_t* i = (uint32_t*)iterator;
    if (instance->m_PathEntries[*i].m_Content == 0)
    {
        out_properties->m_Size = 0;
        out_properties->m_IsDir = 1;
    }
    else
    {
        out_properties->m_Size = (uint64_t)arrlen(instance->m_PathEntries[*i].m_Content);
        out_properties->m_IsDir = 0;
    }
    out_properties->m_Permissions = instance->m_PathEntries[*i].m_Permissions;
    out_properties->m_Name = instance->m_PathEntries[*i].m_FileName;
    return 0;
}

static int InMemStorageAPI_LockFile(struct Longtail_StorageAPI* storage_api, const char* path, Longtail_StorageAPI_HLockFile* out_lock_file)
{
    struct InMemStorageAPI* instance = (struct InMemStorageAPI*)storage_api;
    uint32_t path_hash = InMemStorageAPI_GetPathHash(path);
    uint32_t parent_path_hash = InMemStorageAPI_GetParentPathHash(path);
    Longtail_LockSpinLock(instance->m_SpinLock);
    if (parent_path_hash != 0 && hmgeti(instance->m_PathHashToContent, parent_path_hash) == -1)
    {
        Longtail_UnlockSpinLock(instance->m_SpinLock);
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_INFO, "InMemStorageAPI_LockFile(%p, %s, %p) failed with %d",
            storage_api, path, out_lock_file,
            ENOENT)
        return ENOENT;
    }
    struct PathEntry* path_entry = 0;
    intptr_t it = hmgeti(instance->m_PathHashToContent, path_hash);

    int try_count = 50;
    uint64_t retry_delay = 1000;
    uint64_t total_delay = 0;

    while (it != -1)
    {
        Longtail_UnlockSpinLock(instance->m_SpinLock);
        if (--try_count == 0)
        {
            LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_INFO, "InMemStorageAPI_LockFile(%p, %s, %p) failed with %d, waited %f seconds",
                storage_api, path, out_lock_file,
                EACCES,
                (total_delay / 1000) / 1000.f)
            return EACCES;
        }
        Longtail_Sleep(retry_delay);
        total_delay += retry_delay;

        Longtail_LockSpinLock(instance->m_SpinLock);
        it = hmgeti(instance->m_PathHashToContent, path_hash);
        retry_delay += 2000;
    }
    ptrdiff_t entry_index = arrlen(instance->m_PathEntries);
    arrsetlen(instance->m_PathEntries, (size_t)(entry_index + 1));
    path_entry = &instance->m_PathEntries[entry_index];
    path_entry->m_ParentHash = parent_path_hash;
    path_entry->m_FileName = Longtail_Strdup(InMemStorageAPI_GetFileNamePart(path));
    path_entry->m_Content = 0;
    path_entry->m_Permissions = 0644;
    path_entry->m_IsOpenRead = 0;
    path_entry->m_IsOpenWrite = 2;
    hmput(instance->m_PathHashToContent, path_hash, (uint32_t)entry_index);

    arrsetcap(path_entry->m_Content, 16);
    Longtail_UnlockSpinLock(instance->m_SpinLock);
    *out_lock_file = (Longtail_StorageAPI_HLockFile)(uintptr_t)path_hash;
    return 0;
}

static int InMemStorageAPI_UnlockFile(struct Longtail_StorageAPI* storage_api, Longtail_StorageAPI_HLockFile lock_file)
{
    struct InMemStorageAPI* instance = (struct InMemStorageAPI*)storage_api;
    uint32_t path_hash = (uint32_t)(uintptr_t)lock_file;
    Longtail_LockSpinLock(instance->m_SpinLock);
    intptr_t it = hmgeti(instance->m_PathHashToContent, path_hash);
    if (it == -1)
    {
        Longtail_UnlockSpinLock(instance->m_SpinLock);
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "InMemStorageAPI_UnlockFile(%p, %p) failed with %d",
            storage_api, lock_file,
            EINVAL)
    }
    struct PathEntry* path_entry = &instance->m_PathEntries[instance->m_PathHashToContent[it].value];
    if (path_entry->m_IsOpenRead > 0)
    {
        Longtail_UnlockSpinLock(instance->m_SpinLock);
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "InMemStorageAPI_UnlockFile(%p, %p) failed with %d",
            storage_api, lock_file,
            EINVAL)
        return EINVAL;
    }
    if (path_entry->m_IsOpenWrite != 2)
    {
        Longtail_UnlockSpinLock(instance->m_SpinLock);
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "InMemStorageAPI_UnlockFile(%p, %p) failed with %d",
            storage_api, lock_file,
            EINVAL)
        return EINVAL;
    }
    Longtail_Free(path_entry->m_FileName);
    path_entry->m_FileName = 0;
    arrfree(path_entry->m_Content);
    path_entry->m_Content = 0;
    path_entry->m_ParentHash = 0;
    hmdel(instance->m_PathHashToContent, path_hash);

    Longtail_UnlockSpinLock(instance->m_SpinLock);
    return 0;
}

static int InMemStorageAPI_Init(
    void* mem,
    struct Longtail_StorageAPI** out_storage_api)
{
    LONGTAIL_VALIDATE_INPUT(mem != 0, return 0);
    struct Longtail_StorageAPI* api = Longtail_MakeStorageAPI(
        mem,
        InMemStorageAPI_Dispose,
        InMemStorageAPI_OpenReadFile,
        InMemStorageAPI_GetSize,
        InMemStorageAPI_Read,
        InMemStorageAPI_OpenWriteFile,
        InMemStorageAPI_Write,
        InMemStorageAPI_SetSize,
        InMemStorageAPI_SetPermissions,
        InMemStorageAPI_GetPermissions,
        InMemStorageAPI_CloseFile,
        InMemStorageAPI_CreateDir,
        InMemStorageAPI_RenameFile,
        InMemStorageAPI_ConcatPath,
        InMemStorageAPI_IsDir,
        InMemStorageAPI_IsFile,
        InMemStorageAPI_RemoveDir,
        InMemStorageAPI_RemoveFile,
        InMemStorageAPI_StartFind,
        InMemStorageAPI_FindNext,
        InMemStorageAPI_CloseFind,
        InMemStorageAPI_GetEntryProperties,
        InMemStorageAPI_LockFile,
        InMemStorageAPI_UnlockFile);

    struct InMemStorageAPI* storage_api = (struct InMemStorageAPI*)api;

    storage_api->m_PathHashToContent = 0;
    storage_api->m_PathEntries = 0;
    int err = Longtail_CreateSpinLock(&storage_api[1], &storage_api->m_SpinLock);
    if (err)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "InMemStorageAPI_Init(%p) failed with %d",
            storage_api,
            err)
        return err;
    }
    *out_storage_api = api;
    return 0;
}

struct Longtail_StorageAPI* Longtail_CreateInMemStorageAPI()
{
    void* mem = (struct InMemStorageAPI*)Longtail_Alloc(sizeof(struct InMemStorageAPI) + Longtail_GetSpinLockSize());
    if (!mem)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "Longtail_CreateInMemStorageAPI() failed with %d",
            ENOMEM)
        return 0;
    }
    struct Longtail_StorageAPI* storage_api;
    int err = InMemStorageAPI_Init(mem, &storage_api);
    if (err)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "Longtail_CreateInMemStorageAPI() failed with %d",
            err)
        Longtail_Free(storage_api);
        return 0;
    }
    return storage_api;
}
