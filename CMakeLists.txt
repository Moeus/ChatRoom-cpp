cmake_minimum_required(VERSION 3.15)
project(chat_server CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =========================================================
#  1. 定义本地依赖库根目录
# =========================================================
set(LOCAL_DEPS_ROOT "${CMAKE_SOURCE_DIR}/deps/host")
message(STATUS ">>>> 正在扫描依赖目录: ${LOCAL_DEPS_ROOT}")

# =========================================================
#  2. 暴力扫描：自动找出所有的头文件路径 (include)
# =========================================================
# 递归搜索所有名叫 "include" 的文件夹
file(GLOB_RECURSE ALL_FILES LIST_DIRECTORIES true "${LOCAL_DEPS_ROOT}/*")
set(ALL_INCLUDE_DIRS "")

foreach(ITEM ${ALL_FILES})
    # 如果是文件夹，且名字是以 "include" 结尾
    if(IS_DIRECTORY "${ITEM}" AND "${ITEM}" MATCHES "/include$")
        list(APPEND ALL_INCLUDE_DIRS "${ITEM}")
    endif()
endforeach()

# 打印一下找到了多少个头文件目录
list(LENGTH ALL_INCLUDE_DIRS INC_COUNT)
message(STATUS ">>>> 找到 ${INC_COUNT} 个头文件目录 (include)")

# =========================================================
#  3. 暴力扫描：自动找出所有的静态库 (.lib)
# =========================================================
# 递归搜索所有的 .lib 文件
file(GLOB_RECURSE ALL_LIBRARIES "${LOCAL_DEPS_ROOT}/*.lib")

list(LENGTH ALL_LIBRARIES LIB_COUNT)
message(STATUS ">>>> 找到 ${LIB_COUNT} 个静态库文件 (.lib)")

if(${LIB_COUNT} EQUAL 0)
    message(FATAL_ERROR ">>>> [错误] 一个 .lib 文件都没找到！请检查路径是否正确！")
endif()

# =========================================================
#  4. MSVC 设置 (UTF-8 支持)
# =========================================================
if(MSVC)
    add_compile_options(/utf-8) 
endif()

# =========================================================
#  5. 定义可执行文件
# =========================================================
# 这里不需要 find_package 了，因为我们是直接链接文件
add_executable(chat_server src/main.cc)
set_target_properties(chat_server PROPERTIES
    # 设置默认运行输出目录为当前源码根目录
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}"
    
    # 针对 Windows MSVC 的特殊处理：
    # 强制 Debug 和 Release 模式都不生成子文件夹，直接输出到根目录
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}"
)
# =========================================================
#  6. 链接所有依赖
# =========================================================
# 6.1 加入搜索到的所有头文件路径
target_include_directories(chat_server PRIVATE src ${ALL_INCLUDE_DIRS})

# 6.2 链接搜索到的所有 .lib 文件
target_link_libraries(chat_server PRIVATE ${ALL_LIBRARIES})

# 6.3 [关键] Windows 系统库补全
# 静态链接 Drogon/Curl/OpenSSL 时，必须手动链接这些 Windows 底层库
# 否则会报 "unresolved external symbol __imp_WSAStartup" 之类的错
if(WIN32)
    target_link_libraries(chat_server PRIVATE 
        ws2_32 
        crypt32 
        advapi32 
        rpcrt4 
        secur32
        bcrypt
        User32
        Normaliz
    )
endif()
