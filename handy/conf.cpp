#include "conf.h"
#include <algorithm>
#include <cstdio>
#include <memory>
#include "slice.h"
#include <iostream>

using std::string;
using std::list;
using std::FILE;

namespace handy 
{
    string Conf::makeKey(const string&section, const std::string& name) const
    {
        string key = section + "." + name;
        // 转换为小写，实现大小写不敏感查找
        transform(key.begin(), key.end(), key.begin(), ::tolower);
        return key;
    }

    string Conf::get(const string& section, const std::string& name, const std::string& defaultValue)
    {
        string key = makeKey(section, name);
        auto p = m_values.find(key);
        // 若找到则返回最后一个值（多行值取最后一行），否则返回默认值
        return p == m_values.end() ? defaultValue : p->second.back();
    }

    list<string> Conf::getStrings(const string& section, const std::string& name)
    {
        string key = makeKey(section, name);
        auto p = m_values.find(key);
        return p == m_values.end() ? list<string>() : p->second;
    }

    long Conf::getInteger(const string& section, const std::string& name, long defaultValue)
    {
        string valStr = get(section, name, "");
        const char *val = valStr.c_str();
        char *end;

        // 解析整数（支持十进制、十六进制）
        // - value：待解析字符串
        // - &end：输出参数，指向未解析的第一个字符
        // - 0：自动判断进制（0x开头为十六进制，否则为十进制）
        long n = strtol(val, &end, 0);

        // 若解析成功（至少有一个字符被解析），返回解析结果，否则返回默认值
        return end > val ? n : defaultValue;
    }

    double Conf::getReal(const std::string& section, const std::string& name, double defaultValue)
    { 
        string valStr = get(section, name, "");
        const char* val = valStr.c_str();
        char *end;

        // 解析浮点数（支持科学计数法）
        double n = strtod(val, &end);

        return end > val ? n : defaultValue;
    }

    bool Conf::getBoolean(const std::string& section, const std::string& name, bool defaultValue)
    {
        string valStr = get(section, name, "");
        transform(valStr.begin(), valStr.end(), valStr.begin(), ::tolower);

        if(valStr == "true" || valStr == "yes" || valStr == "on" || valStr == "1")
            return true;
        else if(valStr == "false" || valStr == "no" || valStr == "off" || valStr == "0")
            return false;
        else
            return defaultValue;
    }

    /**
     * @brief 内部辅助结构体，用于逐行扫描和解析INI文件内容
     * @details 封装了字符串扫描、空格跳过、字符匹配等功能，简化parse()函数逻辑
     */
    namespace
    {
        struct LineScanner
        { 
            // 当前扫描位置指针
            char *p;
            // 错误标志（0：无错误，1：解析错误）
            int err;

            /**
             * @brief 构造函数，初始化扫描位置
             * @param ln 待扫描的行字符串
            */
            LineScanner(char *ln) : p(ln), err(0) {}

            /**
             * @brief 跳过当前位置的所有空格字符
             * @return 自身引用（支持链式调用）
            */
            LineScanner& skipSpace() 
            {
                while (*p && isspace(static_cast<unsigned char>(*p))) {
                    p++;
                }
                return *this;
            }

            /**
             * @brief 去除字符串尾部的空格
             * @param s 字符串的起始指针
             * @param e 字符串结束指针（初始为末尾）
             * @return 去除尾部空格后的字符串
            */
            static string rstrip(const char* s, const char* e)
            {
                while(e > s && isspace(static_cast<unsigned char>(*e -1)))
                    e--;
                return string(s, e);
            }

            /**
             * @brief 获取当前位置的第一个非空格字符
             * @return 字符ASCII值（若移到末尾，返回0）
            */
            int peekChar()
            {
                skipSpace();
                return *p;
            }

            /**
             * @brief 跳过指定数量的字符
             * @param i 要跳过的字符数
             * @return 自身引用（支持链式调用）
            */
            LineScanner& skip(int i)
            {
                p += i;
                return *this;
            }

            /**
             * @brief 匹配并跳过指定字符
             * @param c 期望匹配的字符
             * @return 自身引用（支持链式调用）
             * @note 若匹配失败，设置err = 1
            */
            LineScanner& match(char c)
            {
                skipSpace();
                err = (*p++ != c);
                return *this;
            }

