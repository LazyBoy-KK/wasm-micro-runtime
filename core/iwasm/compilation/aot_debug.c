#include "aot_debug.h"
#include "aot_llvm.h"
#include <assert.h>
#include <fcntl.h>
#include <llvm-c-18/llvm-c/Core.h>
#include <llvm-c-18/llvm-c/DebugInfo.h>
#include <llvm-c-18/llvm-c/Types.h>
#include <libgen.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "bh_hashmap.h"
#include "bh_vector.h"
#include "platform_common.h"

AOTCompContext *cur_ctx = NULL;

#define SEED 131       // 一个质数，用于计算哈希值
#define MOD 1000000007 // 模数，通常选择一个大的质数

unsigned int
BKDRHash(const void *p)
{
    const char *str = (const char *)p;
    unsigned long hash = 0;
    unsigned long seed = SEED;
    while (*str) {
        hash = hash * seed + *str++;
        hash %= MOD; // 防止溢出
    }
    return hash;
}

bool
equal(void *str1, void *str2)
{
    return strcmp(str1, str2) == 0;
}

char *
get_file_name(char *path)
{
    char *filename = strrchr(path, '/');
    if (filename == NULL) {
        filename = path; // 如果没有找到 '/', 则整个路径就是文件名
    }
    else {
        filename++; // 跳过 '/'
    }
    return filename;
}

char *
set_file_extension(char *filename, char *extension)
{
    char *dot = strrchr(filename, '.');
    if (dot != NULL) {
        *dot = '\0'; // 截断 '.' 前面的部分
    }
    strcat(filename, extension);
    return filename;
}

int
count_line(const char *str)
{
    int count = 0;
    while (*str) {
        if (*str == '\n') {
            count++;
        }
        str++;
    }
    return count;
}

int
write_str(AOTCompContext *comp_ctx, const char *str)
{
    int res = 0;
    res = write(comp_ctx->debug_file_fd, str, strlen(str));
    assert(res >= 0);
    int old_line = comp_ctx->cur_line;
    comp_ctx->cur_line += count_line(str);
    return old_line;
}

bool
init_debug_ctx(AOTCompContext *comp_ctx, const char *wasm_file_name)
{
    if (!(comp_ctx->debug_builder = LLVMCreateDIBuilder(comp_ctx->module))) {
        aot_set_last_error("create LLVM Debug Infor builder failed.");
        return false;
    }
    char *dir_name = dirname(strdup(wasm_file_name));
    char *temp = strdup(wasm_file_name);
    char *file_name = set_file_extension(get_file_name(temp), ".debug");

    comp_ctx->debug_file = LLVMDIBuilderCreateFile(
        comp_ctx->debug_builder, file_name, file_name ? strlen(file_name) : 0,
        dir_name, dir_name ? strlen(dir_name) : 0);
    if (!comp_ctx->debug_file) {
        aot_set_last_error("dwarf generate file info failed");
        return false;
    }

    char *compiler_name = "wamr 2.1.2";
    comp_ctx->debug_comp_unit = LLVMDIBuilderCreateCompileUnit(
        comp_ctx->debug_builder, 1, comp_ctx->debug_file, compiler_name,
        strlen(compiler_name), 0, NULL, 0, 1, NULL, 0, LLVMDWARFEmissionFull, 0,
        0, 0, "/", 1, "", 0);
    if (!comp_ctx->debug_comp_unit) {
        aot_set_last_error("dwarf generate compile unit info failed");
        return false;
    }
    int fd = open(set_file_extension(strdup(wasm_file_name), ".debug"),
                  O_CREAT | O_RDWR | O_TRUNC, 0666);
    if (fd < 0) {
        aot_set_last_error("create dwarf file failed");
        return false;
    }
    comp_ctx->debug_file_fd = fd;
    comp_ctx->cur_line = 1;
    comp_ctx->func_debug_map =
        bh_hash_map_create(100, false, BKDRHash, equal, NULL, NULL);
    cur_ctx = comp_ctx;
    free(dir_name);
    free(temp);
    return true;
}

typedef struct FuncInfo {
    char *info;
    LLVMValueRef func;
    Vector insts;
} FuncInfo;

