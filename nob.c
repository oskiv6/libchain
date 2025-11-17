#define NOB_IMPLEMENTATION
#include "nob.h"

// ====================================================================
// Configuration
// ====================================================================

#define BUILD_DIR   "build"
#define OBJ_DIR     "build/obj"
#define LIB_DIR     "build/lib"
#define TEST_DIR    "build/test"

#define STATIC_LIB  "build/lib/libchain.a"
#define SHARED_LIB  "build/lib/libchain.so"     // Linux
#ifdef _WIN32
#define SHARED_LIB  "build/lib/chain.dll"       // Windows
#define SHARED_LIB_IMPORT "build/lib/chain.lib"
#endif

#define TEST_EXE    "build/test/chain_test"

const char *sources[] = {
    "src/chain.c",
};

// Common compiler flags
const char *cflags_common[] = {
    "-Iinclude",
    "-O2",
    "-fPIC",          // needed for shared lib
};

// ====================================================================
// Helpers
// ====================================================================

bool ensure_dirs(void) {
    return nob_mkdir_if_not_exists(BUILD_DIR) &&
           nob_mkdir_if_not_exists(OBJ_DIR) &&
           nob_mkdir_if_not_exists(LIB_DIR) &&
           nob_mkdir_if_not_exists(TEST_DIR);
}

const char *obj_path(const char *src) {
    const char *name = nob_path_name(src);
    // "src/chain.c" → "build/obj/chain.o"
    return nob_temp_sprintf("%s/%.*s.o", OBJ_DIR,
                            (int)(strrchr(name, '.') - name), name);
}

bool compile_source(const char *src) {
    const char *obj = obj_path(src);

    if (!nob_needs_rebuild1(obj, src)) {
        nob_log(NOB_INFO, "SKIP %s → %s", src, obj);
        return true;
    }

    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "clang");

    for (size_t i = 0; i < NOB_ARRAY_LEN(cflags_common); ++i) {
        nob_cmd_append(&cmd, cflags_common[i]);
    }

    nob_cmd_append(&cmd, "-c", src, "-o", obj);


    bool ok = nob_cmd_run_sync(cmd);
    nob_cmd_free(cmd);
    return ok;
}

bool build_objects(void) {
    for (size_t i = 0; i < NOB_ARRAY_LEN(sources); ++i) {
        if (!compile_source(sources[i])) return false;
    }
    return true;
}

// ====================================================================
// Build Targets
// ====================================================================

bool build_static_lib(void) {
    if (!nob_needs_rebuild(STATIC_LIB, sources, NOB_ARRAY_LEN(sources))) {
        nob_log(NOB_INFO, "%s up to date", STATIC_LIB);
        return true;
    }

    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "ar", "rcs", STATIC_LIB);
    for (size_t i = 0; i < NOB_ARRAY_LEN(sources); ++i) {
        nob_cmd_append(&cmd, obj_path(sources[i]));
    }

    bool ok = nob_cmd_run_sync(cmd);
    nob_cmd_free(cmd);
    return ok;
}

bool build_shared_lib(void) {
    if (!nob_needs_rebuild(SHARED_LIB, sources, NOB_ARRAY_LEN(sources))) {
        nob_log(NOB_INFO, "%s up to date", SHARED_LIB);
        return true;
    }

    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "clang");

    for (size_t i = 0; i < NOB_ARRAY_LEN(cflags_common); ++i) {
        nob_cmd_append(&cmd, cflags_common[i]);
    }

    nob_cmd_append(&cmd, "-shared", "-o", SHARED_LIB);

    for (size_t i = 0; i < NOB_ARRAY_LEN(sources); ++i) {
        nob_cmd_append(&cmd, sources[i]);            // Tengu is GONE
    }

    bool ok = nob_cmd_run_sync(cmd);
    nob_cmd_free(cmd);
    return ok;
}

