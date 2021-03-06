#include "longtail_filestorage.h"

#include "../longtail_platform.h"
#include <inttypes.h>
#include <errno.h>

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

#include <string.h>

struct FSStorageAPI
{
    struct Longtail_StorageAPI m_FSStorageAPI;
};

static void FSStorageAPI_Dispose(struct Longtail_API* storage_api)
{
    LONGTAIL_FATAL_ASSERT(storage_api != 0, return);
    Longtail_Free(storage_api);
}

#define TMP_STR(str) \
    size_t len_##str = strlen(str); \
    char* tmp_##str = (char*)alloca(len_##str + 1); \
    memmove(tmp_##str, str, len_##str); \
    tmp_##str[len_##str] = '\0';

static int FSStorageAPI_OpenReadFile(struct Longtail_StorageAPI* storage_api, const char* path, Longtail_StorageAPI_HOpenFile* out_open_file)
{
    LONGTAIL_VALIDATE_INPUT(storage_api != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(path != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(out_open_file != 0, return EINVAL);

    TMP_STR(path)
    Longtail_DenormalizePath(tmp_path);
    HLongtail_OpenFile r;
    int err = Longtail_OpenReadFile(tmp_path, &r);
    if (err != 0)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_INFO, "FSStorageAPI_OpenReadFile(%p, %s, %p) failed with %d",
            storage_api, path, out_open_file,
            err)
        return err;
    }
    *out_open_file = (Longtail_StorageAPI_HOpenFile)r;
    return 0;
}

static int FSStorageAPI_GetSize(struct Longtail_StorageAPI* storage_api, Longtail_StorageAPI_HOpenFile f, uint64_t* out_size)
{
    LONGTAIL_VALIDATE_INPUT(storage_api != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(f != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(out_size != 0, return EINVAL);
    int err = Longtail_GetFileSize((HLongtail_OpenFile)f, out_size);
    if (err)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "FSStorageAPI_GetSize(%p, %p, %p) failed with %d",
            storage_api, f, out_size,
            err)
        return err;
    }
    return 0;
}

static int FSStorageAPI_Read(struct Longtail_StorageAPI* storage_api, Longtail_StorageAPI_HOpenFile f, uint64_t offset, uint64_t length, void* output)
{
    LONGTAIL_VALIDATE_INPUT(storage_api != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(f != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(output != 0, return EINVAL);
    int err = Longtail_Read((HLongtail_OpenFile)f, offset,length, output);
    if (err)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "FSStorageAPI_Read(%p, %p, %" PRIu64 ", %" PRIu64 ", %p) failed with %d",
            storage_api, f, offset, length, output,
            err)
        return err;
    }
    return 0;
}

static int FSStorageAPI_OpenWriteFile(struct Longtail_StorageAPI* storage_api, const char* path, uint64_t initial_size, Longtail_StorageAPI_HOpenFile* out_open_file)
{
    LONGTAIL_VALIDATE_INPUT(storage_api != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(path != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(out_open_file != 0, return EINVAL);
    TMP_STR(path)
    Longtail_DenormalizePath(tmp_path);
    HLongtail_OpenFile r;
    int err = Longtail_OpenWriteFile(tmp_path, initial_size, &r);
    if (err)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_INFO, "FSStorageAPI_OpenWriteFile(%p, %s, %" PRIu64 ", %p) failed with %d",
            storage_api, path, initial_size, out_open_file,
            err)
        return err;
    }
    *out_open_file = (Longtail_StorageAPI_HOpenFile)r;
    return 0;
}

static int FSStorageAPI_Write(struct Longtail_StorageAPI* storage_api, Longtail_StorageAPI_HOpenFile f, uint64_t offset, uint64_t length, const void* input)
{
    LONGTAIL_VALIDATE_INPUT(storage_api != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(f != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(input != 0, return EINVAL);
    int err = Longtail_Write((HLongtail_OpenFile)f, offset,length, input);
    if (err)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "FSStorageAPI_Write(%p, %p, %" PRIu64 ", %" PRIu64 ", %p) failed with %d",
            storage_api, f, offset, length, input,
            err)
        return 0;
    }
    return err;
}

static int FSStorageAPI_SetSize(struct Longtail_StorageAPI* storage_api, Longtail_StorageAPI_HOpenFile f, uint64_t length)
{
    LONGTAIL_VALIDATE_INPUT(storage_api != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(f != 0, return EINVAL);
    int err = Longtail_SetFileSize((HLongtail_OpenFile)f, length);
    if (err)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "FSStorageAPI_SetSize(%p, %p, %" PRIu64 ") failed with %d",
            storage_api, f, length,
            err)
        return err;
    }
    return 0;
}

