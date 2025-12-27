#include <cstring>
#include <thread>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cinttypes>
#include <dlfcn.h>
#include "hack.h"
#include "zygisk.hpp"
#include "game.h"
#include "log.h"

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;

// ====================== 新增：海报替换核心定义 + Hook函数 ======================
#define POSTER_DIR_SUFFIX "/files/Poster/"
static FILE* (*old_fopen)(const char* path, const char* mode);
static char* g_poster_dir = nullptr;

// Hook fopen实现海报替换（适配正式服+体验服）
static FILE* my_fopen(const char* path, const char* mode) {
    if (!old_fopen) old_fopen = (decltype(old_fopen))dlsym(RTLD_NEXT, "fopen");
    // 匹配海报/启动图文件 + 海报目录存在
    if (g_poster_dir && access(g_poster_dir, F_OK) == 0 && (strstr(path, "poster") || strstr(path, "launch_image"))) {
        const char* name = strrchr(path, '/');
        if (name) {
            char custom_path[512] = {0};
            snprintf(custom_path, sizeof(custom_path), "%s%s", g_poster_dir, name + 1);
            if (access(custom_path, F_OK) == 0) {
                LOGI("Load custom poster: %s", custom_path);
                return old_fopen(custom_path, mode);
            }
        }
    }
    return old_fopen(path, mode);
}
// ==============================================================================

class MyModule : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        auto package_name = env->GetStringUTFChars(args->nice_name, nullptr);
        auto app_data_dir = env->GetStringUTFChars(args->app_data_dir, nullptr);
        preSpecialize(package_name, app_data_dir);
        env->ReleaseStringUTFChars(args->nice_name, package_name);
        env->ReleaseStringUTFChars(args->app_data_dir, app_data_dir);
    }

    void postAppSpecialize(const AppSpecializeArgs *) override {
        if (enable_hack) {
            std::thread hack_thread(hack_prepare, game_data_dir, data, length);
            hack_thread.detach();
        }
    }

private:
    Api *api;
    JNIEnv *env;
    bool enable_hack;
    char *game_data_dir;
    void *data;
    size_t length;

    void preSpecialize(const char *package_name, const char *app_data_dir) {
        // 原生逻辑：匹配体验服+正式服包名（GamePackageName/GamePackageNameCe）
        if (strcmp(package_name, GamePackageName) == 0 ||
            strcmp(package_name, GamePackageNameCe) == 0) {
            bool skip_hook = false;

            if (strcmp(package_name, GamePackageName) == 0) {
                int dirfd = api->getModuleDir();
                skip_hook = (openat(dirfd, "skipzsf", O_RDONLY) != -1);
            } else if (strcmp(package_name, GamePackageNameCe) == 0) {
                int dirfd = api->getModuleDir();
                skip_hook = (openat(dirfd, "skiptyf", O_RDONLY) != -1);
            }

            if (skip_hook) {
                LOGI("skip hook for game: %s", package_name);
                api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
                return;
            }

            LOGI("detect game: %s", package_name);
            enable_hack = true;
            game_data_dir = new char[strlen(app_data_dir) + 1];
            strcpy(game_data_dir, app_data_dir);

            // ====================== 新增：初始化海报目录 + Hook fopen ======================
            g_poster_dir = new char[strlen(app_data_dir) + strlen(POSTER_DIR_SUFFIX) + 1];
            snprintf(g_poster_dir, strlen(app_data_dir) + strlen(POSTER_DIR_SUFFIX) + 1, "%s%s", app_data_dir, POSTER_DIR_SUFFIX);
            api->hookFunction((void *)fopen, (void *)my_fopen, (void **)&old_fopen);
            LOGI("Init poster dir: %s, hook fopen success", g_poster_dir);
            // ==============================================================================

#if defined(__i386__)
            auto path = "zygisk/armeabi-v7a.so";
#endif
#if defined(__x86_64__)
            auto path = "zygisk/arm64-v8a.so";
#endif
#if defined(__i386__) || defined(__x86_64__)
            int dirfd = api->getModuleDir();
            int fd = openat(dirfd, path, O_RDONLY);
            if (fd != -1) {
                struct stat sb{};
                fstat(fd, &sb);
                length = sb.st_size;
                data = mmap(nullptr, length, PROT_READ, MAP_PRIVATE, fd, 0);
                close(fd);
            } else {
                LOGW("Unable to open arm file");
            }
#endif
        } else {
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
        }
    }
};

REGISTER_ZYGISK_MODULE(MyModule)