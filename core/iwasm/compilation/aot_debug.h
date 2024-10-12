
#ifndef _AOT_DEBUG_H_
#define _AOT_DEBUG_H_
#include "aot_llvm.h"

#ifdef __cplusplus
extern "C" {
#endif
void
create_function_debug_info(AOTCompContext *comp_ctx, LLVMValueRef function);

bool
init_debug_ctx(AOTCompContext *comp_ctx, const char *wasm_file_name);

void
finish_debug_info(AOTCompContext *comp_ctx);

LLVMValueRef
build_load(LLVMBuilderRef builder, LLVMTypeRef type, LLVMValueRef ptr,
           const char *name, const char *file_name, int line);

LLVMValueRef
build_store(LLVMBuilderRef builder, LLVMValueRef val, LLVMValueRef ptr,
            const char *file_name, int line);

LLVMValueRef
build_br(LLVMBuilderRef builder, LLVMBasicBlockRef dest, const char *file_name,
         int line);

LLVMValueRef
build_condbr(LLVMBuilderRef builder, LLVMValueRef cond, LLVMBasicBlockRef then,
             LLVMBasicBlockRef other, const char *file_name, int line);

LLVMValueRef
build_call(LLVMBuilderRef builder, LLVMTypeRef func_type, LLVMValueRef func,
           LLVMValueRef *args, unsigned num_args, const char *name,
           const char *file_name, int line);

LLVMValueRef
build_switch(LLVMBuilderRef builder, LLVMValueRef val,
             LLVMBasicBlockRef default_block, unsigned case_num,
             const char *file_name, int line);

LLVMValueRef
build_ret(LLVMBuilderRef builder, LLVMValueRef val, const char *file_name,
          int line);

LLVMValueRef
build_ret_void(LLVMBuilderRef builder, const char *file_name, int line);

#define WAMR_BUILD_LOAD(builder, type, ptr, name) \
    build_load(builder, type, ptr, name, __FILE__, __LINE__)

#define WAMR_BUILD_STORE(builder, val, ptr) \
    build_store(builder, val, ptr, __FILE__, __LINE__)

#define WAMR_BUILD_BR(builder, dest) build_br(builder, dest, __FILE__, __LINE__)

#define WAMR_BUILD_CONDBR(builder, cond, then, other) \
    build_condbr(builder, cond, then, other, __FILE__, __LINE__)

#define WAMR_BUILD_CALL(builder, func_type, func, args, num_args, name)  \
    build_call(builder, func_type, func, args, num_args, name, __FILE__, \
               __LINE__)

#define WAMR_BUILD_SWITCH(builder, val, default_block, case_num) \
    build_switch(builder, val, default_block, case_num, __FILE__, __LINE__)

#define WAMR_BUILD_RET(builder, val) build_ret(builder, val, __FILE__, __LINE__)

#define WAMR_BUILD_RETVOID(builder) build_ret_void(builder, __FILE__, __LINE__)
#ifdef __cplusplus
} /* end of extern "C" */
#endif

#endif /* end of _AOT_DEBUG_H_ */