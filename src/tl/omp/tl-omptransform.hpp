/*
    Mercurium C/C++ Compiler
    Copyright (C) 2006-2009 - Roger Ferrer Ibanez <roger.ferrer@bsc.es>
    Barcelona Supercomputing Center - Centro Nacional de Supercomputacion
    Universitat Politecnica de Catalunya

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/
#ifndef TL_OMPTRANSFORM_HPP
#define TL_OMPTRANSFORM_HPP

#include "cxx-utils.h"
#include "tl-omp.hpp"
#include "tl-ompserialize.hpp"
#include "tl-predicateutils.hpp"
#include "tl-source.hpp"
#include "tl-functionfilter.hpp"
#include "tl-parameterinfo.hpp"
#include "tl-nanos.hpp"
#include <iostream>
#include <utility>
#include <stack>
#include <set>
#include <fstream>

namespace TL
{
    namespace Nanos4
    {
        class OpenMPTransform : public OpenMP::OpenMPPhase
        {
            private:
                // Here we declare "persistent" variables

                // The number of parallel regions seen so far
                // (technically not, since it is updated in the postorder of these)
                int num_parallels;

                // The nesting for parallel, updated both in preorder and postorder
                int parallel_nesting;

                // A stack to save the number of "section" within a "sections".  If
                // just a scalar was used, we would only be able to handle one
                // level of sections
                std::stack<int> num_sections_stack;

                // Stores the innermost induction variable of a parallel for or for construct
                std::stack<Symbol> induction_var_stack;

                // Stores the non orphaned reduction of the enclosing parallel (if any)
                std::stack<ObjectList<OpenMP::ReductionSymbol> > inner_reductions_stack;

                // A set to save what critical names have been defined in
                // translation unit level
                std::set<std::string> criticals_defined;

                // Serialized functions info
                RefPtr<SerializedFunctionsInfo> serialized_functions_info;

                // -- Transactional world
                int transaction_nesting;
                std::fstream stm_log_file;
                bool stm_log_file_opened;

                // -- End of Transactional world

                /*
                 * Parameters of the phase where the textual parameter is stored
                 */
                std::string nanos_new_interface_str;
                std::string enable_mintaka_instr_str;
                std::string disable_restrict_str;
                std::string use_memcpy_always_str;
                std::string run_pretransform_str;

                std::string stm_replace_functions_file;
                std::string stm_replace_functions_mode;
                std::string stm_wrap_functions_file;
                std::string stm_wrap_functions_mode;
                std::string stm_global_lock_enabled_str;

                /* Parameter set_XXX functions */
                void set_instrumentation(const std::string& str);
                void set_parallel_interface(const std::string& str);
                void set_stm_global_lock(const std::string& str);
                void set_disable_restrict_pointers(const std::string& str);
                void set_use_memcpy_always(const std::string& str);
                void set_run_pretransform(const std::string& str);

                /*
                 * Logical values of parameters computed by the set_XXX functions
                 */
                bool enable_mintaka_instr;
                bool enable_nth_create;
                bool disable_restrict_pointers;
                bool use_memcpy_always;
                bool stm_global_lock_enabled;
                bool run_pretransform;
            public:
                OpenMPTransform();

                bool instrumentation_requested();

                virtual ~OpenMPTransform();

                // Entry point is overridden but calls OpenMP::OpenMPPhase::run
                virtual void run(DTO& dto);

                // Initialization function called from OpenMP::OpenMPPhase
                virtual void init(DTO& dto);

                void parallel_preorder(OpenMP::ParallelConstruct parallel_construct);
                void parallel_postorder(OpenMP::ParallelConstruct parallel_construct);

                void parallel_for_preorder(OpenMP::ParallelForConstruct parallel_for_construct);
                void parallel_for_postorder(OpenMP::ParallelForConstruct parallel_for_construct);

                void for_preorder(OpenMP::ForConstruct for_construct);
                void for_postorder(OpenMP::ForConstruct for_construct);

                void parallel_sections_preorder(OpenMP::ParallelSectionsConstruct parallel_sections_construct);

                void parallel_sections_postorder(OpenMP::ParallelSectionsConstruct parallel_sections_construct);

                void sections_preorder(OpenMP::SectionsConstruct sections_construct);
                void sections_postorder(OpenMP::SectionsConstruct sections_construct);

                void section_postorder(OpenMP::SectionConstruct section_construct);

                void barrier_postorder(OpenMP::BarrierDirective barrier_directive);

                void atomic_postorder(OpenMP::AtomicConstruct atomic_construct);

                void ordered_postorder(OpenMP::OrderedConstruct ordered_construct);

                void master_postorder(OpenMP::MasterConstruct master_construct);

                void single_postorder(OpenMP::SingleConstruct single_construct);

                void critical_postorder(OpenMP::CriticalConstruct critical_construct);

                void define_global_mutex(std::string mutex_variable, AST_t ref_tree, ScopeLink sl);

                void threadprivate_postorder(OpenMP::ThreadPrivateDirective threadprivate_directive);

                void task_preorder(OpenMP::TaskConstruct task_construct);
                void task_postorder(OpenMP::TaskConstruct task_construct);

                void taskwait_postorder(OpenMP::TaskWaitDirective taskwait_construct);

                void taskyield_postorder(OpenMP::CustomConstruct taskyield_construct);

                void taskgroup_postorder(OpenMP::CustomConstruct taskgroup_construct);

                void flush_postorder(OpenMP::FlushDirective flush_directive);

                void common_parallel_data_sharing_code(OpenMP::Construct &parallel_construct);

                AST_t get_parallel_spawn_code(
                        AST_t ref_tree, 
                        FunctionDefinition function_definition,
                        Scope scope,
                        ScopeLink scope_link,
                        ObjectList<ParameterInfo> parameter_info_list,
                        ObjectList<OpenMP::ReductionSymbol> reduction_references,
                        OpenMP::Clause if_clause,
                        OpenMP::Clause num_threads_clause,
                        OpenMP::CustomClause groups_clause,
                        Source& instrument_code_before,
                        Source& instrument_code_after);

                // Actual implementations of 'get_parallel_spawn_code'
                AST_t get_parallel_spawn_code_without_team(
                        AST_t ref_tree, 
                        FunctionDefinition function_definition,
                        Scope scope,
                        ScopeLink scope_link,
                        ObjectList<ParameterInfo> parameter_info_list,
                        ObjectList<OpenMP::ReductionSymbol> reduction_references,
                        OpenMP::Clause num_threads_clause,
                        OpenMP::CustomClause groups_clause,
                        Source& instrument_code_before,
                        Source& instrument_code_after);
                AST_t get_parallel_spawn_code_with_team(
                        AST_t ref_tree, 
                        FunctionDefinition function_definition,
                        Scope scope,
                        ScopeLink scope_link,
                        ObjectList<ParameterInfo> parameter_info_list,
                        ObjectList<OpenMP::ReductionSymbol> reduction_references,
                        OpenMP::Clause if_clause,
                        OpenMP::Clause num_threads_clause,
                        OpenMP::CustomClause groups_clause,
                        Source& instrument_code_before,
                        Source& instrument_code_after);

                std::string get_outline_function_reference(FunctionDefinition function_definition,
                        ObjectList<ParameterInfo>& parameter_info_list,
                        bool team_parameter);

                Source get_critical_reduction_code(ObjectList<OpenMP::ReductionSymbol> reduction_references);
                Source get_noncritical_reduction_code(ObjectList<OpenMP::ReductionSymbol> reduction_references);
                Source get_noncritical_inlined_reduction_code(
                        ObjectList<OpenMP::ReductionSymbol> reduction_references,
                        Statement inner_statement);
                Source get_reduction_update(ObjectList<OpenMP::ReductionSymbol> reduction_references);
                Source get_reduction_gathering(ObjectList<OpenMP::ReductionSymbol> reduction_references);

                Source get_member_function_declaration(
                        FunctionDefinition function_definition,
                        Declaration function_declaration,
                        Source outlined_function_name,
                        ObjectList<ParameterInfo> parameter_info_list,
                        bool team_parameter
                        );

                Source get_outline_common(
                        FunctionDefinition function_definition,
                        Source& specific_body,
                        Source outlined_function_name,
                        ObjectList<ParameterInfo> parameter_info_list,
                        OpenMP::Construct &construct,
                        bool team_parameter = false
                        );

                Source get_formal_parameters(
                        FunctionDefinition function_definition,
                        ObjectList<ParameterInfo> parameter_info_list,
                        Scope decl_scope,
                        bool team_parameter = false);

                AST_t get_outline_parallel(
                        OpenMP::Construct &construct,
                        FunctionDefinition function_definition,
                        Source outlined_function_name,
                        Statement construct_body,
                        ReplaceIdExpression replace_references,
                        ObjectList<ParameterInfo> parameter_info_list,
                        ObjectList<Symbol> private_references,
                        ObjectList<Symbol> firstprivate_references,
                        ObjectList<Symbol> lastprivate_references,
                        ObjectList<OpenMP::ReductionSymbol> reduction_references,
                        ObjectList<Symbol> copyin_references,
                        ObjectList<Symbol> copyprivate_references,
                        bool never_instrument = false
                        );

                AST_t get_outline_task(
                        OpenMP::Construct &construct,
                        FunctionDefinition function_definition,
                        Source outlined_function_name,
                        Statement construct_body,
                        ReplaceIdExpression replace_references,
                        ObjectList<ParameterInfo> parameter_info_list,
                        ObjectList<Symbol> local_references
                        );


                AST_t get_outline_parallel_sections(
                        OpenMP::Construct &construct,
                        FunctionDefinition function_definition,
                        Source outlined_function_name, 
                        Statement construct_body,
                        ReplaceIdExpression replace_references,
                        ObjectList<ParameterInfo> parameter_info_list,
                        ObjectList<Symbol> private_references,
                        ObjectList<Symbol> firstprivate_references,
                        ObjectList<Symbol> lastprivate_references,
                        ObjectList<OpenMP::ReductionSymbol> reduction_references,
                        ObjectList<Symbol> copyin_references,
                        ObjectList<Symbol> copyprivate_references);

                // Create outline for parallel for
                AST_t get_outline_parallel_for(
                        OpenMP::Construct &construct,
                        FunctionDefinition function_definition,
                        Source outlined_function_name,
                        ForStatement for_statement,
                        Statement loop_body,
                        ReplaceIdExpression replace_references,
                        ObjectList<ParameterInfo> parameter_info_list,
                        ObjectList<Symbol> private_references,
                        ObjectList<Symbol> firstprivate_references,
                        ObjectList<Symbol> lastprivate_references,
                        ObjectList<OpenMP::ReductionSymbol> reduction_references,
                        ObjectList<Symbol> copyin_references,
                        ObjectList<Symbol> copyprivate_references,
                        OpenMP::Directive directive
                        );

                void get_data_explicit_attributes(
                        OpenMP::Construct &construct,
                        OpenMP::Directive directive,
                        ObjectList<Symbol>& shared_references,
                        ObjectList<Symbol>& private_references,
                        ObjectList<Symbol>& firstprivate_references,
                        ObjectList<Symbol>& lastprivate_references,
                        ObjectList<OpenMP::ReductionSymbol>& reduction_references,
                        ObjectList<Symbol>& copyin_references,
                        ObjectList<Symbol>& copyprivate_references);

                void get_data_attributes(
                        OpenMP::Construct &construct,
                        OpenMP::Directive directive,
                        Statement construct_body,
                        ObjectList<Symbol>& shared_references,
                        ObjectList<Symbol>& private_references,
                        ObjectList<Symbol>& firstprivate_references,
                        ObjectList<Symbol>& lastprivate_references,
                        ObjectList<OpenMP::ReductionSymbol>& reduction_references,
                        ObjectList<Symbol>& copyin_references,
                        ObjectList<Symbol>& copyprivate_references);

                ReplaceIdExpression set_replacements(FunctionDefinition function_definition,
                        OpenMP::Directive directive,
                        Statement construct_body,
                        ObjectList<Symbol>& shared_references,
                        ObjectList<Symbol>& private_references,
                        ObjectList<Symbol>& firstprivate_references,
                        ObjectList<Symbol>& lastprivate_references,
                        ObjectList<OpenMP::ReductionSymbol>& reduction_references,
                        ObjectList<OpenMP::ReductionSymbol>& inner_reduction_references,
                        ObjectList<Symbol>& copyin_references,
                        ObjectList<Symbol>& copyprivate_references,
                        ObjectList<ParameterInfo>& parameter_info,
                        bool share_always = false);

                ReplaceIdExpression set_replacements_inline(FunctionDefinition function_definition,
                        OpenMP::Directive,
                        Statement construct_body,
                        ObjectList<Symbol>& shared_references,
                        ObjectList<Symbol>& private_references,
                        ObjectList<Symbol>& firstprivate_references,
                        ObjectList<Symbol>& lastprivate_references,
                        ObjectList<OpenMP::ReductionSymbol>& reduction_references,
                        ObjectList<OpenMP::ReductionSymbol>& inner_reduction_references,
                        ObjectList<Symbol>& copyin_references,
                        ObjectList<Symbol>& copyprivate_references);

                Source get_loop_distribution_in_sections(
                        int num_sections,
                        Statement construct_body,
                        ReplaceIdExpression replace_references);

                Source get_loop_distribution_code(ForStatement for_statement,
                        OpenMP::Construct &construct,
                        ReplaceIdExpression replace_references,
                        FunctionDefinition function_definition,
                        OpenMP::Directive directive);

                Source get_loop_finalization(bool do_barrier);

                Source get_privatized_declarations(
                        OpenMP::Construct &construct,
                        ObjectList<Symbol> private_references,
                        ObjectList<Symbol> firstprivate_references,
                        ObjectList<Symbol> lastprivate_references,
                        ObjectList<OpenMP::ReductionSymbol> reduction_references,
                        ObjectList<Symbol> copyin_references,
                        ObjectList<ParameterInfo> parameter_info_list
                        );

                Source get_privatized_declarations_inline(
                        OpenMP::Construct &construct,
                        ObjectList<Symbol> private_references,
                        ObjectList<Symbol> firstprivate_references,
                        ObjectList<Symbol> lastprivate_references,
                        ObjectList<OpenMP::ReductionSymbol> reduction_references,
                        ObjectList<Symbol> copyin_references
                        );

                Source get_lastprivate_assignments(
                        ObjectList<Symbol> firstprivate_references,
                        ObjectList<Symbol> lastprivate_references,
                        ObjectList<Symbol> copyprivate_references,
                        ObjectList<ParameterInfo> parameter_info_list);

                Source get_lastprivate_assignments_inline(
                        ObjectList<Symbol> lastprivate_references,
                        ObjectList<Symbol> copyprivate_references);

                Source get_outlined_function_name(IdExpression function_name, 
                        bool want_fully_qualified = true, 
                        bool want_templated_name = false);

                bool is_nonstatic_member_function(FunctionDefinition function_definition);

                Source array_copy(Type t, const std::string& dest, const std::string& orig, int level);

                bool is_unqualified_member_symbol(Symbol current_symbol, FunctionDefinition function_definition);

                bool is_function_accessible(Symbol current_symbol);

                void instrumentation_outline(Source& instrumentation_code_before,
                        Source& instrumentation_code_after,
                        FunctionDefinition function_definition,
                        Source outlined_function_name,
                        OpenMP::Construct &construct);

                Symbol warn_unreferenced_data(Symbol sym);

                Symbol warn_no_data_sharing(Symbol sym);

                // Dependences support
                void handle_dependences(OpenMP::Directive directive,
                        ObjectList<Expression> &input_dependences,
                        ObjectList<Expression> &output_dependences,
                        OpenMP::Construct &task_construct,
                        ObjectList<Symbol>& captureaddress_references,
                        ObjectList<Symbol>& captureprivate_references);

                static Symbol handle_dep_expr(Expression expr);
                static Symbol handle_scalar_dep_expr(Expression expr);

                Source debug_parameter_info(
                        ObjectList<ParameterInfo> parameter_info_list);

                void add_data_attribute_to_list(
                        OpenMP::Construct &construct,
                        ObjectList<Symbol> list_id_expressions,
                        OpenMP::DataAttribute data_attrib);

                // #pragma omp task special support
                void task_compute_explicit_data_sharing(
                        OpenMP::Directive &directive,
                        ObjectList<Symbol> &captureaddress_references,
                        ObjectList<Symbol> &local_references,
                        ObjectList<Symbol> &captureprivate_references,
                        Scope &function_scope,
                        FunctionDefinition &function_definition,
                        OpenMP::Construct &task_construct);

                void task_compute_implicit_data_sharing(
                        OpenMP::Directive &directive,
                        ObjectList<Symbol> &captureaddress_references,
                        ObjectList<Symbol> &local_references,
                        ObjectList<Symbol> &captureprivate_references,
                        Scope &function_scope,
                        FunctionDefinition &function_definition,
                        Statement &construct_body,
                        OpenMP::Construct &construct);

                Source task_get_spawn_code(
                        ObjectList<ParameterInfo> &parameter_info_list,
                        FunctionDefinition &function_definition,
                        OpenMP::Construct &task_construct,
                        OpenMP::Directive &directive,
                        Statement& construct_body,
                        AST_t& original_code);

                // Debug purposes
                IdExpression print_id_expression(IdExpression id_expression);

                // --- Transactional world --
                class STMExpressionReplacement;

                void stm_transaction_preorder(OpenMP::CustomConstruct protect_construct);
                void stm_transaction_postorder(OpenMP::CustomConstruct protect_construct);

                void stm_retry_postorder(OpenMP::CustomConstruct protect_construct);

                void stm_preserve_postorder(OpenMP::CustomConstruct protect_construct);

                void stm_transaction_full_stm(OpenMP::CustomConstruct transaction_construct);
                void stm_transaction_global_lock(OpenMP::CustomConstruct transaction_construct);

                void stm_replace_init_declarators(AST_t transaction_tree,
                        STMExpressionReplacement &expression_replacement,
                        ScopeLink scope_link);
                void stm_replace_returns(AST_t transaction_tree, 
                        bool from_wrapped_function, ScopeLink scope_link);
                void stm_replace_expressions(AST_t transaction_tree,
                        STMExpressionReplacement &expression_replacement,
                        ScopeLink scope_link);
                void stm_replace_code(
                        OpenMP::CustomConstruct transaction_construct,
                        AST_t&replaced_tree, 
                        AST_t& inner_tree,
                        ObjectList<Symbol> &local_symbols,
                        bool from_wrapped_function);

                // ADF part of STM
                void adf_task_preorder(OpenMP::CustomConstruct adf_construct);
                void adf_task_postorder(OpenMP::CustomConstruct adf_construct);

                // --- End of transactional world --
                
                void declare_member_if_needed(Symbol function_symbol,
                        FunctionDefinition function_definition,
                        IdExpression function_name,
                        ObjectList<ParameterInfo> parameter_info_list,
                        bool team_parameter);
                void invoke_destructors(ObjectList<ParameterInfo> parameter_info_list, 
                        Source &destructor_calls);
                AST_t finish_outline(FunctionDefinition function_definition, Source outline_parallel,
                        ObjectList<ParameterInfo> parameter_info_list,
                        bool team_parameter);
        };

    } // Nanos4
} // TL

#endif // TL_OMPTRANSFORM_HPP