            /**
             * @brief 从当前位置提取字符，直到遇到指定字符
             * @param c 终止字符（如']'、'='、':'）
             * @return 提取的字符串（不含终止字符，去除尾部空格）
             * @note 若未找到终止字符，设置err = 1并返回空字符串
            */
            string consumeTill(char c)
            {
                skipSpace();
                char *e = p;
                // 移动到终止字符位置
                while (!err && *e && *e != c)
                {
                    e++;
                }
                // 检查是否找到终止符
                if(*e != c)
                {
                    err = 1;
                    return "";
                }
                // 提取并修剪字符串
                char *s = p;
                p = e;
                return rstrip(s, e);
            }

            /**
             * @brief 从当前位置提取字符，直到行尾或注释符
             * @return 提取的字符串（去除尾部空格，不含注释符）
             * @note 注释符为';'或'#'，遇到空格后再出现的非空格字符视为值结束
            */
            string consumeTillEnd()
            {
                skipSpace();
                char *e = p;
                // 标记是否已经遇到空格
                int wasSpace = 0;
                while (!err && *e && *e != ';' && *e != '#')
                {
                    if(wasSpace)
                        break;
                    wasSpace = isspace(static_cast<unsigned char>(*e));
                    e++;
                }
                
                char *s = p;
                p = e;
                return rstrip(s, e);
            }
        };
    } // namespace

    int Conf::parse(const std::string& fileName)
    {
        // 记录当前解析的文件名
        m_fileName = fileName;
        // 以只读模式打开
        FILE *file = fopen(m_fileName.c_str(), "r");
        if(!file)
            return -1;

        // 使用智能指针自动关闭文件（避免资源泄露）
        auto autoClose = std::shared_ptr<FILE>(file, fclose);

        // 最大行长度（16KB）
        static const int MAX_LINE = 16 * 1024;
        // 行缓冲区,智能指针自动释放缓冲区
        auto lineBuf = std::make_unique<char[]>(MAX_LINE);

        int curLine = 0; // 当前行号
        string section; // 当前解析的节名称
        Slice key;     // 当前解析的键名称
        int err = 0;    // 错误标志

        // 逐行读取文件
        while(!err && fgets(lineBuf.get(), MAX_LINE, file) != nullptr)
        {
            curLine++;
            // 初始化行扫描器
            LineScanner scanner(lineBuf.get());
            // 获取行首的第一个非空格字符
            int firstChar = scanner.peekChar();

            // 处理注释行或空行（跳过）
            if(firstChar == ';' || firstChar == '#' || firstChar == '\0')
                continue;
            // 处理节定义，如：[section]
            else if(firstChar == '[')
            {
                section = scanner.skip(1).consumeTill(']');
                err = scanner.match(']').err;
                // 新节开始，重置当前键名
                key.clear();
            }
            // 处理续行（以空格开头，说明属于上一个键的值）
            else if (isspace(static_cast<unsigned char>(lineBuf[0])))
            {
                if(!key.empty())
                {
                    // 将续行的内容添加到上一个键的值列表中
                    m_values[makeKey(section, key)].push_back(scanner.consumeTill('\0'));
                }
                else
                    err = 1;
            }
            // 处理键值对（如key = value或key::value）
            else
            {
                // 备份扫描器状态（用于兼容:分隔符）
                LineScanner backUp = scanner;
                // 尝试以'='分隔键和值
                key = scanner.consumeTill('=');
                key.trimSpace();
                
                if(scanner.peekChar() == '=')
                    scanner.skip(1);
                else
                {
                    scanner = backUp;
                    key = scanner.consumeTill(':');
                    err = scanner.match(':').err;
                }
                scanner.skipSpace();
                // 提取键值对并存储
                Slice val = scanner.consumeTillEnd();
                val.trimSpace();
                m_values[makeKey(section, key)].push_back(val);

                // std::cout << section << ": " << "-" << key.toString() << "- = -" << val.toString() << "-" << std::endl;
            }
        }

        return err ? curLine : 0;
    }
} // namespace handy