bool build_test(void) {
    const char *test_src = "test/unit.c";

    bool need_rebuild = nob_needs_rebuild(TEST_EXE, sources, NOB_ARRAY_LEN(sources));
    if (!need_rebuild && nob_file_exists(test_src)) {
        need_rebuild = nob_needs_rebuild1(TEST_EXE, test_src);
    }

    if (!need_rebuild) {
        nob_log(NOB_INFO, "%s up to date", TEST_EXE);
        return true;
    }

    if (!nob_file_exists(test_src)) {
        nob_log(NOB_ERROR, "test/unit.c not found");
        return false;
    }

    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "clang");

    for (size_t i = 0; i < NOB_ARRAY_LEN(cflags_common); ++i) {
        nob_cmd_append(&cmd, cflags_common[i]);
    }

    nob_cmd_append(&cmd, "-o", TEST_EXE, test_src, STATIC_LIB);

    bool ok = nob_cmd_run_sync(cmd);
    nob_cmd_free(cmd);
    return ok;
}

bool run_test(void) {
    if (!build_test()) return false;

    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, TEST_EXE);
    bool ok = nob_cmd_run_sync(cmd);
    nob_cmd_free(cmd);
    return ok;
}

// ====================================================================
// Commands
// ====================================================================

void cmd_clean(void) {
    nob_log(NOB_INFO, "Cleaning...");
    nob_delete_file("build");
}

void cmd_test(void) {
    if (!ensure_dirs()) exit(1);
    if (!build_objects()) exit(1);
    if (!build_static_lib()) exit(1);
    if (!run_test()) exit(1);
    nob_log(NOB_INFO, "All tests passed!");
}

void cmd_build(void) {
    if (!ensure_dirs()) exit(1);
    if (!build_objects()) exit(1);
    if (!build_static_lib()) exit(1);
    if (!build_shared_lib()) exit(1);
    nob_log(NOB_INFO, "Built libchain.a and %s", SHARED_LIB);
}

void cmd_install(void) {
    cmd_build();

    const char *prefix = "/usr/local";

    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "sudo", "sh", "-c");

    Nob_String_Builder sb = {0};
    nob_sb_append_cstr(&sb, "install -Dm644 include/chain.h ");
    nob_sb_append_cstr(&sb, prefix);
    nob_sb_append_cstr(&sb, "/include/chain.h && ");
    nob_sb_append_cstr(&sb, "install -Dm644 ");
    nob_sb_append_cstr(&sb, STATIC_LIB);
    nob_sb_append_cstr(&sb, " ");
    nob_sb_append_cstr(&sb, prefix);
    nob_sb_append_cstr(&sb, "/lib/ && ");
    nob_sb_append_cstr(&sb, "install -Dm644 ");
    nob_sb_append_cstr(&sb, SHARED_LIB);
    nob_sb_append_cstr(&sb, " ");
    nob_sb_append_cstr(&sb, prefix);
    nob_sb_append_cstr(&sb, "/lib/");
    nob_sb_append_null(&sb);

    nob_cmd_append(&cmd, sb.items);

    if (!nob_cmd_run_sync(cmd)) {
        nob_log(NOB_ERROR, "Install failed — did you run with sudo?");
        exit(1);
    }

    nob_log(NOB_INFO, "chain installed to %s", prefix);
    nob_sb_free(sb);
}

// ====================================================================
// Main
// ====================================================================

int main(int argc, char **argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);

    if (argc < 2) {
        cmd_build();
        return 0;
    }

    const char *subcmd = argv[1];

    if (strcmp(subcmd, "clean") == 0)        { cmd_clean(); }
    else if (strcmp(subcmd, "test") == 0)   { cmd_test(); }
    else if (strcmp(subcmd, "build") == 0)  { cmd_build(); }
    else if (strcmp(subcmd, "install") == 0){ cmd_install(); }
    else {
        nob_log(NOB_ERROR, "Unknown command: %s", subcmd);
        nob_log(NOB_INFO,  "Usage: ./nob [clean|test|build|install]");
        return 1;
    }

    return 0;
}
