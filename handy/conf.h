#pragma once
#include "non_copy_able.h"
#include <string>
#include <map>
#include <list>

namespace handy
{
    /**
     * @brief INI配置文件解析器，支持读取字符串、整数、浮点数、布尔值等类型配置
     * @details 功能特点
     *          1. 支持标准INI格式（节[section]、键值对key=value或key:value、注释;或#）
     *          2. 键名和节名大小写不敏感（内部自动转为小写处理）
     *          3. 支持多行值（续行以空格开头）
     *          4. 禁止拷贝操作（继承NonCopyAble），允许移动操作
     * @note 线程安全：单线程使用安全；多线程读写需外部加锁
    */
    class Conf : private NonCopyAble
    { 
        public:
            Conf() = default;
            ~Conf() = default;

            /**
             * @brief 解析指定的INI配置文件
             * @param fileName INI文件路径（相对或绝对路径）
             * @return 解析结果：
             *          0：解析成功，
             *          -1：文件打开失败（如路径不存在、权限不足），
             *          正数：解析错误的行号（如语法错误）
            */
            int parse(const std::string& fileName);

             /**
              * @brief 获取字符串类型配置值
              * @param section 配置节名
              * @param name 配置键名
              * @param defaultValue 未找到时的默认值
              * @return 配置值（找到时返回最后一个匹配值，未找到时返回defaultValue）
              * @note 节名和键名大小写不敏感
             */
             std::string get(const std::string& section, const std::string& name, const std::string& defaultValue);

             /**
              * @brief 获取整数类型配置值
              * @param section 配置节名
              * @param name 配置键名
              * @param defaultValue 未找到或解析失败时的默认值
              * @return 解析后的整数值（支持十进制、负数、十六进制"0x1f"）
             */
             long getInteger(const std::string& section, const std::string& name, long defaultValue);

             /**
              * @brief 获取浮点类型配置值
              * @param section 配置节名
              * @param name 配置键名
              * @param defaultValue 未找到或解析失败时的默认值
              * @return 解析后的浮点数值（支持"3.14"、"-1.2"、"1.2e-3"等格式）
             */
             double getReal(const std::string& section, const std::string& name, double defaultValue);

             /**
              * @brief 获取布尔类型配置项
              * @param section 配置节名
              * @param name 配置键名
              * @param defaultValue 未找到或解析失败时的默认值
              * @return 解析后的布尔值（支持"true"、"false"、"1"、"0"、"on"、"off"、"yes"、"no"、"y"、"n"）（不区分大小写）
             */
             bool getBoolean(const std::string& section, const std::string& name, bool defaultValue);

             /**
              * @brief 获取多值配置（多行值）
              * @param section 配置节名
              * @param name 配置键名
              * @return 配置值列表（续行内容会被拆分为多个元素，未找到时返回空列表）
              * @note 适用于键对应多个值的场景（如"ip_list = 192.168.1.1\n 192.168.1.2"）
             */
             std::list<std::string> getStrings(const std::string& section, const std::string& name);

        private:
            // 当前解析的配置文件名
            std::string m_fileName;
            // 存储解析后的配置文件
            // 键为makeKey生成的字符串，值为该键对应的所有值（支持多行）
            std::map<std::string, std::list<std::string>> m_values;

            /**
             * @brief 生成内部存储的键名（节名.键名，转为小写）
             * @param section 配置节名
             * @param name 配置键名
             * @return 格式化后的键名（如section="Server", name="Port" → "server.port"）
             * @note 私有辅助函数，用于统一内部键名格式，实现大小写不敏感查找
            */
            std::string makeKey(const std::string& section, const std::string& name) const;
    };
} // namespace handy