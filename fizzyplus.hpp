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

#include <iostream>
#include <iomanip>


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
/*
  template<typename T> constexpr std::string_view type_id(T);
  template<> constexpr std::string_view type_id<uint32_t>(uint32_t) { return "uint32_t"; }
  template<> constexpr std::string_view type_id<int32_t>(int32_t) { return "int32_t"; }
  template<> constexpr std::string_view type_id<uint64_t>(uint64_t) { return "uint64_t"; }
  template<> constexpr std::string_view type_id<int64_t>(int64_t) { return "int64_t"; }
  template<> constexpr std::string_view type_id<float_t>(float_t) { return "float_t"; }
  template<> constexpr std::string_view type_id<double_t>(double_t) { return "double_t"; }
*/
    constexpr static inline std::string_view type_string(ValType vt)
    {
      switch(vt)
      {
      default: return "unknown";
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
    constexpr bool matches_value_type(ValType val) { return val == value_type<T>(); }

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
    constexpr arg_result_t arg_compare(const ValType* origin, const ValType*& pos)
    {
      return { origin - pos, *pos++, value_type<T>() };
    }

    template<typename T>
    constexpr T arg_expand(const Value*& pos) { return pos++->as<T>(); }

    // std::any& host_context, Instance&, const Value* args, ExecutionContext& ctx
    template<auto funcptr, typename Rtype, typename... Args>
    static ExecutionResult func_wrapper(::std::any& host_context, Instance& instance, const Value* vals, ExecutionContext& ctx) noexcept
    {
        static_assert(::std::is_same_v<Rtype, void> == false, "opps");
        if constexpr(::std::is_same_v<Rtype, void>)
        {
            reinterpret_cast<Rtype(*)(Args...)>(funcptr)(arg_expand<Args>(vals)...);
            return Void;
        }
        return ExecutionResult(Value(reinterpret_cast<Rtype(*)(Args...)>(funcptr)(arg_expand<Args>(vals)...)));
    };

    template<auto funcptr, typename Rtype, typename... Args>
    static inline ExternalFunction import_wrapper(std::type_identity<Rtype(Args...)>)
    {
      static ValType inputs[sizeof...(Args)] { value_type<Args>()... };
      static ValType output = value_type<Rtype>();

      return ExternalFunction
          {
              { func_wrapper<funcptr, Rtype, Args...> }, // function
              { inputs, sizeof...(Args) },        // input_types
              { &output, return_count<Rtype>() }, // output_types
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
              .append(" was provided."))
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



      bool invoke(void)
      {
        if(!m_module || m_instance)
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
        assert(m_instance);
        ::std::optional<FuncIdx> index = find_exported_function_index(*m_instance->module, export_name);
        if(!index.has_value())
          throw bad_function_call(export_name);

        {
          const auto& inputs = m_instance->module->get_function_type(*index).inputs;
          auto input_ptr = inputs.data();
          const auto origin_ptr = input_ptr;
          for(const helpers::arg_result_t& result : { helpers::arg_compare<Args>(origin_ptr, input_ptr)... })
            if(!result())
              throw invalid_argument_type(export_name, result);
        }

        if constexpr (!std::is_same<Rtype, void>())
        {
          if(const auto& outputs = m_instance->module->get_function_type(*index).outputs;
             !helpers::matches_value_type<Rtype>(outputs.at(0)))
            throw invalid_return_type(export_name);
        }

        std::array<fizzy::Value, sizeof...(args)> wasm_args = { args... };
        std::reverse(std::begin(wasm_args), std::end(wasm_args));
        if constexpr (std::is_same<Rtype, void>())
          fizzy::execute(*m_instance, *index, wasm_args.data());
        else
          return fizzy::execute(*m_instance, *index, wasm_args.data()).value.template as<Rtype>();
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
