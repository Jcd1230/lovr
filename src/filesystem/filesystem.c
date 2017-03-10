#include "filesystem/filesystem.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <initguid.h>
#include <KnownFolders.h>
#include <ShlObj.h>
#include <wchar.h>
#define S_ISREG(mode)  (((mode) & S_IFMT) == S_IFREG)
#define S_ISDIR(mode)  (((mode) & S_IFMT) == S_IFDIR)
#else
#include <pwd.h>
#include <unistd.h>
#include <errno.h>
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#define FOREACH_ARCHIVE(archives) Archive* archive; int i; vec_foreach(archives, archive, i)

static FilesystemState state;

static int mkdir_p(const char* path) {
  char tmp[LOVR_PATH_MAX];
  strncpy(tmp, path, LOVR_PATH_MAX);
  for (char* p = tmp + 1; *p; p++) {
    if (*p == '/') {
      *p = '\0';
#if _WIN32
      CreateDirectory(tmp, NULL);
#else
      mkdir(tmp, 0700);
#endif
      *p = '/';
    }
  }

  mkdir(path, 0700);
  return access(path, 0);
}

static void pathJoin(char* dest, const char* p1, const char* p2) {
  snprintf(dest, LOVR_PATH_MAX, "%s/%s", p1, p2);
}

// fs

static int fsExists(Archive* archive, const char* path) {
  char fullpath[LOVR_PATH_MAX];
  pathJoin(fullpath, archive->path, path);
  return !access(fullpath, 0);
}

static int fsIsDirectory(Archive* archive, const char* path) {
  char fullpath[LOVR_PATH_MAX];
  pathJoin(fullpath, archive->path, path);

  struct stat st;
  return !stat(fullpath, &st) && S_ISDIR(st.st_mode);
}

static int fsIsFile(Archive* archive, const char* path) {
  char fullpath[LOVR_PATH_MAX];
  pathJoin(fullpath, archive->path, path);

  struct stat st;
  return !stat(fullpath, &st) && S_ISREG(st.st_mode);
}

static void* fsRead(Archive* archive, const char* path, size_t* bytesRead) {
  char fullpath[LOVR_PATH_MAX];
  pathJoin(fullpath, archive->path, path);
  FILE* file = fopen(fullpath, "r");
  if (!file) return NULL;

  fseek(file, 0, SEEK_END);
  size_t size = ftell(file);
  rewind(file);

  char* data = malloc(size);
  if (!data) {
    fclose(file);
    return NULL;
  }

  *bytesRead = fread(data, sizeof(char), size, file);
  fclose(file);
  return data;
}

static void fsUnmount(Archive* archive) {
  free(archive);
}

static Archive* fsInit(const char* path) {
  struct stat st;
  if (stat(path, &st) || !S_ISDIR(st.st_mode)) {
    return NULL;
  }

  Archive* archive = malloc(sizeof(Archive));
  if (!archive) return NULL;

  archive->type = ARCHIVE_FS;
  archive->path = path;
  archive->userdata = NULL;
  archive->exists = fsExists;
  archive->isDirectory = fsIsDirectory;
  archive->isFile = fsIsFile;
  archive->read = fsRead;
  archive->unmount = fsUnmount;
  return archive;
}

// tar

static int tarLoad(Archive* archive, const char* path, mtar_header_t* header) {
  TarArchive* tar = archive->userdata;
  int* pos = map_get(&tar->entries, path);
  if (!pos) {
    return 1;
  }

  mtar_seek(&tar->mtar, (unsigned) *pos);
  return mtar_read_header(&tar->mtar, header);
}

static int tarExists(Archive* archive, const char* path) {
  TarArchive* tar = archive->userdata;
  return map_get(&tar->entries, path) != NULL;
}

static int tarIsDirectory(Archive* archive, const char* path) {
  mtar_header_t header;
  return !tarLoad(archive, path, &header) && header.type == MTAR_TDIR;
}

static int tarIsFile(Archive* archive, const char* path) {
  mtar_header_t header;
  return !tarLoad(archive, path, &header) && header.type == MTAR_TREG;
}

