#ifndef FIZZYHELPER_H
#define FIZZYHELPER_H

#include <string_view>
#include <exception>
#include <stdexcept>
#include <vector>
#include <optional>
#include <type_traits>
#include <array>
#include <any>
#include <cstdint>
#include <cmath>
#include <cassert>


#include "lib/fizzy/bytes.hpp"
#include "lib/fizzy/instantiate.hpp"
#include "lib/fizzy/module.hpp"
#include "lib/fizzy/value.hpp"
#include "lib/fizzy/execute.hpp"
#include "lib/fizzy/parser.hpp"

#ifndef __cpp_lib_type_identity
namespace std { template<class T> struct type_identity { using type = T; }; }
#endif

namespace fizzy
{
  namespace helpers
  {
/*
    template<ValType V> constexpr std::string_view type_string(void);
    template<> constexpr std::string_view type_string<ValType::i32>(void) { return "32-bit integer"; }
    template<> constexpr std::string_view type_string<ValType::i64>(void) { return "64-bit integer"; }
    template<> constexpr std::string_view type_string<ValType::f32>(void) { return "32-bit floating point"; }
    template<> constexpr std::string_view type_string<ValType::f64>(void) { return "64-bit floating point"; }
*/
    constexpr static inline std::string_view type_string(ValType vt)
    {
      switch(vt)
      {
      case ValType::i32: return "32-bit integer";
      case ValType::i64: return "64-bit integer";
      case ValType::f32: return "32-bit floating point";
      case ValType::f64: return "64-bit floating point";
      }
    }

    template<typename T>
    constexpr ValType value_type(void);

    template<> constexpr ValType value_type<uint32_t>(void) { return ValType::i32; }
    template<> constexpr ValType value_type< int32_t>(void) { return ValType::i32; }
    template<> constexpr ValType value_type<uint64_t>(void) { return ValType::i64; }
    template<> constexpr ValType value_type< int64_t>(void) { return ValType::i64; }
    template<> constexpr ValType value_type< float_t>(void) { return ValType::f32; }
    template<> constexpr ValType value_type<double_t>(void) { return ValType::f64; }
    template<> constexpr ValType value_type<void    >(void) { return ValType(0x00); }

    template<typename T>
    constexpr bool matches_value_type(ValType val) { return val == value_type<T>();}


    template<typename T> constexpr size_t return_count(void) { return 1; }
    template<> constexpr size_t return_count<void>(void) { return 0; }

    struct arg_result_t
    {
      ssize_t arg_num;
      ValType given;
      ValType expected;
      constexpr bool operator() (void) const { return given == expected; }
    };

    template<typename T>
    constexpr arg_result_t arg_compare(const Value* origin, const Value*& pos)
    {
      return { origin - pos, *pos++, value_type<T>() };
    }

    template<typename T>
    constexpr T arg_expand(const Value*& pos) { return pos++->as<T>(); }

    template<auto funcptr, typename Rtype, typename... Args>
    static inline ExternalFunction import_wrapper(std::type_identity<Rtype(Args...)>)
    {
      static auto func_wrapper = [](::std::any& host_context, Instance& instance, const Value* args, ExecutionContext& ctx) noexcept -> ExecutionResult
      {
        if constexpr(::std::is_same_v<Rtype, void>)
        {
          reinterpret_cast<Rtype(*)(Args...)>(funcptr)(arg_expand<Args>(args)...);
          return Void;
        }
        return ExecutionResult(Value(reinterpret_cast<Rtype(*)(Args...)>(funcptr)(arg_expand<Args>(args)...)));
      };

      ValType* inputs = new ValType[sizeof...(Args)] { value_type<Args>()... };
      ValType* outputs = return_count<Rtype>() ? new ValType { value_type<Rtype>() } : nullptr;

      return ExternalFunction
          {
              ExecuteFunction { HostFunctionPtr(&func_wrapper) },   // function
              { inputs, sizeof...(Args) },        // input_types
              { outputs, return_count<Rtype>() }, // output_types
          };
    }
  }


  class bad_function_call : public std::runtime_error
  {
public:
    bad_function_call(::std::string_view function_name)
        : std::runtime_error(std::string().append("Function \"").append(function_name).append("\" does not exist"))
    { }
  };

  class invalid_argument_type : public std::runtime_error
  {
public:
    invalid_argument_type(::std::string_view function_name, const helpers::arg_result_t& result)
        : std::runtime_error(
            std::string()
              .append("Function \"")
              .append(function_name)
              .append("\" expected argument ")
              .append(std::to_string(result.arg_num))
              .append(" to be a ").append(helpers::type_string(result.expected))
              .append(" but a ").append(helpers::type_string(result.given))
              .append(" was provide."))
    { }
  };

  class invalid_return_type : public std::runtime_error
  {
public:
    invalid_return_type(::std::string_view function_name)
        : std::runtime_error(std::string().append("The return type for \"").append(function_name).append("\" do not match the function signature"))
    { }
  };

class WasmModule
{
public:

  constexpr void setMemoryAllocationLimit(size_t byte_count)
  {
    assert(byte_count / PageSize < 0xFFFFFFFF);
    m_memory_page_allocation_limit = byte_count / PageSize;
  }

  constexpr size_t setMemoryAllocationLimit(void) const
  {
    return m_memory_page_allocation_limit * PageSize;
  }

  void loadBytecode(const void* wasm_binary, size_t wasm_binary_size)
    { m_module = ::std::move(parse({ static_cast<const uint8_t*>(wasm_binary), wasm_binary_size })); }

  template<auto funcptr>
  void addCallback(void)
    { m_imported_functions.emplace_back(helpers::import_wrapper<funcptr>(std::type_identity<::std::remove_pointer_t<decltype(funcptr)>>())); }

  void addMemory(void* memory, size_t size)
  {
    auto& newmem = m_memories.emplace_back(bytes{ static_cast<uint8_t*>(memory), size });
    m_imported_memories.emplace_back(ExternalMemory{ &newmem, Limits{ 0, size }});
  }



  bool run(void)
  {
    if(!m_module)
      return false;

    m_instance = instantiate(::std::move(m_module),
        m_imported_functions,
        {}, // imported_tables
        m_imported_memories,
        {}, // imported_globals
        m_memory_page_allocation_limit);
    return true;
  }


  template<typename Rtype, typename... Args>
    Rtype callExport(::std::string_view export_name, Args... args)
  {
    ::std::optional<ExternalFunction> exported = find_exported_function(*m_instance, export_name);
    if(!exported)
      throw bad_function_call(export_name);

    {
      auto input_ptr = exported->input_types.data();
      for(const helpers::arg_result_t& result : { helpers::arg_compare<Args>(input_ptr, input_ptr)... })
        if(!result())
          throw invalid_argument_type(export_name, result);
    }

    if(!helpers::matches_value_type<Rtype>(*exported->output_types.data()))
      throw invalid_return_type(export_name);


  }

protected:
  uint32_t m_memory_page_allocation_limit = (10 * 1024 * 1024ULL) / PageSize; // 10 MiB
  ::std::unique_ptr<Instance> m_instance;
  ::std::unique_ptr<const Module> m_module;
  ::std::vector<ExternalFunction> m_imported_functions;
  ::std::vector<ExternalMemory> m_imported_memories;
  ::std::vector<bytes> m_memories;
};
}

#endif // FIZZYHELPER_H
