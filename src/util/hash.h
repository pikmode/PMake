#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace pmake::util {

// SHA1 — hex-строка. Используется Node::computeCommandHash (замена QCryptographicHash).
std::string sha1_hex(std::string_view data);

} // namespace pmake::util