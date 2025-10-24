#include "processor.hpp"
#include "cache.hpp"
#include "config.hpp"
#include "assembler.hpp"
#include <cstring>
#include <stdexcept>
#include <unordered_map>
#include <iostream>
#include <iomanip>

namespace sim
{

  // CPU del PE: puede ejecutar por ISA o por traza.
  // - load_*: carga programa/traza
  // - step(): avanza 1 instrucción/acceso
  // - mem_*64: accesos de 64 bits vía caché
  Processor::Processor(PEId id, Cache &cache) : id_(id), cache_(cache) {}

  void Processor::load_trace(const std::vector<Access> &trace)
  {
    trace_ = trace;
    pc_trace_ = 0;
    mode_ = ExecMode::Trace;
  }

  void Processor::load_program(const Program &p)
  {
    prog_ = p;
    pc_ = 0;
    mode_ = ExecMode::ISA;
  }

  void Processor::load_program_from_string(const std::string &asm_source)
  {
    auto p = Assembler::assemble_from_string(asm_source);
    load_program(p);
  }

  void Processor::load_program_from_file(const std::string &path)
  {
    auto p = Assembler::assemble_from_file(path);
    load_program(p);
  }

  void Processor::set_reg(int idx, std::uint64_t v)
  {
    if (idx < 0 || idx >= 8)
      throw std::out_of_range("REG idx");
    reg_[idx] = v;
  }

  std::uint64_t Processor::get_reg(int idx) const
  {
    if (idx < 0 || idx >= 8)
      throw std::out_of_range("REG idx");
    return reg_[idx];
  }

  // Casts rápidos u64 <-> f64
  double Processor::as_double(std::uint64_t v)
  {
    double d;
    std::memcpy(&d, &v, sizeof(double));
    return d;
  }

  std::uint64_t Processor::from_double(double d)
  {
    std::uint64_t v;
    std::memcpy(&v, &d, sizeof(double));
    return v;
  }

  // ===== Accesos a memoria (64 bits) vía caché =====
  std::uint64_t Processor::mem_load64(std::uint64_t addr)
  {
    Word out = 0;
    (void)cache_.load(static_cast<Addr>(addr), sizeof(Word), out);
    return out;
  }

  void Processor::mem_store64(std::uint64_t addr, std::uint64_t val)
  {
    (void)cache_.store(static_cast<Addr>(addr), sizeof(Word), static_cast<Word>(val));
  }

  // ===== Ejecución ISA (una instrucción) =====
  void Processor::exec_one()
  {
    if (pc_ >= prog_.code.size())
      return;
    const Instr &ins = prog_.code[pc_];

    auto next = [&]{ pc_++; };

    switch (ins.op)
    {
    case OpCode::LOAD: {
      // LOAD Rd, [Rs]
      auto dst = ins.rd;
      auto src = ins.ra;
      std::uint64_t addr = reg_[src];
      std::uint64_t val = mem_load64(addr);
      reg_[dst] = val;
      LOG_IF(cfg::kLogPE, "[PE" << id_ << "] LOAD R" << dst << ", [R" << src << "] @0x"
                                << std::hex << addr << std::dec);
      next();
      break;
    }
    case OpCode::STORE: {
      // STORE Rs, [Rd]
      auto src = ins.ra;
      auto dst = ins.rd;
      std::uint64_t addr = reg_[dst];
      mem_store64(addr, reg_[src]);
      LOG_IF(cfg::kLogPE, "[PE" << id_ << "] STORE R" << src << " -> [R" << dst << "] @0x"
                                << std::hex << addr << std::dec);
      next();
      break;
    }
    case OpCode::FMUL: {
      double a = as_double(reg_[ins.ra]);
      double b = as_double(reg_[ins.rb]);
      reg_[ins.rd] = from_double(a * b);
      LOG_IF(cfg::kLogPE, "[PE" << id_ << "] FMUL R" << ins.rd << ", R" << ins.ra << ", R" << ins.rb);
      next();
      break;
    }
    case OpCode::FADD: {
      double a = as_double(reg_[ins.ra]);
      double b = as_double(reg_[ins.rb]);
      reg_[ins.rd] = from_double(a + b);
      LOG_IF(cfg::kLogPE, "[PE" << id_ << "] FADD R" << ins.rd << ", R" << ins.ra << ", R" << ins.rb);
      next();
      break;
    }
    case OpCode::REDUCE: {
      // sumatoria en memoria: sum_{i=0..count-1} [base + i*8]
      std::uint64_t base = reg_[ins.ra];
      std::uint64_t count = reg_[ins.rb];
      double sum = 0.0;
      for (std::uint64_t i = 0; i < count; ++i) {
        Word v = mem_load64(base + i * cfg::kWordBytes);
        sum += as_double(v);
      }
      reg_[ins.rd] = from_double(sum);
      LOG_IF(cfg::kLogPE, "[PE" << id_ << "] REDUCE R" << ins.rd
                                << " base=0x" << std::hex << base << std::dec
                                << " count=" << count
                                << " -> " << std::fixed << std::setprecision(6) << sum);
      next();
      break;
    }
    case OpCode::INC: {
      reg_[ins.rd] += cfg::kWordBytes; // puntero +8
      LOG_IF(cfg::kLogPE, "[PE" << id_ << "] INC R" << ins.rd << " (+" << cfg::kWordBytes << ")");
      next();
      break;
    }
    case OpCode::DEC: {
      reg_[ins.rd] -= 1;
      LOG_IF(cfg::kLogPE, "[PE" << id_ << "] DEC R" << ins.rd);
      next();
      break;
    }
    case OpCode::MOVI: {
      reg_[ins.rd] = ins.imm;
      LOG_IF(cfg::kLogPE, "[PE" << id_ << "] MOVI R" << ins.rd << ", " << ins.imm);
      next();
      break;
    }
    case OpCode::JNZ: {
      // REG0 = contador implícito
      const auto &L = labels_map();
      auto it = L.find(ins.label);
      if (it == L.end())
        throw std::runtime_error("Label no encontrado: " + ins.label);
      if (reg_[0] != 0) {
        pc_ = static_cast<std::size_t>(it->second);
      } else {
        next();
      }
      break;
    }
    }
  }

  void Processor::step()
  {
    if (mode_ == ExecMode::ISA) {
      exec_one();
    } else {
      // Traza (placeholder simple)
      if (pc_trace_ < trace_.size()) {
        // aquí iría la simulación de un acceso
        pc_trace_++;
      }
    }
  }

  bool Processor::is_done() const
  {
    if (mode_ == ExecMode::ISA) {
      return pc_ >= prog_.code.size();
    }
    return true; // modo traza: por ahora asumimos fin
  }

  const std::unordered_map<std::string, int> &Processor::labels_map()
  {
    return get_labels_singleton();
  }

} // namespace sim
