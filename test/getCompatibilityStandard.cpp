#include <string.h>
#include <type_traits>
#include <iostream>

// 编译时判断strerror_r的返回值类型
using strerror_r_ret_type = decltype(strerror_r(0, nullptr, 0));

int main() {
    if (std::is_same<strerror_r_ret_type, int>::value) {
        std::cout << "当前系统使用的是POSIX版本strerror_r（返回int）" << std::endl;
    } else if (std::is_same<strerror_r_ret_type, char*>::value) {
        std::cout << "当前系统使用的是XSI版本strerror_r（返回char*）" << std::endl;
    } else {
        std::cout << "未知版本的strerror_r" << std::endl;
    }
    return 0;
}