static int FSStorageAPI_SetPermissions(struct Longtail_StorageAPI* storage_api, const char* path, uint16_t permissions)
{
    LONGTAIL_VALIDATE_INPUT(storage_api != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(path != 0, return EINVAL);
    TMP_STR(path)
    Longtail_DenormalizePath(tmp_path);
    int err = Longtail_SetFilePermissions(tmp_path, permissions);
    if (err)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_INFO, "FSStorageAPI_SetPermissions(%p, %p, %u) failed with %d",
            storage_api, path, permissions,
            err)
        return err;
    }
    return 0;
}

static int FSStorageAPI_GetPermissions(struct Longtail_StorageAPI* storage_api, const char* path, uint16_t* out_permissions)
{
    LONGTAIL_VALIDATE_INPUT(storage_api != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(path != 0, return EINVAL);
    TMP_STR(path)
    Longtail_DenormalizePath(tmp_path);
    int err = Longtail_GetFilePermissions(tmp_path, out_permissions);
    if (err == ENOENT)
    {
        return err;
    }
    if (err)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_INFO, "FSStorageAPI_GetPermissions(%p, %p, %u) failed with %d",
            storage_api, path, out_permissions,
            err)
        return err;
    }
    return 0;
}

static void FSStorageAPI_CloseFile(struct Longtail_StorageAPI* storage_api, Longtail_StorageAPI_HOpenFile f)
{
    LONGTAIL_VALIDATE_INPUT(storage_api != 0, return);
    Longtail_CloseFile((HLongtail_OpenFile)f);
}

static int FSStorageAPI_CreateDir(struct Longtail_StorageAPI* storage_api, const char* path)
{
    LONGTAIL_VALIDATE_INPUT(storage_api != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(path != 0, return EINVAL);
    TMP_STR(path)
    Longtail_DenormalizePath(tmp_path);
    int err = Longtail_CreateDirectory(tmp_path);
    if (err)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_INFO, "FSStorageAPI_CreateDir(%p, %s) failed with %d",
            storage_api, path,
            err)
        return err;
    }
    return 0;
}

static int FSStorageAPI_RenameFile(struct Longtail_StorageAPI* storage_api, const char* source_path, const char* target_path)
{
    LONGTAIL_VALIDATE_INPUT(storage_api != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(source_path != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(target_path != 0, return EINVAL);
    TMP_STR(source_path)
    TMP_STR(target_path)
    Longtail_DenormalizePath(tmp_source_path);
    Longtail_DenormalizePath(tmp_target_path);
    int err = Longtail_MoveFile(tmp_source_path, tmp_target_path);
    if (err)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_INFO, "Longtail_MoveFile(%p, %s, %s) failed with %d",
            storage_api, source_path, target_path,
            err)
        return err;
    }
    return 0;
}

static char* FSStorageAPI_ConcatPath(struct Longtail_StorageAPI* storage_api, const char* root_path, const char* sub_path)
{
    LONGTAIL_VALIDATE_INPUT(storage_api != 0, return 0);
    LONGTAIL_VALIDATE_INPUT(root_path != 0, return 0);
    LONGTAIL_VALIDATE_INPUT(sub_path != 0, return 0);
    TMP_STR(root_path)
    Longtail_DenormalizePath(tmp_root_path);
    TMP_STR(sub_path)
    Longtail_DenormalizePath(tmp_sub_path);
    char* path = (char*)Longtail_ConcatPath(tmp_root_path, tmp_sub_path);
    if (!path)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "FSStorageAPI_ConcatPath(%p, %s, %s) failed with %d",
            storage_api, root_path, sub_path,
            ENOMEM)
        return 0;
    }
    Longtail_NormalizePath(path);
    return path;
}

