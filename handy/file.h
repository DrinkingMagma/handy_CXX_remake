#pragma once

#include "status.h"
#include "non_copy_able.h"
#include <string>
#include <vector>

namespace handy
{
    /**
     * @class File
     * @brief  文件和目录操作的工具类，提供静态方法封装常用文件操作
     * @note 所有方法均为线程安全（依赖底层系统调用的线程安全性）
    */
    class File : private NonCopyAble
    { 
        public:
            /**
             * @brief 读取文件内容到字符串
             * @param filename 文件名
             * @param[out] content 文件内容
             * @return Status 操作结果状态
            */
            static Status getContent(const std::string& filename, std::string& content);

            /**
             * @brief 将字符串内容写入文件（覆盖模式）
             * @param filename 文件名
             * @param content 要写入的内容
             * @return Status 操作结果状态
            */
            static Status writeContent(const std::string& filename, const std::string& content);

            /**
             * @brief 安全写入文件（先写临时文件，再原子重命名）
             * @param targetFilename 目标文件名
             * @param tmpFilename 临时文件名（用于原子操作）
             * @param content 要写入的内容
             * @return Status 操作结果状态
             * @note 保证写入操作的原子性，避免文件损坏
            */
            static Status renameSave(const std::string& targetFilename, const std::string& tmpFilename, const std::string& content);

            /**
             * @brief 获取目录下的所有子文件/子目录名
             * @param dir 目录名
             * @param[out] result 目录项名称列表
             * @return Status 操作结果状态
            */
            static Status getChildren(const std::string& dir, std::vector<std::string>* result);

            /**
             * @brief 删除指定文件
             * @param filename 文件名
             * @return Status 操作结果状态
            */
            static Status deleteFile(const std::string& filename);

            /**
             * @brief 创建目录（仅创建一级目录）
             * @param dirname 目录名
             * @return Status 操作结果状态
             * @note 创建的目录权限为0755(rwxr-xr-x)
            */
            static Status createDir(const std::string& dirname);

            /**
             * @brief 删除空目录
             * @param dirname 目录名
             * @return Status 操作结果状态
             * @note 目录必须为空才能删除成功
            */
            static Status deleteDir(const std::string& dirname);

            /**
             * @brief 获取文件大小
             * @param filename 文件名
             * @param[out] size 文件大小（字节）
             * @return Status 操作结果状态
            */
            static Status getFileSize(const std::string& filename, uint64_t size);

            /**
             * @brief 重名名文件或目录
             * @param src 源文件或目录路径
             * @param dst 目标文件或目录路径
             * @return Status 操作结果状态
            */
            static Status rename(const std::string& src, const std::string& dst);

            /**
             * @brief 检查文件或目录是否存在
             * @param path 文件或目录路径
             * @return bool true:存在; false:不存在
            */
            static bool exists(const std::string& path);

        private:
            /**
             * @brief 私有构造函数，禁止实例化（工具类仅提供静态方法）
            */
            File() = delete;
    };
} // namespace handy
