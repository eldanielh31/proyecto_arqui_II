#pragma once
// Utilidades de impresión compactas para registros y bitcasts u64<->f64.
// Pensado para dumps de stepping y resúmenes.

#include <cstdint>
#include <cstring>
#include <iomanip>
#include <ostream>

namespace sim::dbg {

inline void set_float_fmt(std::ostream& os) {
  os.setf(std::ios::fixed);
  os << std::setprecision(6);
}

inline std::uint64_t to_u64(double d) {
  std::uint64_t v; std::memcpy(&v, &d, sizeof(double)); return v;
}

inline double to_f64(std::uint64_t u) {
  double d; std::memcpy(&d, &u, sizeof(double)); return d;
}

inline void print_reg_compact(std::ostream& os, int r, std::uint64_t v) {
  os << "R" << r << "=";
  if (r >= 4) {
    os << "0x" << std::hex << v << std::dec
       << " (f64=" << std::fixed << std::setprecision(6) << to_f64(v) << ")";
  } else if (r == 1 || r == 2 || r == 3) {
    os << "0x" << std::hex << v << std::dec << " (addr-dec=" << v << ")";
  } else {
    os << "0x" << std::hex << v << std::dec;
  }
}

inline void print_reg_diff(std::ostream& os, int r, std::uint64_t before, std::uint64_t after) {
  if (before == after) return;
  os << "  R" << r << ": ";
  if (r >= 4) {
    os << "0x" << std::hex << before << std::dec << " (" << to_f64(before) << ")"
       << " -> "
       << "0x" << std::hex << after  << std::dec << " (" << to_f64(after) << ")";
  } else if (r == 1 || r == 2 || r == 3) {
    os << "0x" << std::hex << before << std::dec << " [" << before << "]"
       << " -> "
       << "0x" << std::hex << after  << std::dec << " [" << after  << "]";
  } else {
    os << "0x" << std::hex << before << std::dec
       << " -> "
       << "0x" << std::hex << after  << std::dec;
  }
  os << "\n";
}

} // namespace sim::dbg