static int FSStorageAPI_IsDir(struct Longtail_StorageAPI* storage_api, const char* path)
{
    LONGTAIL_VALIDATE_INPUT(storage_api != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(path != 0, return EINVAL);
    TMP_STR(path)
    Longtail_DenormalizePath(tmp_path);
    int is_dir = Longtail_IsDir(tmp_path);
    return is_dir;
}

static int FSStorageAPI_IsFile(struct Longtail_StorageAPI* storage_api, const char* path)
{
    LONGTAIL_VALIDATE_INPUT(storage_api != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(path != 0, return EINVAL);
    TMP_STR(path)
    Longtail_DenormalizePath(tmp_path);
    int is_file = Longtail_IsFile(tmp_path);
    return is_file;
}

static int FSStorageAPI_RemoveDir(struct Longtail_StorageAPI* storage_api, const char* path)
{
    LONGTAIL_VALIDATE_INPUT(storage_api != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(path != 0, return EINVAL);
    TMP_STR(path)
    Longtail_DenormalizePath(tmp_path);
    int err = Longtail_RemoveDir(tmp_path);
    if (err)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_INFO, "FSStorageAPI_RemoveDir(%p, %s) failed with %d",
            storage_api, path,
            err)
        return err;
    }
    return 0;
}

static int FSStorageAPI_RemoveFile(struct Longtail_StorageAPI* storage_api, const char* path)
{
    LONGTAIL_VALIDATE_INPUT(storage_api != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(path != 0, return EINVAL);
    TMP_STR(path)
    Longtail_DenormalizePath(tmp_path);
    int err = Longtail_RemoveFile(tmp_path);
    if (err)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_INFO, "Longtail_RemoveFile(%p, %s) failed with %d",
            storage_api, path,
            err)
        return err;
    }
    return 0;
}

static int FSStorageAPI_StartFind(struct Longtail_StorageAPI* storage_api, const char* path, Longtail_StorageAPI_HIterator* out_iterator)
{
    LONGTAIL_VALIDATE_INPUT(storage_api != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(path != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(out_iterator != 0, return EINVAL);
    Longtail_StorageAPI_HIterator iterator = (Longtail_StorageAPI_HIterator)Longtail_Alloc(Longtail_GetFSIteratorSize());
    TMP_STR(path)
    Longtail_DenormalizePath(tmp_path);
    int err = Longtail_StartFind((HLongtail_FSIterator)iterator, tmp_path);
    if (err == ENOENT)
    {
        Longtail_Free(iterator);
        return err;
    }
    if (err)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_INFO, "FSStorageAPI_StartFind(%p, %s, %p) failed with %d",
            storage_api, path, out_iterator,
            err)
        Longtail_Free(iterator);
        return err;
    }
    *out_iterator = iterator;
    return 0;
}

static int FSStorageAPI_FindNext(struct Longtail_StorageAPI* storage_api, Longtail_StorageAPI_HIterator iterator)
{
    LONGTAIL_VALIDATE_INPUT(storage_api != 0, return EINVAL);
    LONGTAIL_VALIDATE_INPUT(iterator != 0, return EINVAL);
    int err = Longtail_FindNext((HLongtail_FSIterator)iterator);
    if (err && (err != ENOENT))
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_INFO, "FSStorageAPI_FindNext(%p, %p) failed with %d",
            storage_api, iterator,
            err)
    }
    return err;
}

static void FSStorageAPI_CloseFind(struct Longtail_StorageAPI* storage_api, Longtail_StorageAPI_HIterator iterator)
{
    LONGTAIL_VALIDATE_INPUT(storage_api != 0, return );
    LONGTAIL_VALIDATE_INPUT(iterator != 0, return );
    Longtail_CloseFind((HLongtail_FSIterator)iterator);
    Longtail_Free(iterator);
}