static void* tarRead(Archive* archive, const char* path, size_t* bytesRead) {
  TarArchive* tar = archive->userdata;
  mtar_header_t header;
  if (tarLoad(archive, path, &header)) {
    *bytesRead = 0;
    return NULL;
  }

  char* data = calloc(1, header.size + 1);
  if (mtar_read_data(&tar->mtar, data, header.size)) {
    *bytesRead = 0;
    free(data);
    return NULL;
  }

  *bytesRead = header.size;
  return data;
}

static void tarUnmount(Archive* archive) {
  TarArchive* tar = archive->userdata;
  fclose(tar->file);
  mtar_close(&tar->mtar);
  map_deinit(&tar->entries);
  free(tar);
  free(archive);
}

static int tarStreamRead(mtar_t* mtar, void* data, unsigned size) {
  TarArchive* tar = mtar->stream;
  unsigned res = fread(data, 1, size, tar->file);
  return (res == size) ? MTAR_ESUCCESS : MTAR_EREADFAIL;
}

static int tarStreamSeek(mtar_t* mtar, unsigned pos) {
  TarArchive* tar = mtar->stream;
  int err = fseek(tar->file, tar->offset + pos, SEEK_SET);
  return err ? MTAR_EFAILURE : MTAR_ESUCCESS;
}

static int tarStreamClose(mtar_t* mtar) {
  TarArchive* tar = mtar->stream;
  fclose(tar->file);
  return MTAR_ESUCCESS;
}

static Archive* tarInit(const char* path) {
  TarArchive* tar = malloc(sizeof(TarArchive));
  if (!tar) return NULL;

  tar->offset = 0;
  tar->file = fopen(path, "rb");
  if (!tar->file) {
    return NULL;
  }

  // Initialize microtar
  memset(&tar->mtar, 0, sizeof(mtar_t));
  tar->mtar.stream = tar;
  tar->mtar.read = tarStreamRead;
  tar->mtar.seek = tarStreamSeek;
  tar->mtar.close = tarStreamClose;

  // If the beginning of the file does not have a header, check end of file for offset
  mtar_header_t header;
  if (mtar_read_header(&tar->mtar, &header)) {
    int offset;
    char buf[4];
    fseek(tar->file, -8, SEEK_END);
    fread(buf, 1, 4, tar->file);
    fread(&offset, 1, 4, tar->file);
    if (!memcmp(buf, "TAR\0", 4)) {
      fseek(tar->file, -offset, SEEK_END);
      tar->offset = ftell(tar->file);
    }
    mtar_rewind(&tar->mtar);

    // If there still isn't a valid header then this isn't a valid archive
    if (mtar_read_header(&tar->mtar, &header)) {
      fclose(tar->file);
      free(tar);
      return NULL;
    }

    state.isFused = 1;
  }

  // Read all entries in the archive
  map_init(&tar->entries);
  while ((mtar_read_header(&tar->mtar, &header)) != MTAR_ENULLRECORD) {
    map_set(&tar->entries, header.name, tar->mtar.pos);
    mtar_next(&tar->mtar);
  }

  Archive* archive = malloc(sizeof(Archive));
  if (!archive) {
    fclose(tar->file);
    mtar_close(&tar->mtar);
    map_deinit(&tar->entries);
    free(tar);
    return NULL;
  }

  archive->type = ARCHIVE_TAR;
  archive->path = path;
  archive->userdata = tar;
  archive->exists = tarExists;
  archive->isDirectory = tarIsDirectory;
  archive->isFile = tarIsFile;
  archive->read = tarRead;
  archive->unmount = tarUnmount;
  return archive;
}

// lovr.filesystem

void lovrFilesystemInit() {
  state.isFused = 0;
  vec_init(&state.archives);
  state.writePath = NULL;
  state.identity = NULL;
  atexit(lovrFilesystemDestroy);
}

void lovrFilesystemDestroy() {
  FOREACH_ARCHIVE(&state.archives) {
    archive->unmount(archive);
  }
  vec_deinit(&state.archives);
  free(state.writePath);
}

int lovrFilesystemCreateDirectory(const char* path) {
  if (!state.writePath) {
    return 1;
  }

  char fullpath[LOVR_PATH_MAX];
  pathJoin(fullpath, state.writePath, path);
  return mkdir_p(fullpath);
}

int lovrFilesystemExists(const char* path) {
  FOREACH_ARCHIVE(&state.archives) {
    if (archive->exists(archive, path)) {
      return 1;
    }
  }

  return 0;
}

