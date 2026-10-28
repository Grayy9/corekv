#include "corekv/db.hpp"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: corekv_cli <directory> put <key> <value> | get <key> | delete <key> | scan <begin> <end> <limit> | flush | compact\n";
    return 2;
  }
  const std::string command = argv[2];
  if (!((command == "put" && argc == 5) ||
        (command == "scan" && argc == 6) ||
        ((command == "get" || command == "delete") && argc == 4) ||
        ((command == "flush" || command == "compact") && argc == 3))) {
    std::cerr << "invalid command arguments\n";
    return 2;
  }
  try {
    auto db = corekv::DB::Open(argv[1]);
    if (command == "put") db->Put(argv[3], argv[4]);
    else if (command == "get") {
      const auto value = db->Get(argv[3]);
      if (!value) return 3;
      std::cout << *value << '\n';
    } else if (command == "delete") db->Delete(argv[3]);
    else if (command == "scan") {
      const std::string limit_text = argv[5];
      std::size_t consumed = 0;
      const auto limit = std::stoull(limit_text, &consumed);
      if (consumed != limit_text.size() || limit > 1000000)
        throw std::invalid_argument("invalid scan limit");
      for (const auto& [key, value] : db->Scan(argv[3], argv[4], static_cast<std::size_t>(limit)))
        std::cout << key << '\t' << value << '\n';
    }
    else if (command == "flush") db->Flush();
    else db->Compact();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
