#include "corekv/db.hpp"

#include <cstdlib>

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  auto db = corekv::DB::Open(argv[1]);
  db->Put("survives", "yes");
  db->WriteBatch({{"batch-a", "one"}, {"batch-b", "two"}});
  std::_Exit(0);
}
