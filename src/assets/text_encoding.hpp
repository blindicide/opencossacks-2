#pragma once

#include <string>
#include <string_view>

namespace oc2::assets {

// Тексты и XML игры записаны в Windows-1251; внутри движка строки хранятся в UTF-8.
std::string cp1251ToUtf8(std::string_view text);

} // namespace oc2::assets