int lovrFilesystemGetAppdataDirectory(char* dest, unsigned int size) {
#ifdef __APPLE__
  const char* home;
  if ((home = getenv("HOME")) == NULL) {
    home = getpwuid(getuid())->pw_dir;
  }

  snprintf(dest, size, "%s/Library/Application Support", home);
  return 0;
#elif _WIN32
  PWSTR appData = NULL;
  SHGetKnownFolderPath(&FOLDERID_RoamingAppData, 0, NULL, &appData);
  wcstombs(dest, appData, size);
  CoTaskMemFree(appData);
  return 0;
#else
#error "This platform is missing an implementation for lovrFilesystemGetAppdataDirectory"
#endif

  return 1;
}

int lovrFilesystemGetExecutablePath(char* dest, unsigned int size) {
#ifdef __APPLE__
  if (_NSGetExecutablePath(dest, &size) == 0) {
    return 0;
  }
#elif _WIN32
  return !GetModuleFileName(NULL, dest, size);
#else
#error "This platform is missing an implementation for lovrFilesystemGetExecutablePath"
#endif

  return 1;
}

const char* lovrFilesystemGetIdentity() {
  return state.identity;
}

const char* lovrFilesystemGetSaveDirectory() {
  return state.writePath;
}

int lovrFilesystemIsDirectory(const char* path) {
  FOREACH_ARCHIVE(&state.archives) {
    if (archive->isDirectory(archive, path)) {
      return 1;
    }
  }

  return 0;
}

int lovrFilesystemIsFile(const char* path) {
  FOREACH_ARCHIVE(&state.archives) {
    if (archive->isFile(archive, path)) {
      return 1;
    }
  }

  return 0;
}

int lovrFilesystemMount(const char* path, int append) {
  FOREACH_ARCHIVE(&state.archives) {
    if (!strncmp(archive->path, path, LOVR_PATH_MAX)) {
      return 1;
    }
  }

  archive = NULL; // FOREACH_ARCHIVE defines this
  if ((archive = fsInit(path)) != NULL || (archive = tarInit(path)) != NULL) {
    if (append) {
      vec_push(&state.archives, archive);
    } else {
      vec_insert(&state.archives, 0, archive);
    }
  }

  return !archive;
}

void* lovrFilesystemRead(const char* path, size_t* bytesRead) {
  void* data;

  FOREACH_ARCHIVE(&state.archives) {
    if ((data = archive->read(archive, path, bytesRead)) != NULL) {
      return data;
    }
  }

  *bytesRead = 0;
  return NULL;
}

int lovrFilesystemRemove(const char* path) {
  if (!state.writePath) {
    return 1;
  }

  char fullpath[LOVR_PATH_MAX];
  pathJoin(fullpath, state.writePath, path);
  return remove(fullpath);
}

void lovrFilesystemSetIdentity(const char* identity) {
  state.identity = identity;

  if (state.writePath) {
    lovrFilesystemUnmount(state.writePath);
  } else {
    state.writePath = malloc(LOVR_PATH_MAX * sizeof(char));
    if (!state.writePath) {
      error("Unable to allocate memory for save directory");
    }
  }

  int err = 0;
  err |= lovrFilesystemGetAppdataDirectory(state.writePath, LOVR_PATH_MAX);
  err |= snprintf(state.writePath, LOVR_PATH_MAX, "%s/LOVR/%s", state.writePath, identity) >= LOVR_PATH_MAX;
  err |= mkdir_p(state.writePath);
  err |= lovrFilesystemMount(state.writePath, 0);

  if (err) {
    error("Unable to create save directory");
  }
}

int lovrFilesystemUnmount(const char* path) {
  FOREACH_ARCHIVE(&state.archives) {
    if (!strncmp(archive->path, path, LOVR_PATH_MAX)) {
      archive->unmount(archive);
      vec_splice(&state.archives, i, 1);
      return 1;
    }
  }

  return 0;
}

size_t lovrFilesystemWrite(const char* path, const char* content, size_t size, int append) {
  if (!state.writePath) {
    return 0;
  }

  char fullpath[LOVR_PATH_MAX];
  pathJoin(fullpath, state.writePath, path);
  FILE* file = fopen(fullpath, append ? "a" : "w");
  if (!file) {
    return 0;
  }

  size_t bytesWritten = fwrite(content, sizeof(char), size, file);
  fclose(file);
  return bytesWritten;
}