static int FSStorageAPI_GetEntryProperties(struct Longtail_StorageAPI* storage_api, Longtail_StorageAPI_HIterator iterator, struct Longtail_StorageAPI_EntryProperties* out_properties)
{
    LONGTAIL_FATAL_ASSERT(storage_api != 0, return EINVAL);
    LONGTAIL_FATAL_ASSERT(iterator != 0, return EINVAL);
    LONGTAIL_FATAL_ASSERT(out_properties != 0, return EINVAL);
    int err = Longtail_GetEntryProperties((HLongtail_FSIterator)iterator, &out_properties->m_Size, &out_properties->m_Permissions, &out_properties->m_IsDir);
    if (err)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_INFO, "FSStorageAPI_GetEntryProperties(%p, %p, %p) failed with %d",
            storage_api, iterator, out_properties,
            err)
        return err;
    }
    out_properties->m_Name = (out_properties->m_IsDir) ? Longtail_GetDirectoryName((HLongtail_FSIterator)iterator) : Longtail_GetFileName((HLongtail_FSIterator)iterator);
    return 0;
}

static int FSStorageAPI_LockFile(struct Longtail_StorageAPI* storage_api, const char* path, Longtail_StorageAPI_HLockFile* out_lock_file)
{
    void* mem = Longtail_Alloc(Longtail_GetFileLockSize());
    if (!mem)
    {
        return ENOMEM;
    }
    HLongtail_FileLock file_lock;
    int err = Longtail_LockFile(mem, path, &file_lock);
    if (err)
    {
        Longtail_Free(mem);
        return err;
    }
    *out_lock_file = (Longtail_StorageAPI_HLockFile)file_lock;
    return 0;
}

static int FSStorageAPI_UnlockFile(struct Longtail_StorageAPI* storage_api, Longtail_StorageAPI_HLockFile lock_file)
{
    int err = Longtail_UnlockFile((HLongtail_FileLock)lock_file);
    if (err)
    {
        return err;
    }
    Longtail_Free(lock_file);
    return 0;
}

static int FSStorageAPI_Init(
    void* mem,
    struct Longtail_StorageAPI** out_storage_api)
{
    LONGTAIL_VALIDATE_INPUT(mem != 0, return 0);
    struct Longtail_StorageAPI* api = Longtail_MakeStorageAPI(
        mem,
        FSStorageAPI_Dispose,
        FSStorageAPI_OpenReadFile,
        FSStorageAPI_GetSize,
        FSStorageAPI_Read,
        FSStorageAPI_OpenWriteFile,
        FSStorageAPI_Write,
        FSStorageAPI_SetSize,
        FSStorageAPI_SetPermissions,
        FSStorageAPI_GetPermissions,
        FSStorageAPI_CloseFile,
        FSStorageAPI_CreateDir,
        FSStorageAPI_RenameFile,
        FSStorageAPI_ConcatPath,
        FSStorageAPI_IsDir,
        FSStorageAPI_IsFile,
        FSStorageAPI_RemoveDir,
        FSStorageAPI_RemoveFile,
        FSStorageAPI_StartFind,
        FSStorageAPI_FindNext,
        FSStorageAPI_CloseFind,
        FSStorageAPI_GetEntryProperties,
        FSStorageAPI_LockFile,
        FSStorageAPI_UnlockFile);
    *out_storage_api = api;
    return 0;
}


struct Longtail_StorageAPI* Longtail_CreateFSStorageAPI()
{
    void* mem = (struct FSStorageAPI*)Longtail_Alloc(sizeof(struct FSStorageAPI));
    if (!mem)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "Longtail_CreateFSStorageAPI() failed with %d",
            ENOMEM)
        return 0;
    }
    struct Longtail_StorageAPI* storage_api;
    int err = FSStorageAPI_Init(mem, &storage_api);
    if (err)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "Longtail_CreateFSStorageAPI() failed with %d",
            err)
        return 0;
    }
    return storage_api;
}