typedef struct InstInfo {
    enum {
        LLVMIR,
        WasmOpcode,
    } kind;
    char *info;
    LLVMValueRef inst;
} InstInfo;

void
create_function_debug_info(AOTCompContext *comp_ctx, LLVMValueRef function)
{
    bool res = false;
    size_t func_name_size = 0;
    const char *func_name = LLVMGetValueName2(function, &func_name_size);
    char *prefix = "Function: ";
    // '\0'+'\n'
    char *temp = calloc(1, strlen(prefix) + strlen(func_name) + 2);
    memcpy(temp, prefix, strlen(prefix) + 1);
    temp = strcat(strcat(temp, func_name), "\n");

    FuncInfo *func_info = malloc(sizeof(FuncInfo));
    func_info->info = temp;
    func_info->func = function;
    res = bh_vector_init(&func_info->insts, 100, sizeof(InstInfo), false);
    assert(res == true);
    res = bh_hash_map_insert(comp_ctx->func_debug_map, (void *)func_name,
                             func_info);
    assert(res == true);
}

void
build_debug_info(AOTCompContext *comp_ctx, LLVMValueRef inst, char *inst_str,
                 const char *file_name, char *location, InstKind kind)
{
    char temp[5] = "0 ";
    size_t len = strlen(inst_str) + strlen(file_name) + strlen(location)
                 + strlen("\tIRInfo") + strlen(" ") + strlen("\n") + 2;
    char *info = malloc(len);
    memset(info, 0, len);
    strcat(info, "\tIRInfo");
    if ((kind & comp_ctx->debug_inst_kind) != 0) {
        sprintf(temp, "%d ", __builtin_ctz(comp_ctx->debug_inst_kind));
    }
    strcat(info, temp);
    strcat(info, inst_str);
    strcat(info, file_name);
    strcat(info, location);
    strcat(info, "\n");
    const char *func_name = LLVMGetValueName(comp_ctx->cur_func);
    FuncInfo *func_info = (FuncInfo *)bh_hash_map_find(comp_ctx->func_debug_map,
                                                       (void *)func_name);
    assert(func_info != NULL);
    InstInfo inst_info;
    inst_info.info = info;
    inst_info.kind = LLVMIR;
    inst_info.inst = inst;
    bh_vector_append(&func_info->insts, &inst_info);
}

void
insert_wasm_opcode(AOTCompContext *comp_ctx, char *wasm_opcode)
{
    size_t len = strlen("\tWasmOpcode") + strlen(" ") + strlen(wasm_opcode)
                 + strlen("\n") + 1;
    char *info = malloc(len);
    memset(info, 0, len);
    strcat(info, "\tWasmOpcode ");
    strcat(info, wasm_opcode);
    strcat(info, "\n");
    const char *func_name = LLVMGetValueName(comp_ctx->cur_func);
    FuncInfo *func_info = (FuncInfo *)bh_hash_map_find(comp_ctx->func_debug_map,
                                                       (void *)func_name);
    assert(func_info != NULL);
    InstInfo inst_info;
    inst_info.info = info;
    inst_info.kind = WasmOpcode;
    inst_info.inst = NULL;
    bh_vector_append(&func_info->insts, &inst_info);
}

LLVMValueRef
build_load(LLVMBuilderRef builder, LLVMTypeRef type, LLVMValueRef ptr,
           const char *name, const char *file_name, int line)
{
    char temp[20];
    LLVMValueRef value = LLVMBuildLoad2(builder, type, ptr, name);
    sprintf(temp, ":%d", line);
    build_debug_info(cur_ctx, value, "Load ", file_name, temp, Other);
    return value;
}

LLVMValueRef
build_store(LLVMBuilderRef builder, LLVMValueRef val, LLVMValueRef ptr,
            const char *file_name, int line)
{
    char temp[20];
    LLVMValueRef value = LLVMBuildStore(builder, val, ptr);
    sprintf(temp, ":%d", line);
    build_debug_info(cur_ctx, value, "Store ", file_name, temp, Other);
    return value;
}

LLVMValueRef
build_br(LLVMBuilderRef builder, LLVMBasicBlockRef dest, const char *file_name,
         int line)
{
    char temp[20];
    LLVMValueRef value = LLVMBuildBr(builder, dest);
    sprintf(temp, ":%d", line);
    build_debug_info(cur_ctx, value, "UncondBranch ", file_name, temp,
                     Br | Brif);
    return value;
}

LLVMValueRef
build_condbr(LLVMBuilderRef builder, LLVMValueRef cond, LLVMBasicBlockRef then,
             LLVMBasicBlockRef other, const char *file_name, int line)
{
    char temp[20];
    LLVMValueRef value = LLVMBuildCondBr(builder, cond, then, other);
    sprintf(temp, ":%d", line);
    build_debug_info(cur_ctx, value, "CondBranch ", file_name, temp, Brif | Br);
    return value;
}

LLVMValueRef
build_call(LLVMBuilderRef builder, LLVMTypeRef func_type, LLVMValueRef func,
           LLVMValueRef *args, unsigned num_args, const char *name,
           const char *file_name, int line)
{
    char temp[20];
    LLVMValueRef value =
        LLVMBuildCall2(builder, func_type, func, args, num_args, name);
    sprintf(temp, ":%d", line);
    build_debug_info(cur_ctx, value, "Call ", file_name, temp,
                     Call | CallIndirect);
    return value;
}

LLVMValueRef
build_switch(LLVMBuilderRef builder, LLVMValueRef val,
             LLVMBasicBlockRef default_block, unsigned case_num,
             const char *file_name, int line)
{
    char temp[20];
    LLVMValueRef value = LLVMBuildSwitch(builder, val, default_block, case_num);
    sprintf(temp, ":%d", line);
    build_debug_info(cur_ctx, value, "Switch ", file_name, temp, BrTable);
    return value;
}

LLVMValueRef
build_ret(LLVMBuilderRef builder, LLVMValueRef val, const char *file_name,
          int line)
{
    char temp[20];
    LLVMValueRef value = LLVMBuildRet(builder, val);
    sprintf(temp, ":%d", line);
    build_debug_info(cur_ctx, value, "Return ", file_name, temp, Return);
    return value;
}

LLVMValueRef
build_ret_void(LLVMBuilderRef builder, const char *file_name, int line)
{
    char temp[20];
    LLVMValueRef value = LLVMBuildRetVoid(builder);
    sprintf(temp, ":%d", line);
    build_debug_info(cur_ctx, value, "Return ", file_name, temp, Other);
    return value;
}

void
call_back(void *func_name, void *value, void *user_data)
{
    FuncInfo *func_info = (FuncInfo *)value;
    LLVMValueRef function = func_info->func;
    AOTCompContext *comp_ctx = (AOTCompContext *)user_data;
    size_t func_name_size = strlen(func_name);
    int line = write_str(comp_ctx, func_info->info);

    LLVMMetadataRef function_ty = LLVMDIBuilderCreateSubroutineType(
        comp_ctx->debug_builder, comp_ctx->debug_file, NULL, 0, LLVMDIFlagZero);

    LLVMMetadataRef func_scope = LLVMDIBuilderCreateFunction(
        comp_ctx->debug_builder, comp_ctx->debug_file, func_name,
        func_name_size, func_name, func_name_size, comp_ctx->debug_file, line,
        function_ty, true, true, line, LLVMDIFlagPublic, false);
    LLVMSetSubprogram(function, func_scope);
    for (size_t i = 0; i < bh_vector_size(&func_info->insts); i++) {
        InstInfo inst_info;
        bh_vector_get(&func_info->insts, i, &inst_info);
        int line = write_str(comp_ctx, inst_info.info);
        if (inst_info.kind == LLVMIR) {
            LLVMMetadataRef debug_locatin = LLVMDIBuilderCreateDebugLocation(
                comp_ctx->context, line, 0, func_scope, NULL);
            LLVMInstructionSetDebugLoc(inst_info.inst, debug_locatin);
        }
    }
    free(func_info->info);
    bh_vector_destroy(&func_info->insts);
    free(func_info);
}

void
finish_debug_info(AOTCompContext *comp_ctx)
{
    bh_hash_map_traverse(comp_ctx->func_debug_map, call_back, comp_ctx);
    bh_hash_map_destroy(comp_ctx->func_debug_map);
}

void
write_wasm_opcode()
{}