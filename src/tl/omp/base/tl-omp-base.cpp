/*--------------------------------------------------------------------
  (C) Copyright 2006-2013 Barcelona Supercomputing Center
                          Centro Nacional de Supercomputacion

  This file is part of Mercurium C/C++ source-to-source compiler.

  See AUTHORS file in the top level directory for information
  regarding developers and contributors.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 3 of the License, or (at your option) any later version.

  Mercurium C/C++ source-to-source compiler is distributed in the hope
  that it will be useful, but WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
  PURPOSE.  See the GNU Lesser General Public License for more
  details.

  You should have received a copy of the GNU Lesser General Public
  License along with Mercurium C/C++ source-to-source compiler; if
  not, write to the Free Software Foundation, Inc., 675 Mass Ave,
  Cambridge, MA 02139, USA.
--------------------------------------------------------------------*/

#include "tl-omp-base.hpp"
#include "tl-omp-base-task.hpp"
#include "tl-omp-base-utils.hpp"

#include "config.h"
#include "tl-nodecl-utils.hpp"
#include "tl-predicateutils.hpp"
#include "tl-counters.hpp"
#include "tl-compilerpipeline.hpp"

#include "cxx-diagnostic.h"
#include "cxx-cexpr.h"
#include "fortran03-scope.h"

#include <algorithm>
#include <iterator>

#include "cxx-graphviz.h"

namespace TL { namespace OpenMP {

    namespace Report
    {
        const char indent[] = "  ";
    }

    Base::Base()
        : PragmaCustomCompilerPhase("omp"),
        _core(),
        _simd_enabled(false),
        _ompss_mode(false),
        _omp_report(false),
        _copy_deps_by_default(true),
        _untied_tasks_by_default(true)
    {
        set_phase_name("OpenMP directive to parallel IR");
        set_phase_description("This phase lowers the semantics of OpenMP into the parallel IR of Mercurium");

        register_parameter("omp_dry_run",
                "Disables OpenMP transformation",
                _openmp_dry_run,
                "0");

        register_parameter("discard_unused_data_sharings",
                "Discards unused data sharings in the body of the construct. "
                "This behaviour may cause wrong code be emitted, use at your own risk",
                _discard_unused_data_sharings_str,
                "0").connect(functor(&Base::set_discard_unused_data_sharings, *this));

        register_parameter("simd_enabled",
                "If set to '1' enables simd constructs, otherwise it is disabled",
                _simd_enabled_str,
                "0").connect(functor(&Base::set_simd, *this));

        register_parameter("allow_shared_without_copies",
                "If set to '1' allows shared without any copy directionality, otherwise they are set to copy_inout",
                _allow_shared_without_copies_str,
                "0").connect(functor(&Base::set_allow_shared_without_copies, *this));

        register_parameter("allow_array_reductions",
                "If set to '1' enables extended support for array reductions in C/C++",
                _allow_array_reductions_str,
                "1").connect(functor(&Base::set_allow_array_reductions, *this));

        register_parameter("ompss_mode",
                "Enables OmpSs semantics instead of OpenMP semantics",
                _ompss_mode_str,
                "0").connect(functor(&Base::set_ompss_mode, *this));

        register_parameter("omp_report",
                "Emits an OpenMP report describing the OpenMP semantics of the code",
                _omp_report_str,
                "0").connect(functor(&Base::set_omp_report, *this));

        register_parameter("copy_deps_by_default",
                "Enables copy_deps by default",
                _copy_deps_str,
                "1").connect(functor(&Base::set_copy_deps_by_default, *this));

        register_parameter("untied_tasks_by_default",
                "If set to '1' tasks are untied by default, otherwise they are tied. This flag is only valid in OmpSs",
                _untied_tasks_by_default_str,
                "1").connect(functor(&Base::set_untied_tasks_by_default, *this));

        register_parameter("disable_task_expression_optimization",
                "Disables some optimizations applied to task expressions",
                _disable_task_expr_optim_str,
                "0");

#define OMP_DIRECTIVE(_directive, _name, _pred) \
                if (_pred) { \
                    std::string directive_name = remove_separators_of_directive(_directive); \
                    dispatcher().directive.pre[directive_name].connect(functor(&Base::_name##_handler_pre, *this)); \
                    dispatcher().directive.post[directive_name].connect(functor(&Base::_name##_handler_post, *this)); \
                }
#define OMP_CONSTRUCT_COMMON(_directive, _name, _noend, _pred) \
                if (_pred) { \
                    std::string directive_name = remove_separators_of_directive(_directive); \
                    dispatcher().declaration.pre[directive_name].connect(functor((void (Base::*)(TL::PragmaCustomDeclaration))&Base::_name##_handler_pre, *this)); \
                    dispatcher().declaration.post[directive_name].connect(functor((void (Base::*)(TL::PragmaCustomDeclaration))&Base::_name##_handler_post, *this)); \
                    dispatcher().statement.pre[directive_name].connect(functor((void (Base::*)(TL::PragmaCustomStatement))&Base::_name##_handler_pre, *this)); \
                    dispatcher().statement.post[directive_name].connect(functor((void (Base::*)(TL::PragmaCustomStatement))&Base::_name##_handler_post, *this)); \
                }
#define OMP_CONSTRUCT(_directive, _name, _pred) OMP_CONSTRUCT_COMMON(_directive, _name, false, _pred)
#define OMP_CONSTRUCT_NOEND(_directive, _name, _pred) OMP_CONSTRUCT_COMMON(_directive, _name, true, _pred)
#include "tl-omp-constructs.def"
#undef OMP_DIRECTIVE
#undef OMP_CONSTRUCT_COMMON
#undef OMP_CONSTRUCT
#undef OMP_CONSTRUCT_NOEND
    }

    void Base::pre_run(TL::DTO& dto)
    {
        _core.pre_run(dto);

        // Do nothing once we have analyzed everything
        if (_openmp_dry_run != "0")
            return;

        this->PragmaCustomCompilerPhase::pre_run(dto);
    }

    void Base::run(TL::DTO& dto)
    {
        if (CURRENT_CONFIGURATION->explicit_instantiation)
        {
            this->set_ignore_template_functions(true);
        }

        _core.run(dto);

        if (diagnostics_get_error_count() != 0)
            return;

        // Do nothing once we have analyzed everything
        if (_openmp_dry_run != "0")
            return;

        if (emit_omp_report())
        {
            TL::CompiledFile current = TL::CompilationProcess::get_current_file();
            std::string report_filename = current.get_filename() + "." +
                    std::string(in_ompss_mode() ? "ompss.report" : "openmp.report");

            info_printf("%s: creating %s report in '%s'\n",
                    current.get_filename().c_str(),
                    in_ompss_mode() ? "OmpSs" : "OpenMP",
                    report_filename.c_str());

            _omp_report_file = new std::ofstream(report_filename.c_str());
            *_omp_report_file
                << (in_ompss_mode() ? "OmpSs " : "OpenMP ") << "Report for file '" << current.get_filename() << "'\n"
                << "=================================================================\n";
        }

        this->PragmaCustomCompilerPhase::run(dto);

        RefPtr<FunctionTaskSet> function_task_set = RefPtr<FunctionTaskSet>::cast_static(dto["openmp_task_info"]);

        Nodecl::NodeclBase translation_unit = dto["nodecl"];

        bool task_expr_optim_disabled = (_disable_task_expr_optim_str == "1");
        TransformNonVoidFunctionCalls transform_nonvoid_task_calls(function_task_set, task_expr_optim_disabled,
                /* ignore_template_functions */ CURRENT_CONFIGURATION->explicit_instantiation);
        transform_nonvoid_task_calls.walk(translation_unit);
        transform_nonvoid_task_calls.remove_nonvoid_function_tasks_from_function_task_set();

        const std::map<Nodecl::NodeclBase, Nodecl::NodeclBase>& funct_call_to_enclosing_stmt_map =
            transform_nonvoid_task_calls.get_function_call_to_enclosing_stmt_map();

        const std::map<Nodecl::NodeclBase, Nodecl::NodeclBase>& enclosing_stmt_to_original_stmt_map =
            transform_nonvoid_task_calls.get_enclosing_stmt_to_original_stmt_map();

        const std::map<Nodecl::NodeclBase, std::set<TL::Symbol> >& enclosing_stmt_to_return_vars_map =
            transform_nonvoid_task_calls.get_enclosing_stmt_to_return_variables_map();

        FunctionCallVisitor function_call_visitor(
                function_task_set,
                funct_call_to_enclosing_stmt_map,
                enclosing_stmt_to_original_stmt_map,
                enclosing_stmt_to_return_vars_map,
                this,
                /* ignore_template_functions */ CURRENT_CONFIGURATION->explicit_instantiation);

        function_call_visitor.walk(translation_unit);
        function_call_visitor.build_all_needed_task_expressions();

        if (emit_omp_report())
        {
            *_omp_report_file
                << "\n=================================================================\n"
                << "End of report\n"
                << std::endl;
            _omp_report_file->close();
            _omp_report_file = NULL;
        }
    }

    void Base::phase_cleanup(DTO& data_flow)
    {
        _core.phase_cleanup(data_flow);
    }

    void Base::phase_cleanup_end_of_pipeline(DTO& data_flow)
    {
        _core.phase_cleanup_end_of_pipeline(data_flow);
    }

#define INVALID_STATEMENT_HANDLER(_name) \
        void Base::_name##_handler_pre(TL::PragmaCustomStatement ctr) { \
            error_printf("%s: error: invalid '#pragma %s %s'\n",  \
                    ctr.get_locus_str().c_str(), \
                    ctr.get_text().c_str(), \
                    ctr.get_pragma_line().get_text().c_str()); \
        } \
        void Base::_name##_handler_post(TL::PragmaCustomStatement) { }

#define INVALID_DECLARATION_HANDLER(_name) \
        void Base::_name##_handler_pre(TL::PragmaCustomDeclaration ctr) { \
            error_printf("%s: error: invalid '#pragma %s %s'\n",  \
                    ctr.get_locus_str().c_str(), \
                    ctr.get_text().c_str(), \
                    ctr.get_pragma_line().get_text().c_str()); \
        } \
        void Base::_name##_handler_post(TL::PragmaCustomDeclaration) { }

        INVALID_DECLARATION_HANDLER(parallel)
        INVALID_DECLARATION_HANDLER(parallel_for)
        INVALID_DECLARATION_HANDLER(parallel_simd_for)
        INVALID_DECLARATION_HANDLER(parallel_do)
        INVALID_DECLARATION_HANDLER(for)
        INVALID_DECLARATION_HANDLER(simd_for)
        INVALID_DECLARATION_HANDLER(simd_parallel)
        INVALID_DECLARATION_HANDLER(do)
        INVALID_DECLARATION_HANDLER(parallel_sections)
        INVALID_DECLARATION_HANDLER(sections)
        INVALID_DECLARATION_HANDLER(single)
        INVALID_DECLARATION_HANDLER(workshare)
        INVALID_DECLARATION_HANDLER(critical)
        INVALID_DECLARATION_HANDLER(atomic)
        INVALID_DECLARATION_HANDLER(master)

#define EMPTY_HANDLERS_CONSTRUCT(_name) \
        void Base::_name##_handler_pre(TL::PragmaCustomStatement) { } \
        void Base::_name##_handler_post(TL::PragmaCustomStatement) { } \
        void Base::_name##_handler_pre(TL::PragmaCustomDeclaration) { } \
        void Base::_name##_handler_post(TL::PragmaCustomDeclaration) { } \

#define EMPTY_HANDLERS_DIRECTIVE(_name) \
        void Base::_name##_handler_pre(TL::PragmaCustomDirective) { } \
        void Base::_name##_handler_post(TL::PragmaCustomDirective) { }

        EMPTY_HANDLERS_CONSTRUCT(ordered)

        EMPTY_HANDLERS_DIRECTIVE(section)

    void Base::set_simd(const std::string &simd_enabled_str)
    {
        parse_boolean_option("simd_enabled",
                simd_enabled_str,
                _simd_enabled,
                "Assuming false");
    }

    void Base::set_ompss_mode(const std::string& str)
    {
        parse_boolean_option("ompss_mode", str, _ompss_mode, "Assuming false.");
        _core.set_ompss_mode(_ompss_mode);
    }

    void Base::set_omp_report(const std::string& str)
    {
        parse_boolean_option("omp_report", str, _omp_report, "Assuming false.");
    }

    bool Base::in_ompss_mode() const
    {
        return _ompss_mode;
    }

    bool Base::emit_omp_report() const
    {
        return _omp_report;
    }

    void Base::set_copy_deps_by_default(const std::string& str)
    {
        parse_boolean_option("copy_deps", str, _copy_deps_by_default, "Assuming true.");
        _core.set_copy_deps_by_default(_copy_deps_by_default);
    }

    bool Base::copy_deps_by_default() const
    {
        return _copy_deps_by_default;
    }

    void Base::set_untied_tasks_by_default(const std::string& str)
    {
         parse_boolean_option("untied_tasks", str, _untied_tasks_by_default, "Assuming true.");
        _core.set_untied_tasks_by_default(_untied_tasks_by_default);
    }

    bool Base::untied_tasks_by_default() const
    {
        return _untied_tasks_by_default;
    }

    void Base::set_allow_shared_without_copies(const std::string &allow_shared_without_copies_str)
    {
        bool b = false;
        parse_boolean_option("allow_shared_without_copies",
                allow_shared_without_copies_str, b, "Assuming false");
        _core.set_allow_shared_without_copies(b);
    }

    void Base::set_allow_array_reductions(const std::string &allow_array_reductions)
    {
        bool b = true;
        parse_boolean_option("allow_array_reductions",
                allow_array_reductions, b, "Assuming true");
        _core.set_allow_array_reductions(b);
    }

    void Base::set_discard_unused_data_sharings(const std::string& str)
    {
        bool b = false;
        parse_boolean_option("discard_unused_data_sharings",
                str,
                b,
                "Assuming false");
        _core.set_discard_unused_data_sharings(b);
    }

    void Base::atomic_handler_pre(TL::PragmaCustomStatement) { }
    void Base::atomic_handler_post(TL::PragmaCustomStatement directive)
    {
        TL::PragmaCustomLine pragma_line = directive.get_pragma_line();

        Nodecl::List execution_environment = Nodecl::List::make(
                Nodecl::OpenMP::FlushAtEntry::make(
                        directive.get_locus()),
                Nodecl::OpenMP::FlushAtExit::make(
                        directive.get_locus())
        );

        if (emit_omp_report())
        {
            *_omp_report_file
                << "\n"
                << directive.get_locus_str() << ": " << "ATOMIC construct\n"
                << directive.get_locus_str() << ": " << "----------------\n"
                << OpenMP::Report::indent << directive.get_statements().prettyprint() << "\n"
                ;
        }

        Nodecl::OpenMP::Atomic atomic =
            Nodecl::OpenMP::Atomic::make(
                    execution_environment,
                    directive.get_statements().shallow_copy(),
                    directive.get_locus());

        pragma_line.diagnostic_unused_clauses();
        directive.replace(atomic);
    }

    void Base::critical_handler_pre(TL::PragmaCustomStatement) { }
    void Base::critical_handler_post(TL::PragmaCustomStatement directive)
    {
        TL::PragmaCustomLine pragma_line = directive.get_pragma_line();

        TL::PragmaCustomParameter param = pragma_line.get_parameter();

        Nodecl::List execution_environment;

        Nodecl::OpenMP::FlushAtEntry entry_flush =
            Nodecl::OpenMP::FlushAtEntry::make(
                    directive.get_locus()
            );

        Nodecl::OpenMP::FlushAtExit exit_flush =
            Nodecl::OpenMP::FlushAtExit::make(
                    directive.get_locus()
            );

        if (emit_omp_report())
        {
            *_omp_report_file
                << "\n"
                << directive.get_locus_str() << ": " << "CRITICAL construct\n"
                << directive.get_locus_str() << ": " << "------------------\n"
                ;
        }

        if (param.is_defined())
        {
            ObjectList<std::string> critical_name = param.get_tokenized_arguments();

            execution_environment = Nodecl::List::make(
                    Nodecl::OpenMP::CriticalName::make(critical_name[0],
                        directive.get_locus()),
                    entry_flush, exit_flush);

            if (emit_omp_report())
            {
                *_omp_report_file
                    << OpenMP::Report::indent
                    << "Named critical construct: '" << critical_name[0] << "'\n";
            }
        }
        else
        {
            execution_environment = Nodecl::List::make(entry_flush, exit_flush);

            if (emit_omp_report())
            {
                *_omp_report_file
                    << OpenMP::Report::indent
                    << "Unnamed critical construct\n";
            }
        }

        pragma_line.diagnostic_unused_clauses();
        directive.replace(
                Nodecl::OpenMP::Critical::make(
                        execution_environment,
                        directive.get_statements().shallow_copy(),
                        directive.get_locus())
                );
    }

    void Base::barrier_handler_pre(TL::PragmaCustomDirective) { }
    void Base::barrier_handler_post(TL::PragmaCustomDirective directive)
    {
        TL::PragmaCustomLine pragma_line = directive.get_pragma_line();

        if (emit_omp_report())
        {
            *_omp_report_file
                << "\n"
                << directive.get_locus_str() << ": " << "BARRIER construct\n"
                << directive.get_locus_str() << ": " << "-----------------\n"
                << OpenMP::Report::indent << "(There is no more information for BARRIER)\n"
                ;
        }

        Nodecl::List execution_environment = Nodecl::List::make(
                Nodecl::OpenMP::FlushAtEntry::make(
                    directive.get_locus()),
                Nodecl::OpenMP::FlushAtExit::make(
                    directive.get_locus())
                );

        pragma_line.diagnostic_unused_clauses();
        directive.replace(
                Nodecl::OpenMP::BarrierFull::make(
                    execution_environment,
                    directive.get_locus())
                );
    }

    void Base::flush_handler_pre(TL::PragmaCustomDirective) { }
    void Base::flush_handler_post(TL::PragmaCustomDirective directive)
    {
        TL::PragmaCustomLine pragma_line = directive.get_pragma_line();
        PragmaClauseArgList parameter = directive.get_pragma_line().get_parameter();

        if (emit_omp_report())
        {
            *_omp_report_file
                << "\n"
                << directive.get_locus_str() << ": " << "FLUSH construct\n"
                << directive.get_locus_str() << ": " << "---------------\n"
                << OpenMP::Report::indent << "(There is no more information for FLUSH)\n"
                ;
        }

        TL::ObjectList<Nodecl::NodeclBase> expr_list;
        if (!parameter.is_null())
        {
            expr_list = parameter.get_arguments_as_expressions();
        }

        pragma_line.diagnostic_unused_clauses();
        directive.replace(
                Nodecl::OpenMP::FlushMemory::make(
                    Nodecl::List::make(expr_list),
                    directive.get_locus())
                );
    }

    void Base::master_handler_pre(TL::PragmaCustomStatement) { }
    void Base::master_handler_post(TL::PragmaCustomStatement directive)
    {
        TL::PragmaCustomLine pragma_line = directive.get_pragma_line();

        if (emit_omp_report())
        {
            *_omp_report_file
                << "\n"
                << directive.get_locus_str() << ": " << "MASTER construct\n"
                << directive.get_locus_str() << ": " << "----------------\n"
                << OpenMP::Report::indent << "(There is no more information for MASTER)\n"
                ;
        }

        pragma_line.diagnostic_unused_clauses();
        directive.replace(
                Nodecl::OpenMP::Master::make(
                    directive.get_statements().shallow_copy(),
                    directive.get_locus())
                );
    }

    void Base::taskwait_handler_pre(TL::PragmaCustomDirective) { }
    void Base::taskwait_handler_post(TL::PragmaCustomDirective directive)
    {
        PragmaCustomLine pragma_line = directive.get_pragma_line();

        if (emit_omp_report())
        {
            *_omp_report_file
                << "\n"
                << directive.get_locus_str() << ": " << "TASKWAIT construct\n"
                << directive.get_locus_str() << ": " << "------------------\n"
                ;

            if (pragma_line.get_clause("on").is_defined())
            {
                *_omp_report_file
                    << OpenMP::Report::indent
                    << "This taskwait contains an 'on' clause\n"
                    ;
            }
        }

        OpenMP::DataSharingEnvironment &data_sharing = _core.get_openmp_info()->get_data_sharing(directive);
        Nodecl::List environment = this->make_execution_environment(
                data_sharing,
                pragma_line,
                /* ignore_target_info */ true);

        PragmaCustomClause noflush_clause = pragma_line.get_clause("noflush");
        if (noflush_clause.is_defined())
        {
            environment.append(
                    Nodecl::OpenMP::NoFlush::make(directive.get_locus()));
            if (emit_omp_report())
            {
                *_omp_report_file
                    << OpenMP::Report::indent
                    << "This taskwait does not flush device overlaps due to 'noflush' clause\n"
                    ;
            }
        }
        else
        {
            if (emit_omp_report())
            {
                *_omp_report_file
                    << OpenMP::Report::indent
                    << "This taskwait flushes device overlaps (if any device is used)\n"
                    ;
            }
        }

        pragma_line.diagnostic_unused_clauses();

        TL::ObjectList<OpenMP::DependencyItem> dependences;
        data_sharing.get_all_dependences(dependences);
        if (!dependences.empty())
        {
            directive.replace(
                    Nodecl::OpenMP::WaitOnDependences::make(
                        environment,
                        directive.get_locus())
                    );
        }
        else
        {
            if (emit_omp_report())
            {
                *_omp_report_file
                    << OpenMP::Report::indent
                    << "This taskwait waits for all tasks created in the current context\n"
                    ;
            }
            directive.replace(
                    Nodecl::OpenMP::TaskwaitShallow::make(
                        environment,
                        directive.get_locus())
                    );
        }
    }


    void Base::taskyield_handler_pre(TL::PragmaCustomDirective) { }
    void Base::taskyield_handler_post(TL::PragmaCustomDirective directive)
    {
        PragmaCustomLine pragma_line = directive.get_pragma_line();

        if (emit_omp_report())
        {
            *_omp_report_file
                << "\n"
                << directive.get_locus_str() << ": " << "TASKYIELD construct\n"
                << directive.get_locus_str() << ": " << "------------------\n"
                ;
        }

        directive.replace(
                Nodecl::OpenMP::Taskyield::make(
                    directive.get_locus())
                );
    }

    // Inline tasks
    void Base::task_handler_pre(TL::PragmaCustomStatement) { }
    void Base::task_handler_post(TL::PragmaCustomStatement directive)
    {
        OpenMP::DataSharingEnvironment &ds = _core.get_openmp_info()->get_data_sharing(directive);
        PragmaCustomLine pragma_line = directive.get_pragma_line();

        if (emit_omp_report())
        {
            *_omp_report_file
                << "\n"
                << directive.get_locus_str() << ": " << "TASK construct\n"
                << directive.get_locus_str() << ": " << "--------------\n"
                ;
        }

        Nodecl::List execution_environment = this->make_execution_environment(ds, pragma_line, /* ignore_target_info */ false);

        PragmaCustomClause tied = pragma_line.get_clause("tied");
        PragmaCustomClause untied = pragma_line.get_clause("untied");
        if (untied.is_defined()
                // The tasks are untied by default and the current task has not defined the 'tied' clause
                || (_untied_tasks_by_default && !tied.is_defined()))
        {
            execution_environment.append(
                    Nodecl::OpenMP::Untied::make(
                        directive.get_locus()));

            if (emit_omp_report())
            {
                *_omp_report_file
                    << OpenMP::Report::indent
                    << "This is an untied task. The thread that executes the task may change "
                    "during the execution of the task (i.e. because of preemptions)\n"
                    ;
            }
        }
        else
        {
            if (emit_omp_report())
            {
                *_omp_report_file
                    << OpenMP::Report::indent
                    << "This is a tied task. The thread that executes the task will not change "
                    "during the execution of the task\n"
                    ;
            }
        }

        PragmaCustomClause priority = pragma_line.get_clause("priority");
        {
            TL::ObjectList<Nodecl::NodeclBase> expr_list = priority.get_arguments_as_expressions(directive);

            if (priority.is_defined()
                    && expr_list.size() == 1)
            {
                if (emit_omp_report())
                {
                    *_omp_report_file
                        << OpenMP::Report::indent
                        << "This task has priority '" << expr_list[0].prettyprint() << "'\n";
                    ;
                }
                execution_environment.append(
                        Nodecl::OpenMP::Priority::make(
                            expr_list[0],
                            directive.get_locus()));
            }
            else
            {
                if (priority.is_defined())
                {
                    warn_printf("%s: warning: ignoring invalid 'priority' clause in 'task' construct\n",
                            directive.get_locus_str().c_str());
                }
                if (emit_omp_report())
                {
                    *_omp_report_file
                        << OpenMP::Report::indent
                        << "No priority was defined for this task\n";
                    ;
                }
            }
        }

        // Attach the implicit flushes at the entry and exit of the task (for analysis purposes)
        execution_environment.append(
                Nodecl::OpenMP::FlushAtEntry::make(
                    directive.get_locus())
                );
        execution_environment.append(
                Nodecl::OpenMP::FlushAtExit::make(
                    directive.get_locus())
                );

        // Label task (this is used only for instrumentation)
        PragmaCustomClause label_clause = pragma_line.get_clause("label");
        {
            TL::ObjectList<std::string> str_list = label_clause.get_tokenized_arguments();
            if (label_clause.is_defined()
                    && str_list.size() == 1)
            {
                if (emit_omp_report())
                {
                    *_omp_report_file
                        << OpenMP::Report::indent
                        << "Label of this task is '" << str_list[0] << "'\n";
                    ;
                }
                execution_environment.append(
                        Nodecl::OpenMP::TaskLabel::make(
                            str_list[0],
                            directive.get_locus()));
            }
            else
            {
                if (label_clause.is_defined())
                {
                    warn_printf("%s: warning: ignoring invalid 'label' clause in 'task' construct\n",
                            directive.get_locus_str().c_str());
                }

                if (emit_omp_report())
                {
                    *_omp_report_file
                        << OpenMP::Report::indent
                        << "This task does not have any label\n";
                    ;
                }
            }
        }

        PragmaCustomClause if_clause = pragma_line.get_clause("if");
        {
            ObjectList<Nodecl::NodeclBase> expr_list = if_clause.get_arguments_as_expressions(directive);
            if (if_clause.is_defined()
                    && expr_list.size() == 1)
            {
                execution_environment.append(Nodecl::OpenMP::If::make(expr_list[0].shallow_copy()));

                if (emit_omp_report())
                {
                    *_omp_report_file
                        << OpenMP::Report::indent
                        << "This task will be deferred only if expression '"
                        << expr_list[0].prettyprint() << "'\n"
                        // << OpenMP::Report::indent
                        // << OpenMP::Report::indent
                        // << "Note that this does not affect dependences: if "
                        // "the task is not deferred the current thread will block until they are satisfied\n"
                        // << OpenMP::Report::indent
                        // << "Note also that there is some unavoidable overhead "
                        // "caused by the required bookkeeping of the task context, even if the task is not deferred\n"
                    ;
                }
            }
            else
            {
                if (if_clause.is_defined())
                {
                    error_printf("%s: error: ignoring invalid 'if' clause\n",
                            directive.get_locus_str().c_str());
                }

                // if (emit_omp_report())
                // {
                //     *_omp_report_file
                //         << OpenMP::Report::indent
                //         "This task may run deferred because it does not have any 'if' clause\n"
                //         // << OpenMP::Report::indent
                //         // << "Note that the runtime may still choose not to run the task deferredly by a number of reasons\n"
                //         ;
                // }
            }
        }

        PragmaCustomClause final_clause = pragma_line.get_clause("final");
        {
            ObjectList<Nodecl::NodeclBase> expr_list = final_clause.get_arguments_as_expressions(directive);
            if (final_clause.is_defined()
                    && expr_list.size() == 1)
            {
                execution_environment.append(Nodecl::OpenMP::Final::make(expr_list[0].shallow_copy()));

                if (emit_omp_report())
                {
                    *_omp_report_file
                        << OpenMP::Report::indent
                        << "When this task is executed it will be final "
                        "if the expression '" << expr_list[0].prettyprint() << "' holds\n"
                        // << OpenMP::Report::indent
                        // << OpenMP::Report::indent
                        // << "A final task does not create any deferred task when it is executed\n"
                        ;
                }
            }
            else
            {
                if (final_clause.is_defined())
                {
                    error_printf("%s: error: ignoring invalid 'final' clause\n",
                            directive.get_locus_str().c_str());
                }
            }
        }

        pragma_line.diagnostic_unused_clauses();

        Nodecl::NodeclBase body_of_task =
            directive.get_statements().shallow_copy();

        Nodecl::NodeclBase async_code =
            Nodecl::OpenMP::Task::make(execution_environment,
                    body_of_task,
                    directive.get_locus());

        directive.replace(async_code);
    }

    void Base::parallel_handler_pre(TL::PragmaCustomStatement)
    {
        if (this->in_ompss_mode())
        {
            return;
        }
    }
    void Base::parallel_handler_post(TL::PragmaCustomStatement directive)
    {
        if (emit_omp_report())
        {
            *_omp_report_file
                << "\n"
                << directive.get_locus_str() << ": " << "PARALLEL construct\n"
                << directive.get_locus_str() << ": " << "------------------\n"
                ;
        }
        if (this->in_ompss_mode())
        {
            warn_printf("%s: warning: explicit parallel regions do not have any effect in OmpSs\n",
                    locus_to_str(directive.get_locus()));
            // Ignore parallel
            directive.replace(directive.get_statements());

            if (emit_omp_report())
            {
                *_omp_report_file
                    << OpenMP::Report::indent
                    << "This construct is ignored in OmpSs mode\n"
                    ;
            }
            return;
        }

        OpenMP::DataSharingEnvironment &ds = _core.get_openmp_info()->get_data_sharing(directive);
        PragmaCustomLine pragma_line = directive.get_pragma_line();

        Nodecl::List execution_environment = this->make_execution_environment(ds, pragma_line, /* ignore_target_info */ false);

        PragmaCustomClause label_clause = pragma_line.get_clause("label");
        {
            TL::ObjectList<std::string> str_list = label_clause.get_tokenized_arguments();
            if (label_clause.is_defined()
                    && str_list.size() == 1)
            {
                execution_environment.append(
                        Nodecl::OpenMP::TaskLabel::make(
                            str_list[0],
                            directive.get_locus()));

                if (emit_omp_report())
                {
                    *_omp_report_file
                        << OpenMP::Report::indent
                        << "Parallel construct labeled '" << str_list[0] << "'\n";
                }
            }
            else
            {
                if (label_clause.is_defined())
                {
                    warn_printf("%s: warning: ignoring invalid 'label' clause in 'parallel' construct\n",
                            directive.get_locus_str().c_str());
                }
                if (emit_omp_report())
                {
                    *_omp_report_file
                        << OpenMP::Report::indent
                        << "This parallel construct does not have any label\n";
                }
            }
        }


        Nodecl::NodeclBase num_threads;
        PragmaCustomClause clause = pragma_line.get_clause("num_threads");
        {
            ObjectList<Nodecl::NodeclBase> args = clause.get_arguments_as_expressions();
            if (clause.is_defined()
                    && args.size() == 1)
            {
                num_threads = args[0];
                if (emit_omp_report())
                {
                    *_omp_report_file
                        << OpenMP::Report::indent
                        << "Number of threads requested '" << num_threads.prettyprint() << "'\n";
                }
            }
            else
            {
                if (clause.is_defined())
                {
                    error_printf("%s: error: ignoring invalid 'num_threads' clause\n",
                            directive.get_locus_str().c_str());
                }
            }
        }

        // Since the parallel construct implies a barrier at its end,
        // there is no need of adding a flush at end, because the barrier implies also a flush
        execution_environment.append(
                Nodecl::OpenMP::FlushAtEntry::make(
                    directive.get_locus())
        );
        execution_environment.append(
                Nodecl::OpenMP::FlushAtExit::make(
                    directive.get_locus())
        );

        // Set implicit barrier at the exit of the combined worksharing
        execution_environment.append(
            Nodecl::OpenMP::BarrierAtEnd::make(
                directive.get_locus()));

        PragmaCustomClause if_clause = pragma_line.get_clause("if");
        {
            ObjectList<Nodecl::NodeclBase> expr_list = if_clause.get_arguments_as_expressions(directive);
            if (if_clause.is_defined()
                    && expr_list.size() == 1)
            {
                execution_environment.append(Nodecl::OpenMP::If::make(expr_list[0].shallow_copy()));

                if (emit_omp_report())
                {
                    *_omp_report_file
                        << OpenMP::Report::indent
                        << "A team of threads will be created only if '"
                        << expr_list[0].prettyprint()
                        << "' holds\n";
                }
            }
            else
            {
                if (if_clause.is_defined())
                {
                    error_printf("%s: error: ignoring invalid 'if' clause\n",
                            directive.get_locus_str().c_str());
                }
                if (emit_omp_report())
                {
                    // *_omp_report_file
                    //     << OpenMP::Report::indent
                    //     << "A team of threads will always be created\n"
                    //     ;
                }
            }
        }

        // if (emit_omp_report())
        // {
        //     *_omp_report_file
        //         << OpenMP::Report::indent
        //         << "Recall that a PARALLEL construct always implies a BARRIER at the end\n"
        //         ;
        // }

        Nodecl::NodeclBase parallel_code = Nodecl::OpenMP::Parallel::make(
                    execution_environment,
                    num_threads,
                    directive.get_statements().shallow_copy(),
                    directive.get_locus());

        pragma_line.diagnostic_unused_clauses();
        directive.replace(parallel_code);
    }

    void Base::single_handler_pre(TL::PragmaCustomStatement) { }
    void Base::single_handler_post(TL::PragmaCustomStatement directive)
    {
        OpenMP::DataSharingEnvironment &ds = _core.get_openmp_info()->get_data_sharing(directive);
        PragmaCustomLine pragma_line = directive.get_pragma_line();

        if (emit_omp_report())
        {
            *_omp_report_file
                << "\n"
                << directive.get_locus_str() << ": " << "SINGLE construct\n"
                << directive.get_locus_str() << ": " << "----------------\n"
                ;
        }

        Nodecl::List execution_environment = this->make_execution_environment(
                ds, pragma_line, /* ignore_target_info */ true);

        if (!pragma_line.get_clause("nowait").is_defined())
        {
            execution_environment.append(
                    Nodecl::OpenMP::FlushAtExit::make(
                        directive.get_locus())
            );

            execution_environment.append(
                    Nodecl::OpenMP::BarrierAtEnd::make(
                        directive.get_locus()));

            if (emit_omp_report())
            {
                *_omp_report_file
                    << OpenMP::Report::indent
                    << "This SINGLE construct does NOT have a BARRIER at the"
                    " end because of the 'nowait' clause\n";
                    ;
            }
        }
        else
        {
            if (emit_omp_report())
            {
                *_omp_report_file
                    << OpenMP::Report::indent
                    << "This SINGLE construct implies a BARRIER at the end\n";
                    ;
            }
        }

        Nodecl::List code;
        code.append(
                Nodecl::OpenMP::Single::make(
                    execution_environment,
                    directive.get_statements().shallow_copy(),
                    directive.get_locus()));

        pragma_line.diagnostic_unused_clauses();
        directive.replace(code);
    }

    void Base::workshare_handler_pre(TL::PragmaCustomStatement) { }
    void Base::workshare_handler_post(TL::PragmaCustomStatement directive)
    {
        OpenMP::DataSharingEnvironment &ds = _core.get_openmp_info()->get_data_sharing(directive);
        PragmaCustomLine pragma_line = directive.get_pragma_line();

        if (emit_omp_report())
        {
            *_omp_report_file
                << "\n"
                << directive.get_locus_str() << ": " << "WORKSHARE construct\n"
                << directive.get_locus_str() << ": " << "-------------------\n"
                ;
        }

        Nodecl::List execution_environment = this->make_execution_environment(
                ds, pragma_line, /* ignore_target_info */ true);

        if (!pragma_line.get_clause("nowait").is_defined())
        {
            execution_environment.append(
                    Nodecl::OpenMP::FlushAtExit::make(
                        directive.get_locus())
            );

            execution_environment.append(
                    Nodecl::OpenMP::BarrierAtEnd::make(
                        directive.get_locus()));

            if (emit_omp_report())
            {
                *_omp_report_file
                    << OpenMP::Report::indent
                    << "This WORKSHARE construct does not have a "
                    "BARRIER at the end due to the 'nowait' clause\n"
                    ;
            }
        }
        else
        {
            if (emit_omp_report())
            {
                *_omp_report_file
                    << OpenMP::Report::indent
                    << "This WORKSHARE construct implies a BARRIER at the end\n"
                    ;
            }
        }

        Nodecl::List code;
        code.append(
                Nodecl::OpenMP::Workshare::make(
                    execution_environment,
                    directive.get_statements().shallow_copy(),
                    directive.get_locus()));

        pragma_line.diagnostic_unused_clauses();
        directive.replace(code);
    }


    void Base::for_handler_pre(TL::PragmaCustomStatement) { }
    void Base::for_handler_post(TL::PragmaCustomStatement directive)
    {
        Nodecl::NodeclBase statement = directive.get_statements();
        ERROR_CONDITION(!statement.is<Nodecl::List>(), "Invalid tree", 0);
        statement = statement.as<Nodecl::List>().front();
        ERROR_CONDITION(!statement.is<Nodecl::Context>(), "Invalid tree", 0);

        PragmaCustomLine pragma_line = directive.get_pragma_line();
        bool barrier_at_end = !pragma_line.get_clause("nowait").is_defined();

        if (emit_omp_report())
        {
            *_omp_report_file
                << "\n"
                << directive.get_locus_str() << ": " << "FOR construct\n"
                << directive.get_locus_str() << ": " << "-------------\n"
            ;
        }

        Nodecl::NodeclBase code = loop_handler_post(directive, statement, barrier_at_end, /* is_combined_worksharing */ false);
        pragma_line.diagnostic_unused_clauses();
        directive.replace(code);
    }

    Nodecl::NodeclBase Base::sections_handler_common(
            TL::PragmaCustomStatement directive,
            Nodecl::NodeclBase statements,
            bool barrier_at_end,
            bool is_combined_worksharing)
    {
        OpenMP::DataSharingEnvironment &ds = _core.get_openmp_info()->get_data_sharing(directive);
        PragmaCustomLine pragma_line = directive.get_pragma_line();
        Nodecl::List execution_environment = this->make_execution_environment(ds, pragma_line, /* ignore_target_info */ false);

        // Set the implicit OpenMP flush / barrier nodes to the environment
        if (barrier_at_end)
        {
            execution_environment.append(
                    Nodecl::OpenMP::FlushAtExit::make(
                        directive.get_locus())
                    );
            execution_environment.append(
                    Nodecl::OpenMP::BarrierAtEnd::make(
                        directive.get_locus()));

            if (emit_omp_report())
            {
                *_omp_report_file
                    << OpenMP::Report::indent
                    << "This SECTIONS construct implies a BARRIER at the end\n"
                    ;
            }
        }
        else
        {
            if (emit_omp_report())
            {
                *_omp_report_file
                    << OpenMP::Report::indent
                    << "This SECTIONS construct does not have a barrier at end\n"
                    ;
            }
        }

        if (is_combined_worksharing)
        {
            execution_environment.append(
                    Nodecl::OpenMP::CombinedWorksharing::make(
                        directive.get_locus()));
        }

        ERROR_CONDITION(!statements.is<Nodecl::List>(), "This is not a list!", 0);
        Nodecl::List tasks = statements.as<Nodecl::List>();

        // There is an extra compound statement right after #pragma omp sections
        ERROR_CONDITION(!tasks[0].is<Nodecl::CompoundStatement>(), "Expecting a compound statement here", 0);
        tasks = tasks[0].as<Nodecl::CompoundStatement>().get_statements().as<Nodecl::List>();

        Nodecl::List section_list;

        for (Nodecl::List::iterator it = tasks.begin(); it != tasks.end(); it++)
        {
            ERROR_CONDITION(!it->is<Nodecl::PragmaCustomStatement>(), "Unexpected node '%s'\n",
                    ast_print_node_type(it->get_kind()));

            Nodecl::PragmaCustomStatement p = it->as<Nodecl::PragmaCustomStatement>();

            section_list.append(
                    Nodecl::OpenMP::Section::make(
                        p.get_statements().shallow_copy(),
                        p.get_locus()));
        }

        Nodecl::OpenMP::Sections sections =
            Nodecl::OpenMP::Sections::make(
                    execution_environment,
                    section_list,
                    directive.get_locus());

        Nodecl::NodeclBase code = Nodecl::List::make(sections);

        return code;
    }

    Nodecl::NodeclBase Base::loop_handler_post(
            TL::PragmaCustomStatement directive,
            Nodecl::NodeclBase context,
            bool barrier_at_end,
            bool is_combined_worksharing)
    {
        OpenMP::DataSharingEnvironment &ds = _core.get_openmp_info()->get_data_sharing(directive);
        PragmaCustomLine pragma_line = directive.get_pragma_line();
        Nodecl::List execution_environment = this->make_execution_environment(
                ds, pragma_line, /* ignore_target_info */ false);

        PragmaCustomClause label_clause = pragma_line.get_clause("label");
        {
            TL::ObjectList<std::string> str_list = label_clause.get_tokenized_arguments();
            if (label_clause.is_defined()
                    && str_list.size() == 1)
            {
                execution_environment.append(
                        Nodecl::OpenMP::TaskLabel::make(
                            str_list[0],
                            directive.get_locus()));

                if (emit_omp_report())
                {
                    *_omp_report_file
                        << OpenMP::Report::indent
                        << "Label of this loop '" << str_list[0] << "'\n";
                        ;
                }
            }
            else
            {
                if (label_clause.is_defined())
                {
                    warn_printf("%s: warning: ignoring invalid 'label' clause in loop construct\n",
                            directive.get_locus_str().c_str());
                }
                if (emit_omp_report())
                {
                    *_omp_report_file
                        << OpenMP::Report::indent
                        << "This loop does not have any label\n";
                        ;
                }
            }
        }

        if (pragma_line.get_clause("schedule").is_defined())
        {
            PragmaCustomClause clause = pragma_line.get_clause("schedule");

            ObjectList<std::string> arguments = clause.get_tokenized_arguments();

            Nodecl::NodeclBase chunk;

            std::string schedule = arguments[0];
            schedule = strtolower(schedule.c_str());

            std::string checked_schedule_name = schedule;

            // Allow OpenMP schedules be prefixed with 'ompss_', 'omp_' and 'openmp_'

            std::string valid_prefixes[] = { "ompss_", "omp_", "openmp_", ""};
            int i = 0;
            bool found = false;
            while (valid_prefixes[i] != "" && !found)
            {
                found = checked_schedule_name.substr(0,valid_prefixes[i].size()) == valid_prefixes[i];
                if (found)
                    checked_schedule_name = checked_schedule_name.substr(valid_prefixes[i].size());

                ++i;
            }

            bool default_chunk = false;
            if (arguments.size() == 1)
            {
                if (checked_schedule_name == "static")
                {
                    default_chunk = true;
                    chunk = const_value_to_nodecl(const_value_get_signed_int(0));
                }
                else
                {
                    chunk = const_value_to_nodecl(const_value_get_signed_int(1));
                }
            }
            else if (arguments.size() == 2)
            {
                chunk = Source(arguments[1]).parse_expression(directive);
            }
            else
            {
                // Core should have checked this
                internal_error("Invalid values in schedule clause", 0);
            }


            if (checked_schedule_name == "static"
                    || checked_schedule_name == "dynamic"
                    || checked_schedule_name == "guided"
                    || checked_schedule_name == "runtime"
                    || checked_schedule_name == "auto")
            {
                execution_environment.append(
                        Nodecl::OpenMP::Schedule::make(
                            chunk,
                            schedule,
                            directive.get_locus()));
            }
            else
            {
                internal_error("Invalid schedule '%s' for schedule clause\n",
                        schedule.c_str());
            }

            if (emit_omp_report())
            {
                *_omp_report_file
                    << OpenMP::Report::indent
                    << "Loop has been explictly scheduled as '"
                    << schedule << "'";

               if (!default_chunk)
               {
                   *_omp_report_file << " with a chunk of '" << chunk.prettyprint() << "'"
                       ;
               }

               *_omp_report_file << "\n";
            }
        }
        else
        {
            // def-sched-var is STATIC in our implementation
            execution_environment.append(
                    Nodecl::OpenMP::Schedule::make(
                        ::const_value_to_nodecl(const_value_get_signed_int(0)),
                        "static",
                        directive.get_locus()));

            if (emit_omp_report())
            {
                *_omp_report_file
                    << OpenMP::Report::indent
                    << "Loop has been implicitly scheduled as 'STATIC'\n"
                    ;
            }
        }

        if (barrier_at_end)
        {
            execution_environment.append(
                    Nodecl::OpenMP::FlushAtExit::make(
                        directive.get_locus())
            );

            execution_environment.append(
                    Nodecl::OpenMP::BarrierAtEnd::make(
                        directive.get_locus()));

            if (emit_omp_report())
            {
                *_omp_report_file
                    << OpenMP::Report::indent
                    << "This loop implies a BARRIER at end\n"
                    ;
            }
        }
        else
        {
            if (emit_omp_report())
            {
                if (!is_combined_worksharing)
                {
                    *_omp_report_file
                        << OpenMP::Report::indent
                        << "This loop does not have any BARRIER at end\n"
                        ;
                }
                else
                {
                    *_omp_report_file
                        << OpenMP::Report::indent
                        << "This loop implies a BARRIER at end of the enclosing PARALLEL\n"
                        ;
                }
            }
        }

        if (is_combined_worksharing)
        {
            execution_environment.append(
                    Nodecl::OpenMP::CombinedWorksharing::make(
                        directive.get_locus()));
        }

        // ERROR_CONDITION (!context.is<Nodecl::ForStatement>(), "Invalid tree of kind '%s'", ast_print_node_type(statement.get_kind()));
        // TL::ForStatement for_statement(context.as<Nodecl::ForStatement>());

        Nodecl::OpenMP::For distribute =
            Nodecl::OpenMP::For::make(
                    execution_environment,
                    context,
                    directive.get_locus());

        Nodecl::NodeclBase code = Nodecl::List::make(distribute);

        return code;
    }

    void Base::do_handler_pre(TL::PragmaCustomStatement directive) { }
    void Base::do_handler_post(TL::PragmaCustomStatement directive)
    {
        Nodecl::NodeclBase statement = directive.get_statements();
        ERROR_CONDITION(!statement.is<Nodecl::List>(), "Invalid tree", 0);
        statement = statement.as<Nodecl::List>().front();
        ERROR_CONDITION(!statement.is<Nodecl::Context>(), "Invalid tree", 0);

        PragmaCustomLine pragma_line = directive.get_pragma_line();
        bool barrier_at_end = !pragma_line.get_clause("nowait").is_defined();

        if (emit_omp_report())
        {
            *_omp_report_file
                << "\n"
                << directive.get_locus_str() << ": " << "DO construct\n"
                << directive.get_locus_str() << ": " << "------------\n"
            ;
        }
        Nodecl::NodeclBase code = loop_handler_post(directive, statement, barrier_at_end, /* is_combined_worksharing */ false);
        pragma_line.diagnostic_unused_clauses();
        directive.replace(code);
    }

    // Since parallel {for,do,sections} are split into two nodes: parallel and
    // then {for,do,section}, we need to make sure the children of the new
    // parallel contains a proper context as its child
    void Base::nest_context_in_pragma(TL::PragmaCustomStatement directive)
    {
        Nodecl::NodeclBase stms = directive.get_statements();

        decl_context_t new_context =
            new_block_context(directive.retrieve_context().get_decl_context());
        Nodecl::NodeclBase ctx = Nodecl::List::make(
                Nodecl::Context::make(
                    stms,
                    new_context,
                    stms.get_locus()));

        directive.set_statements(ctx);
    }

    void Base::parallel_do_handler_pre(TL::PragmaCustomStatement directive)
    {
        if (this->in_ompss_mode())
        {
            do_handler_pre(directive);
            return;
        }

        nest_context_in_pragma(directive);
    }

    void Base::parallel_do_handler_post(TL::PragmaCustomStatement directive)
    {
        if (emit_omp_report())
        {
            *_omp_report_file
                << "\n"
                << directive.get_locus_str() << ": " << "PARALLEL DO construct\n"
                << directive.get_locus_str() << ": " << "---------------------\n"
            ;
        }

        if (this->in_ompss_mode())
        {
            // In OmpSs this is like a simple DO
            warn_printf("%s: warning: explicit parallel regions do not have any effect in OmpSs\n",
                    locus_to_str(directive.get_locus()));
            if (emit_omp_report())
            {
                *_omp_report_file
                    << OpenMP::Report::indent
                    << "Note that in OmpSs the PARALLEL part of a PARALLEL DO is ignored\n"
                    ;
            }
            do_handler_post(directive);
            return;
        }

        OpenMP::DataSharingEnvironment &ds = _core.get_openmp_info()->get_data_sharing(directive);
        PragmaCustomLine pragma_line = directive.get_pragma_line();
        Nodecl::List execution_environment = this->make_execution_environment_for_combined_worksharings(ds, pragma_line);

        Nodecl::NodeclBase statement = directive.get_statements();
        // This first context was added by nest_context_in_pragma
        ERROR_CONDITION(!statement.is<Nodecl::List>(), "Invalid tree", 0);
        statement = statement.as<Nodecl::List>().front();
        ERROR_CONDITION(!statement.is<Nodecl::Context>(), "Invalid tree", 0);
        // This is the usual context of the statements of a pragma
        statement = statement.as<Nodecl::Context>().get_in_context();
        ERROR_CONDITION(!statement.is<Nodecl::List>(), "Invalid tree", 0);
        statement = statement.as<Nodecl::List>().front();
        ERROR_CONDITION(!statement.is<Nodecl::Context>(), "Invalid tree", 0);

        Nodecl::NodeclBase num_threads;
        PragmaCustomClause clause = pragma_line.get_clause("num_threads");
        {
            ObjectList<Nodecl::NodeclBase> args = clause.get_arguments_as_expressions();
            if (clause.is_defined()
                    && args.size() == 1)
            {
                num_threads = args[0];
            }
            else if (clause.is_defined())
            {
                error_printf("%s: error: ignoring invalid 'num_threads' wrong clause\n",
                        directive.get_locus_str().c_str());
            }
        }

        if (!num_threads.is_null())
        {
            if (emit_omp_report())
            {
                *_omp_report_file
                    << OpenMP::Report::indent
                    << "Number of threads requested '" << num_threads.prettyprint() << "'\n";
            }
        }

        // Set implicit flushes at the entry and exit of the combined worksharing
        execution_environment.append(
                Nodecl::OpenMP::FlushAtEntry::make(
                    directive.get_locus())
        );
        execution_environment.append(
                Nodecl::OpenMP::FlushAtExit::make(
                    directive.get_locus())
        );

        // Set implicit barrier at the exit of the combined worksharing
        execution_environment.append(
                Nodecl::OpenMP::BarrierAtEnd::make(
                    directive.get_locus()));

        PragmaCustomClause if_clause = pragma_line.get_clause("if");
        if (if_clause.is_defined())
        {
            ObjectList<Nodecl::NodeclBase> expr_list = if_clause.get_arguments_as_expressions(directive);
            if (expr_list.size() != 1)
            {
                running_error("%s: error: clause 'if' requires just one argument\n",
                        directive.get_locus_str().c_str());
            }
            execution_environment.append(Nodecl::OpenMP::If::make(expr_list[0].shallow_copy()));
        }

        // for-statement
        Nodecl::NodeclBase for_statement_code = loop_handler_post(directive,
                statement,
                /* barrier_at_end */ false,
                /* is_combined_worksharing */ true);

        statement = directive.get_statements();
        // This first context was added by nest_context_in_pragma
        ERROR_CONDITION(!statement.is<Nodecl::List>(), "Invalid tree", 0);
        statement = statement.as<Nodecl::List>().front();
        ERROR_CONDITION(!statement.is<Nodecl::Context>(), "Invalid tree", 0);
        // Nest the for in the place where we expect the for-statement code
        statement.as<Nodecl::Context>().set_in_context(for_statement_code);

        Nodecl::NodeclBase parallel_code
            = Nodecl::OpenMP::Parallel::make(
                execution_environment,
                num_threads,
                directive.get_statements().shallow_copy(),
                directive.get_locus());

        pragma_line.diagnostic_unused_clauses();
        directive.replace(parallel_code);
    }

    // Function tasks
    void Base::task_handler_pre(TL::PragmaCustomDeclaration declaration) { }
    void Base::task_handler_post(TL::PragmaCustomDeclaration decl)
    {
        TL::PragmaCustomLine pragma_line = decl.get_pragma_line();
        pragma_line.diagnostic_unused_clauses();
        Nodecl::Utils::remove_from_enclosing_list(decl);
    }

    void Base::target_handler_pre(TL::PragmaCustomStatement stmt)   { }
    void Base::target_handler_pre(TL::PragmaCustomDeclaration decl) { }

    void Base::target_handler_post(TL::PragmaCustomStatement stmt)
    {
        TL::PragmaCustomLine pragma_line = stmt.get_pragma_line();
        pragma_line.diagnostic_unused_clauses();
        stmt.replace(stmt.get_statements());
    }

    void Base::target_handler_post(TL::PragmaCustomDeclaration decl)
    {
        TL::PragmaCustomLine pragma_line = decl.get_pragma_line();
        if (decl.get_nested_pragma().is_null())
        {
            Nodecl::NodeclBase result;
            ObjectList<Nodecl::NodeclBase> devices;
            ObjectList<Nodecl::NodeclBase> symbols;

            const locus_t* locus = decl.get_locus();

            PragmaCustomClause device_clause = pragma_line.get_clause("device");
            if (device_clause.is_defined())
            {
                ObjectList<std::string> device_names = device_clause.get_tokenized_arguments();
                for (ObjectList<std::string>::iterator it = device_names.begin();
                        it != device_names.end();
                        ++it)
                {
                    devices.append(Nodecl::Text::make(*it, locus));
                }
            }

            ERROR_CONDITION(!decl.has_symbol(),
                    "%s: expecting a function declaration or definition", decl.get_locus_str().c_str());

            Symbol sym = decl.get_symbol();
            symbols.append(Nodecl::Symbol::make(sym, locus));

            result = Nodecl::OpenMP::TargetDeclaration::make(
                    Nodecl::List::make(devices),
                    Nodecl::List::make(symbols),
                    locus);

            pragma_line.diagnostic_unused_clauses();
            decl.replace(result);
        }
        else
        {
            pragma_line.diagnostic_unused_clauses();
            Nodecl::Utils::remove_from_enclosing_list(decl);
        }
    }

    // clause(list[:int])
    template <typename openmp_node>
    void Base::process_symbol_list_colon_int_clause(
            const TL::PragmaCustomLine& pragma_line,
            const std::string& pragma_name,
            const Nodecl::NodeclBase& ref_scope,
            Nodecl::List& environment,
            const int default_int)
    {
        PragmaCustomClause clause_clause = pragma_line.get_clause(pragma_name);

        if (clause_clause.is_defined())
        {
            TL::ObjectList<std::string> arg_clauses_list = clause_clause.get_raw_arguments();

            TL::ExpressionTokenizerTrim colon_tokenizer(':');
            TL::ExpressionTokenizerTrim comma_tokenizer(',');

            for(TL::ObjectList<std::string>::iterator it = arg_clauses_list.begin();
                    it != arg_clauses_list.end();
                    it++)
            {
                TL::ObjectList<std::string> colon_splited_list = colon_tokenizer.tokenize(*it);

                int colon_splited_list_size = colon_splited_list.size();

                ERROR_CONDITION((colon_splited_list_size <= 0) ||
                        (colon_splited_list_size > 2),
                        "'%s' clause has a wrong format", 
                        pragma_name.c_str());

                // Int value will be default_int
                Nodecl::IntegerLiteral int_value = 
                    const_value_to_nodecl(const_value_get_signed_int(default_int));

                if (colon_splited_list_size == 2)
                {
                    TL::Source colon_src;
                    colon_src << colon_splited_list.back();

                    Nodecl::NodeclBase nodecl_int_value = colon_src.parse_expression(
                            ref_scope.retrieve_context());

                    ERROR_CONDITION(!nodecl_int_value.is<Nodecl::IntegerLiteral>(),
                            "wrong int_value in '%s' clause", pragma_name.c_str());

                    int_value = nodecl_int_value.as<Nodecl::IntegerLiteral>();
                }

                TL::ObjectList<std::string> comma_splited_list = comma_tokenizer.tokenize(
                        colon_splited_list.front());

                Nodecl::List clause_variables = 
                    Nodecl::List::make(Nodecl::Utils::get_strings_as_expressions(
                                comma_splited_list, ref_scope));

                environment.append(openmp_node::make(
                            clause_variables, int_value,
                            pragma_line.get_locus()));
            }
        }
    }

    // clause(list)
    template <typename openmp_node>
    void Base::process_symbol_list_clause(
            const TL::PragmaCustomLine& pragma_line,
            const std::string& pragma_name,
            const Nodecl::NodeclBase& ref_scope,
            Nodecl::List& environment)
    {
        PragmaCustomClause clause = pragma_line.get_clause(pragma_name);

        if (clause.is_defined())
        {
            environment.append(openmp_node::make(
                        Nodecl::List::make(
                            clause.get_arguments_as_expressions(ref_scope)),
                        pragma_line.get_locus()));
        }
    }

    void Base::process_common_simd_clauses(
            const TL::PragmaCustomLine& pragma_line,
            const Nodecl::NodeclBase& ref_scope,
            Nodecl::List& environment)
    {
        // Aligned
        process_symbol_list_colon_int_clause<Nodecl::OpenMP::Aligned>
            (pragma_line, "aligned", ref_scope, environment, 0);

        // Linear
        process_symbol_list_colon_int_clause<Nodecl::OpenMP::Linear>
            (pragma_line, "linear", ref_scope, environment, 1);

        // Uniform
        process_symbol_list_clause<Nodecl::OpenMP::Uniform>
            (pragma_line, "uniform", ref_scope, environment);

        // Suitable
        process_symbol_list_clause<Nodecl::OpenMP::Suitable>
            (pragma_line, "suitable", ref_scope, environment);

        // Unroll
        PragmaCustomClause unroll_clause = pragma_line.get_clause("unroll");

        if (unroll_clause.is_defined())
        {
            environment.append(
                    Nodecl::OpenMP::Unroll::make(
                        Nodecl::IntegerLiteral::make(TL::Type::get_int_type(),
                            unroll_clause.get_arguments_as_expressions().front().get_constant()),
                        pragma_line.get_locus()));
        }

        // Unroll and Jam
        PragmaCustomClause unroll_and_jam_clause = pragma_line.get_clause("unroll_and_jam");

        if (unroll_and_jam_clause.is_defined())
        {
            environment.append(
                    Nodecl::OpenMP::UnrollAndJam::make(
                        Nodecl::IntegerLiteral::make(TL::Type::get_int_type(),
                            unroll_and_jam_clause.get_arguments_as_expressions().front().get_constant()),
                        pragma_line.get_locus()));
        }

        // VectorLengthFor
        PragmaCustomClause vectorlengthfor_clause = pragma_line.get_clause("vectorlengthfor");

        if (vectorlengthfor_clause.is_defined())
        {
            TL::Source target_type_src;

            target_type_src << vectorlengthfor_clause.get_raw_arguments().front();

            TL::Type target_type = target_type_src.parse_c_type_id(ref_scope.retrieve_context());

            environment.append(
                    Nodecl::OpenMP::VectorLengthFor::make(
                        target_type,
                        pragma_line.get_locus()));
        }

        // Non-temporal (Stream stores)
        PragmaCustomClause nontemporal_clause = pragma_line.get_clause("nontemporal");

        if (nontemporal_clause.is_defined())
        {
            TL::ObjectList<std::string> arg_clauses_list = nontemporal_clause.get_raw_arguments();

            TL::ExpressionTokenizerTrim colon_tokenizer(':');
            TL::ExpressionTokenizerTrim comma_tokenizer(',');

            for(TL::ObjectList<std::string>::iterator it = arg_clauses_list.begin();
                    it != arg_clauses_list.end();
                    it++)
            {
                TL::ObjectList<std::string> colon_splited_list = colon_tokenizer.tokenize(*it);

                int colon_splited_list_size = colon_splited_list.size();

                ERROR_CONDITION((colon_splited_list_size <= 0) ||
                        (colon_splited_list_size > 2),
                        "'nontemporal' clause has a wrong format", 0);

                //Nodecl::IntegerLiteral alignment = const_value_to_nodecl(const_value_get_zero(4, 1));

                TL::ObjectList<std::string> comma_splited_list;
                TL::ObjectList<Nodecl::NodeclBase> nontemporal_flags_obj_list;

                if (colon_splited_list_size == 2)
                {
                    comma_splited_list = comma_tokenizer.tokenize(colon_splited_list.back());

                    for(ObjectList<std::string>::iterator comma_it = comma_splited_list.begin();
                            comma_it != comma_splited_list.end();
                            comma_it++)
                    {
                        if ((*comma_it) == "relaxed")
                        {
                            nontemporal_flags_obj_list.insert(Nodecl::RelaxedFlag::make());
                            printf("Relaxed!\n");
                        }
                        else if((*comma_it) == "evict")
                        {
                            nontemporal_flags_obj_list.insert(Nodecl::EvictFlag::make());
                            printf("Evict!\n");
                        }
                        else
                        {
                            printf("%s\n", comma_it->c_str());
                            running_error("Neither 'relaxed' nor 'evict'");
                        }
                    }
                }

                comma_splited_list = comma_tokenizer.tokenize(
                        colon_splited_list.front());

                Nodecl::List nontemporal_variables =
                    Nodecl::List::make(Nodecl::Utils::get_strings_as_expressions(
                                comma_splited_list, pragma_line));

                Nodecl::List nontemporal_flags =
                    Nodecl::List::make(nontemporal_flags_obj_list);

                environment.append(
                        Nodecl::OpenMP::Nontemporal::make(
                            nontemporal_variables,
                            nontemporal_flags,
                            pragma_line.get_locus()));
            }
        }

        // Overlap
        PragmaCustomClause overlap_clause = pragma_line.get_clause("overlap");

        if (overlap_clause.is_defined())
        {
            TL::ObjectList<std::string> arg_clauses_list = overlap_clause.get_raw_arguments();

            TL::ExpressionTokenizerTrim colon_tokenizer(':');
            TL::ExpressionTokenizerTrim comma_tokenizer(',');

            for(TL::ObjectList<std::string>::iterator it = arg_clauses_list.begin();
                    it != arg_clauses_list.end();
                    it++)
            {
                TL::ObjectList<std::string> colon_splited_list = colon_tokenizer.tokenize(*it);

                int colon_splited_list_size = colon_splited_list.size();

                ERROR_CONDITION((colon_splited_list_size <= 0) ||
                        (colon_splited_list_size > 2),
                        "'overlap' clause has a wrong format", 0);

                //Nodecl::IntegerLiteral alignment = const_value_to_nodecl(const_value_get_zero(4, 1));

                TL::ObjectList<std::string> comma_splited_list;
                TL::ObjectList<Nodecl::NodeclBase> overlap_flags_obj_list;

                Nodecl::NodeclBase min_group_loads;
                Nodecl::NodeclBase max_group_registers;
                Nodecl::NodeclBase max_groups;

                if (colon_splited_list_size == 2)
                {
                    comma_splited_list = comma_tokenizer.tokenize(colon_splited_list.back());

                    ERROR_CONDITION(comma_splited_list.size() > 3,
                        "'overlap' clause has a wrong format", 0);

                    TL::ObjectList<std::string>::iterator comma_splited_it =
                       comma_splited_list.begin();

                    // Min group loads
                    if (comma_splited_it != comma_splited_list.end())
                    {
                        TL::Source it_src;
                        it_src << *comma_splited_it;

                        min_group_loads = it_src.parse_expression(
                                ref_scope.retrieve_context());

                        ERROR_CONDITION(!min_group_loads.is<Nodecl::IntegerLiteral>(),
                                "'min_group_loads' in 'overlap' clause has a wrong type", 0);

                        comma_splited_it++;
                    }
                    else
                    {
                        running_error("Missing 'min_group_loads' parameter in 'overlap' clause");
                    } 

                    // Max group registers
                    if (comma_splited_it != comma_splited_list.end())
                    {
                        TL::Source it_src;
                        it_src << *comma_splited_it;

                        max_group_registers = it_src.parse_expression(
                                ref_scope.retrieve_context());

                        ERROR_CONDITION(!min_group_loads.is<Nodecl::IntegerLiteral>(),
                                "'max_group_registers' in 'overlap' clause has a wrong type", 0);

                        comma_splited_it++;
                    }
                    else
                    {
                        running_error("Missing 'max_group_registers' parameter in 'overlap' clause");
                    } 

                    // Max groups
                    if (comma_splited_it != comma_splited_list.end())
                    {
                        TL::Source it_src;
                        it_src << *comma_splited_it;

                        max_groups = it_src.parse_expression(
                                ref_scope.retrieve_context());

                        ERROR_CONDITION(!min_group_loads.is<Nodecl::IntegerLiteral>(),
                                "'max_groups' in 'overlap' clause has a wrong type", 0);

                        comma_splited_it++;
                    }
                    else
                    {
                        running_error("Missing 'max_groups' parameter in 'overlap' clause");
                    } 
                }

                comma_splited_list = comma_tokenizer.tokenize(
                        colon_splited_list.front());

                Nodecl::List overlap_variables =
                    Nodecl::List::make(Nodecl::Utils::get_strings_as_expressions(
                                comma_splited_list, pragma_line));

                environment.append(
                        Nodecl::OpenMP::Overlap::make(
                            overlap_variables,
                            min_group_loads,
                            max_group_registers,
                            max_groups,
                            pragma_line.get_locus()));
            }
        }
    }

    // SIMD Statement
    void Base::simd_handler_pre(TL::PragmaCustomStatement) { }
    void Base::simd_handler_post(TL::PragmaCustomStatement stmt)
    {
#ifndef VECTORIZATION_DISABLED
        if (_simd_enabled)
        {
            // SIMD Clauses
            PragmaCustomLine pragma_line = stmt.get_pragma_line();
            OpenMP::DataSharingEnvironment &ds = _core.get_openmp_info()->get_data_sharing(stmt);
            Nodecl::List environment = this->make_execution_environment(ds, pragma_line, /* ignore_target_info */ false);

            process_common_simd_clauses(pragma_line, stmt, environment);

            Nodecl::NodeclBase loop_statement = get_statement_from_pragma(stmt);

            ERROR_CONDITION(!(loop_statement.is<Nodecl::ForStatement>() ||
                    loop_statement.is<Nodecl::WhileStatement>()),
                    "Unexpected node %s. Expecting a for-statement or while-statement"\
                    " after '#pragma omp simd'", 
                    ast_print_node_type(loop_statement.get_kind()));

            Nodecl::OpenMP::Simd omp_simd_node =
               Nodecl::OpenMP::Simd::make(
                       loop_statement.shallow_copy(),
                       environment,
                       loop_statement.get_locus());

            pragma_line.diagnostic_unused_clauses();
            stmt.replace(Nodecl::List::make(omp_simd_node));
        }
#else
    warn_printf("%s: warning: ignoring #pragma omp simd\n", stmt.get_locus_str().c_str());
#endif
    }

    // SIMD Functions
    void Base::simd_handler_pre(TL::PragmaCustomDeclaration decl) { }
    void Base::simd_handler_post(TL::PragmaCustomDeclaration decl)
    {
#ifndef VECTORIZATION_DISABLED
        if (_simd_enabled)
        {
            // SIMD Clauses
            TL::PragmaCustomLine pragma_line = decl.get_pragma_line();
            OpenMP::DataSharingEnvironment &ds = _core.get_openmp_info()->get_data_sharing(decl);
            Nodecl::List environment = this->make_execution_environment(ds, pragma_line, /* ignore_target_info */ false);

            process_common_simd_clauses(pragma_line, 
                    decl.get_context_of_parameters(), environment);

            // Mask
            PragmaCustomClause mask_clause = pragma_line.get_clause("mask");
            
            if (mask_clause.is_defined())
            {
                environment.append(
                        Nodecl::OpenMP::Mask::make(decl.get_locus()));
            }

            // No Mask
            PragmaCustomClause no_mask_clause = pragma_line.get_clause("nomask");
            
            if (no_mask_clause.is_defined())
            {
                environment.append(
                        Nodecl::OpenMP::NoMask::make(decl.get_locus()));
            }

            ERROR_CONDITION(!decl.has_symbol(), "Expecting a function definition here (1)", 0);

            TL::Symbol sym = decl.get_symbol();
            ERROR_CONDITION(!sym.is_function(), "Expecting a function definition here (2)", 0);

            Nodecl::NodeclBase node = sym.get_function_code();
            ERROR_CONDITION(!node.is<Nodecl::FunctionCode>(), "Expecting a function definition here (3)", 0);

            Nodecl::OpenMP::SimdFunction simd_func =
                Nodecl::OpenMP::SimdFunction::make(
                        node.shallow_copy(),
                        environment,
                        node.get_locus());

            node.replace(simd_func);

            pragma_line.diagnostic_unused_clauses();
            // Remove #pragma
            Nodecl::Utils::remove_from_enclosing_list(decl);
        }
#else
    warn_printf("%s: warning: ignoring #pragma omp simd\n", decl.get_locus_str().c_str());
#endif
    }

    // SIMD For Statement
    void Base::simd_for_handler_pre(TL::PragmaCustomStatement) { }
    void Base::simd_for_handler_post(TL::PragmaCustomStatement stmt)
    {
#ifndef VECTORIZATION_DISABLED
        if (_simd_enabled)
        {
            // SIMD Clauses
            PragmaCustomLine pragma_line = stmt.get_pragma_line();
            Nodecl::List environment;

            process_common_simd_clauses(pragma_line, stmt, environment);

            // Skipping AST_LIST_NODE
            Nodecl::NodeclBase statements = stmt.get_statements();
            ERROR_CONDITION(!statements.is<Nodecl::List>(),
                    "'pragma omp simd' Expecting a AST_LIST_NODE (1)", 0);
            Nodecl::List ast_list_node = statements.as<Nodecl::List>();
            ERROR_CONDITION(ast_list_node.size() != 1,
                    "AST_LIST_NODE after '#pragma omp simd' must be equal to 1 (1)", 0);

            // Skipping NODECL_CONTEXT
            Nodecl::NodeclBase context = ast_list_node.front();
            //ERROR_CONDITION(!context.is<Nodecl::Context>(),
            //        "'pragma omp simd' Expecting a NODECL_CONTEXT", 0);

            // Skipping AST_LIST_NODE
            //Nodecl::NodeclBase in_context = context.as<Nodecl::Context>().get_in_context();
            // ERROR_CONDITION(!in_context.is<Nodecl::List>(),
            //         "'pragma omp simd' Expecting a AST_LIST_NODE (2)", 0);
            // Nodecl::List ast_list_node2 = in_context.as<Nodecl::List>();
            // ERROR_CONDITION(ast_list_node2.size() != 1,
            //         "AST_LIST_NODE after '#pragma omp simd' must be equal to 1 (2)", 0);

            // Nodecl::NodeclBase for_statement = ast_list_node2.front();
            // ERROR_CONDITION(!for_statement.is<Nodecl::ForStatement>(),
            //         "Unexpected node %s. Expecting a ForStatement after '#pragma omp simd'",
            //         ast_print_node_type(for_statement.get_kind()));

            // for_handler_post
            bool barrier_at_end = !pragma_line.get_clause("nowait").is_defined();

            Nodecl::OpenMP::For omp_for = loop_handler_post(
                    stmt, context, barrier_at_end, false).as<Nodecl::List>().front()
                .as<Nodecl::OpenMP::For>();

            Nodecl::OpenMP::SimdFor omp_simd_for_node =
               Nodecl::OpenMP::SimdFor::make(
                       omp_for,
                       environment,
                       context.get_locus());

            // Removing #pragma
            pragma_line.diagnostic_unused_clauses();
            //stmt.replace(code);
            stmt.replace(omp_simd_for_node);
        }
#else
    warn_printf("%s: warning: ignoring #pragma omp simd for\n", stmt.get_locus_str().c_str());
#endif
    }

    void Base::parallel_simd_for_handler_pre(TL::PragmaCustomStatement) { }
    void Base::parallel_simd_for_handler_post(TL::PragmaCustomStatement stmt)
    {
#ifndef VECTORIZATION_DISABLED
        TL::PragmaCustomLine pragma_line = stmt.get_pragma_line();
        pragma_line.diagnostic_unused_clauses();
        // FIXME - What is supposed to happen here?
        // It is still not supported
#else
    warn_printf("%s: warning: ignoring #pragma omp parallel simd for\n", stmt.get_locus_str().c_str());
#endif
    }

    // SIMD Parallel
    void Base::simd_parallel_handler_pre(TL::PragmaCustomStatement stmt) 
    {
        parallel_handler_pre(stmt);
    }

    void Base::simd_parallel_handler_post(TL::PragmaCustomStatement stmt)
    {
#ifndef VECTORIZATION_DISABLED
        if (_simd_enabled)
        {
            // SIMD Clauses
            PragmaCustomLine pragma_line = stmt.get_pragma_line();
            Nodecl::List environment;

            process_common_simd_clauses(pragma_line, stmt, environment);

            parallel_handler_post(stmt);

            Nodecl::OpenMP::SimdParallel omp_simd_parallel_node =
               Nodecl::OpenMP::SimdParallel::make(
                       stmt.shallow_copy(), // it's been replaced by an                        
                       environment,         // OpenMP::Parallel in parallel_handler_post
                       stmt.get_locus());

            // Removing #pragma
            pragma_line.diagnostic_unused_clauses();
            stmt.replace(omp_simd_parallel_node);
        }
#else
    warn_printf("%s: warning: ignoring #pragma omp simd parallel\n", stmt.get_locus_str().c_str());
#endif
    }


    void Base::sections_handler_pre(TL::PragmaCustomStatement) { }
    void Base::sections_handler_post(TL::PragmaCustomStatement directive)
    {
        PragmaCustomLine pragma_line = directive.get_pragma_line();

        if (emit_omp_report())
        {
            *_omp_report_file
                << "\n"
                << directive.get_locus_str() << ": " << "SECTIONS construct\n"
                << directive.get_locus_str() << ": " << "------------------\n"
                << directive.get_locus_str() << ": " << directive.get_statements().prettyprint() << "\n"
                ;
        }

        bool barrier_at_end = !pragma_line.get_clause("nowait").is_defined();

        Nodecl::NodeclBase sections_code = sections_handler_common(directive,
                directive.get_statements(),
                barrier_at_end,
                /* is_combined_worksharing */ false);
        pragma_line.diagnostic_unused_clauses();
        directive.replace(sections_code);
    }

    void Base::parallel_sections_handler_pre(TL::PragmaCustomStatement directive)
    {
        if (this->in_ompss_mode())
        {
            sections_handler_pre(directive);
            return;
        }

        nest_context_in_pragma(directive);
    }

    void Base::parallel_sections_handler_post(TL::PragmaCustomStatement directive)
    {
        if (emit_omp_report())
        {
            *_omp_report_file
                << "\n"
                << directive.get_locus_str() << ": " << "PARALLEL SECTIONS construct\n"
                << directive.get_locus_str() << ": " << "---------------------------\n"
                << directive.get_locus_str() << ": " << directive.get_statements().prettyprint() << "\n"
                ;
        }
        if (this->in_ompss_mode())
        {
            warn_printf("%s: warning: explicit parallel regions do not have any effect in OmpSs\n",
                    locus_to_str(directive.get_locus()));
            if (emit_omp_report())
            {
                *_omp_report_file
                    << OpenMP::Report::indent
                    << "Note that the PARALLEL part of PARALLEL SECTIONS is ignored in OmpSs\n"
                    ;
            }
            sections_handler_post(directive);
            return;
        }

        OpenMP::DataSharingEnvironment &ds = _core.get_openmp_info()->get_data_sharing(directive);
        PragmaCustomLine pragma_line = directive.get_pragma_line();

        Nodecl::List execution_environment = this->make_execution_environment_for_combined_worksharings(ds, pragma_line);

        Nodecl::NodeclBase statement = directive.get_statements();
        // This first context was added by nest_context_in_pragma
        ERROR_CONDITION(!statement.is<Nodecl::List>(), "Invalid tree", 0);
        statement = statement.as<Nodecl::List>().front();
        ERROR_CONDITION(!statement.is<Nodecl::Context>(), "Invalid tree", 0);
        // This is the usual context of the statements of a pragma
        statement = statement.as<Nodecl::Context>().get_in_context();

        Nodecl::NodeclBase num_threads;
        PragmaCustomClause clause = pragma_line.get_clause("num_threads");
        {
            ObjectList<Nodecl::NodeclBase> args = clause.get_arguments_as_expressions();
            if (clause.is_defined()
                    && args.size() == 1)
            {
                num_threads = args[0];
            }
            else if (clause.is_defined())
            {
                error_printf("%s: error: ignoring invalid 'num_threads' wrong clause\n",
                        directive.get_locus_str().c_str());
            }
        }

        if (!num_threads.is_null())
        {
            if (emit_omp_report())
            {
                *_omp_report_file
                    << OpenMP::Report::indent
                    << "Number of threads requested '" << num_threads.prettyprint() << "'\n";
            }
        }

        // Set implicit flushes at the entry and exit of the combined worksharing
        execution_environment.append(
                Nodecl::OpenMP::FlushAtEntry::make(
                    directive.get_locus())
        );
        execution_environment.append(
                Nodecl::OpenMP::FlushAtExit::make(
                    directive.get_locus())
        );

        // Set implicit barrier at the exit of the combined worksharing
        execution_environment.append(
                Nodecl::OpenMP::BarrierAtEnd::make(
                    directive.get_locus()));

        PragmaCustomClause if_clause = pragma_line.get_clause("if");
        {
            ObjectList<Nodecl::NodeclBase> expr_list = if_clause.get_arguments_as_expressions(directive);
            if (if_clause.is_defined()
                    && expr_list.size() == 1)
            {
                execution_environment.append(Nodecl::OpenMP::If::make(expr_list[0].shallow_copy()));

                if (emit_omp_report())
                {
                    *_omp_report_file
                        << OpenMP::Report::indent
                        << "A team of threads will be created only if '"
                        << expr_list[0].prettyprint()
                        << "' holds\n";
                }
            }
            else
            {
                if (if_clause.is_defined())
                {
                    running_error("%s: error: clause 'if' requires just one argument\n",
                            directive.get_locus_str().c_str());
                }
                // if (emit_omp_report())
                // {
                //     *_omp_report_file
                //         << directive.get_locus_str() << ": " << "A team of threads will always be created\n"
                //         ;
                // }
            }
        }

        Nodecl::NodeclBase sections_code = sections_handler_common(directive,
                statement,
                /* barrier_at_end */ false,
                /* is_combined_worksharing */ true);

        statement = directive.get_statements();
        // This first context was added by nest_context_in_pragma
        ERROR_CONDITION(!statement.is<Nodecl::List>(), "Invalid tree", 0);
        statement = statement.as<Nodecl::List>().front();
        ERROR_CONDITION(!statement.is<Nodecl::Context>(), "Invalid tree", 0);
        // Nest the for in the place where we expect the for-statement code
        statement.as<Nodecl::Context>().set_in_context(sections_code);

        Nodecl::NodeclBase parallel_code
            = Nodecl::OpenMP::Parallel::make(
                execution_environment,
                num_threads,
                directive.get_statements().shallow_copy(),
                directive.get_locus());

        pragma_line.diagnostic_unused_clauses();
        directive.replace(parallel_code);
    }

    void Base::parallel_for_handler_pre(TL::PragmaCustomStatement directive)
    {
        if (this->in_ompss_mode())
        {
            for_handler_pre(directive);
            return;
        }

        nest_context_in_pragma(directive);
    }
    void Base::parallel_for_handler_post(TL::PragmaCustomStatement directive)
    {
        if (emit_omp_report())
        {
            *_omp_report_file
                << "\n"
                << directive.get_locus_str() << ": " << "PARALLEL FOR construct\n"
                << directive.get_locus_str() << ": " << "----------------------\n"
                << directive.get_locus_str() << ": " << directive.get_statements().prettyprint() << "\n"
                ;
        }
        if (this->in_ompss_mode())
        {
            // In OmpSs this is like a simple for
            warn_printf("%s: warning: explicit parallel regions do not have any effect in OmpSs\n",
                    locus_to_str(directive.get_locus()));
            if (emit_omp_report())
            {
                *_omp_report_file
                    << OpenMP::Report::indent
                    << "Note that in OmpSs the PARALLEL part of PARALLEL FOR is ignored\n"
                    ;
            }
            for_handler_post(directive);
            return;
        }

        OpenMP::DataSharingEnvironment &ds = _core.get_openmp_info()->get_data_sharing(directive);
        PragmaCustomLine pragma_line = directive.get_pragma_line();
        Nodecl::List execution_environment = this->make_execution_environment_for_combined_worksharings(ds, pragma_line);

        Nodecl::NodeclBase statement = directive.get_statements();
        // This first context was added by nest_context_in_pragma
        ERROR_CONDITION(!statement.is<Nodecl::List>(), "Invalid tree", 0);
        statement = statement.as<Nodecl::List>().front();
        ERROR_CONDITION(!statement.is<Nodecl::Context>(), "Invalid tree", 0);
        // This is the usual context of the statements of a pragma
        statement = statement.as<Nodecl::Context>().get_in_context();
        ERROR_CONDITION(!statement.is<Nodecl::List>(), "Invalid tree", 0);
        statement = statement.as<Nodecl::List>().front();
        ERROR_CONDITION(!statement.is<Nodecl::Context>(), "Invalid tree", 0);

        Nodecl::NodeclBase num_threads;
        PragmaCustomClause clause = pragma_line.get_clause("num_threads");
        {
            ObjectList<Nodecl::NodeclBase> args = clause.get_arguments_as_expressions();
            if (clause.is_defined()
                    && args.size() == 1)
            {
                num_threads = args[0];
                if (emit_omp_report())
                {
                    *_omp_report_file
                        << OpenMP::Report::indent
                        << "Number of threads requested '" << num_threads.prettyprint() << "'\n";
                }
            }
            else
            {
                if (clause.is_defined())
                {
                    error_printf("%s: error: ignoring invalid 'num_threads' clause\n",
                            directive.get_locus_str().c_str());
                }
            }
        }

        // Set implicit flushes at the entry and exit of the combined worksharing
        execution_environment.append(
                Nodecl::OpenMP::FlushAtEntry::make(
                    directive.get_locus())
        );
        execution_environment.append(
                Nodecl::OpenMP::FlushAtExit::make(
                    directive.get_locus())
        );

        // Set implicit barrier at the exit of the combined worksharing
        execution_environment.append(
                Nodecl::OpenMP::BarrierAtEnd::make(
                    directive.get_locus()));

        PragmaCustomClause if_clause = pragma_line.get_clause("if");
        {
            ObjectList<Nodecl::NodeclBase> expr_list = if_clause.get_arguments_as_expressions(directive);
            if (if_clause.is_defined()
                    && expr_list.size() == 1)
            {
                execution_environment.append(Nodecl::OpenMP::If::make(expr_list[0].shallow_copy()));

                if (emit_omp_report())
                {
                    *_omp_report_file
                        << OpenMP::Report::indent
                        << "A team of threads will be created only if '"
                        << expr_list[0].prettyprint()
                        << "' holds\n";
                }
            }
            else
            {
                if (if_clause.is_defined())
                {
                    error_printf("%s: error: ignoring invalid 'if' clause\n",
                            directive.get_locus_str().c_str());
                }
                // if (emit_omp_report())
                // {
                //     *_omp_report_file
                //         << OpenMP::Report::indent
                //         << "A team of threads will always be created\n"
                //         ;
                // }
            }
        }

        // for-statement
        Nodecl::NodeclBase for_statement_code = loop_handler_post(directive,
                statement,
                /* barrier_at_end */ false,
                /* is_combined_worksharing */ true);

        statement = directive.get_statements();
        // This first context was added by nest_context_in_pragma
        ERROR_CONDITION(!statement.is<Nodecl::List>(), "Invalid tree", 0);
        statement = statement.as<Nodecl::List>().front();
        ERROR_CONDITION(!statement.is<Nodecl::Context>(), "Invalid tree", 0);
        // Nest the for in the place where we expect the for-statement code
        statement.as<Nodecl::Context>().set_in_context(for_statement_code);

        Nodecl::NodeclBase parallel_code
            = Nodecl::OpenMP::Parallel::make(
                    execution_environment,
                    num_threads,
                    directive.get_statements().shallow_copy(),
                    directive.get_locus());

        pragma_line.diagnostic_unused_clauses();
        directive.replace(parallel_code);
    }

    void Base::threadprivate_handler_pre(TL::PragmaCustomDirective) { }
    void Base::threadprivate_handler_post(TL::PragmaCustomDirective directive)
    {
        TL::PragmaCustomLine pragma_line = directive.get_pragma_line();
        OpenMP::DataSharingEnvironment &ds = _core.get_openmp_info()->get_data_sharing(directive);

        if (emit_omp_report())
        {
            *_omp_report_file
                << "\n"
                << directive.get_locus_str() << ": " << "THREADPRIVATE construct\n"
                << directive.get_locus_str() << ": " << "-----------------------\n"
                ;
        }

        TL::ObjectList<Symbol> threadprivate_symbols;
        ds.get_all_symbols(OpenMP::DS_THREADPRIVATE, threadprivate_symbols);

        if (!threadprivate_symbols.empty())
        {
            if (emit_omp_report())
            {
                *_omp_report_file
                    << OpenMP::Report::indent
                    << "List of variables set as threadprivate\n"
                    ;
            }

            for (TL::ObjectList<Symbol>::iterator it = threadprivate_symbols.begin();
                    it != threadprivate_symbols.end();
                    it++)
            {
                TL::Symbol &sym(*it);
                if (emit_omp_report())
                {
                    *_omp_report_file
                        << OpenMP::Report::indent
                        << sym.get_qualified_name() << std::endl;
                        ;
                }

                // Mark as __thread
                scope_entry_t* entry = sym.get_internal_symbol();
                symbol_entity_specs_set_is_thread(entry, 1);

                if (IS_FORTRAN_LANGUAGE)
                {
                    error_printf("%s: error: !$OMP THREADPRIVATE is not supported in Fortran\n",
                            directive.get_locus_str().c_str());
                }
            }
        }

        pragma_line.diagnostic_unused_clauses();
        Nodecl::Utils::remove_from_enclosing_list(directive);
    }

    void Base::declare_reduction_handler_pre(TL::PragmaCustomDirective) { }
    void Base::declare_reduction_handler_post(TL::PragmaCustomDirective directive)
    {
        TL::PragmaCustomLine pragma_line = directive.get_pragma_line();
        // Remove
        pragma_line.diagnostic_unused_clauses();
        Nodecl::Utils::remove_from_enclosing_list(directive);
    }

    void Base::register_handler_pre(TL::PragmaCustomDirective) { }
    void Base::register_handler_post(TL::PragmaCustomDirective directive)
    {
        TL::PragmaCustomLine pragma_line = directive.get_pragma_line();
        PragmaCustomParameter parameter = pragma_line.get_parameter();

        if (!parameter.is_defined())
        {
            error_printf("%s: error: missing parameter clause in '#pragma omp register'\n",
                    directive.get_locus_str().c_str());
            return;
        }

        ObjectList<Nodecl::NodeclBase> expr_list = parameter.get_arguments_as_expressions();
        if (expr_list.empty())
        {
            warn_printf("%s: warning: ignoring empty '#pragma omp register\n", 
                    directive.get_locus_str().c_str());
            return;
        }

        ObjectList<Nodecl::NodeclBase> valid_expr_list;

        for (TL::ObjectList<Nodecl::NodeclBase>::iterator it = expr_list.begin();
                it != expr_list.end();
                it++)
        {
            if (it->is<Nodecl::Symbol>() // x
                    || (it->is<Nodecl::Shaping>() // [n1][n2] x
                        && it->as<Nodecl::Shaping>().get_postfix().is<Nodecl::Symbol>()))
            {
                valid_expr_list.append(*it);
            }
            else
            {
                error_printf("%s: error: invalid object specification '%s' in '#pragma omp register'\n",
                        directive.get_locus_str().c_str(),
                        it->prettyprint().c_str());
            }
        }
        
        if (valid_expr_list.empty())
            return;

        Nodecl::List list_expr = Nodecl::List::make(valid_expr_list);

        Nodecl::OpenMP::Register new_register_directive = 
            Nodecl::OpenMP::Register::make(
                    list_expr,
                    directive.get_locus());

        pragma_line.diagnostic_unused_clauses();
        directive.replace(new_register_directive);
    }

    struct SymbolBuilder : TL::Functor<Nodecl::NodeclBase, Symbol>
    {
        private:
            const locus_t* _locus;

        public:
            SymbolBuilder(const locus_t* locus)
                : _locus(locus)
            {
            }

            virtual Nodecl::NodeclBase do_(ArgType arg) const
            {
                return arg.make_nodecl(/*set_ref*/true, _locus);
            }
    };

    struct SymbolReasonBuilder : TL::Functor<Nodecl::NodeclBase,
                                             DataSharingEnvironment::DataSharingInfoPair>
    {
        private:
            const locus_t* _locus;

        public:
            SymbolReasonBuilder(const locus_t* locus)
                : _locus(locus)
            {
            }

            virtual Nodecl::NodeclBase do_(ArgType arg) const
            {
                return arg.first.make_nodecl(/*set_ref*/true, _locus);
            }
    };

    struct ReportSymbols : TL::Functor<void, DataSharingEnvironment::DataSharingInfoPair>
    {
        private:
            const locus_t* _locus;
            DataSharingAttribute _data_sharing;
            std::ofstream *_omp_report_file;

            std::string string_of_data_sharing(DataSharingAttribute data_attr) const
            {
                std::string result;
                data_attr = DataSharingAttribute(data_attr & ~DS_IMPLICIT);

                switch (data_attr)
                {
#define CASE(x, str) case x : result += str; break;
                    CASE(DS_UNDEFINED, "<<undefined>>")
                    CASE(DS_SHARED, "shared")
                    CASE(DS_PRIVATE, "private")
                    CASE(DS_FIRSTPRIVATE, "firstprivate")
                    CASE(DS_LASTPRIVATE, "lastprivate")
                    CASE(DS_FIRSTLASTPRIVATE, "firstprivate and lastprivate")
                    CASE(DS_REDUCTION, "reduction")
                    CASE(DS_THREADPRIVATE, "threadprivate")
                    CASE(DS_COPYIN, "copyin")
                    CASE(DS_COPYPRIVATE, "copyprivate")
                    CASE(DS_NONE, "<<none>>")
                    CASE(DS_AUTO, "auto")
#undef CASE
                    default: result += "<<???unknown>>";
                }

                return result;
            }

        public:
            ReportSymbols(const locus_t* locus,
                    DataSharingAttribute data_sharing,
                    std::ofstream* omp_report_file)
                : _locus(locus),
                _data_sharing(data_sharing),
                _omp_report_file(omp_report_file)
            {
            }

            virtual void do_(ArgType arg) const
            {
                // These variables confuse the user
                if (arg.first.is_saved_expression())
                    return;

                // Let's make sure this is properly aligned
                std::stringstream ss;
                ss
                    << OpenMP::Report::indent
                    << OpenMP::Report::indent
                    << arg.first.get_qualified_name()
                    ;

                int length = ss.str().size();
                int diff = 10 - length;
                if (diff > 0)
                    std::fill_n( std::ostream_iterator<const char*>(ss), diff, " ");

                ss << string_of_data_sharing(_data_sharing);

                length = ss.str().size();
                diff = 20 - length;
                if (diff > 0)
                    std::fill_n( std::ostream_iterator<const char*>(ss), diff, " ");

                ss << " (" << arg.second << ")" << std::endl;

                *_omp_report_file << ss.str();
            }
    };

    struct ReductionSymbolBuilder : TL::Functor<Nodecl::NodeclBase, ReductionSymbol>
    {
        private:
            const locus_t* _locus;

        public:
            ReductionSymbolBuilder(const locus_t* locus)
                : _locus(locus)
            {
            }

            virtual Nodecl::NodeclBase do_(ArgType arg) const
            {
                return Nodecl::OpenMP::ReductionItem::make(
                        /* reductor */ Nodecl::Symbol::make(arg.get_reduction()->get_symbol(), _locus),
                        /* reduced symbol */ arg.get_symbol().make_nodecl(/* set_ref_type */ true, _locus),
                        /* reduction type */ Nodecl::Type::make(arg.get_reduction_type(), _locus),
                        _locus);
            }
    };

    struct ReportReductions : TL::Functor<void, ReductionSymbol>
    {
        private:
            const locus_t* _locus;
            std::ofstream* _omp_report_file;

        public:
            ReportReductions(const locus_t* locus,
                    std::ofstream* omp_report_file)
                : _locus(locus),
                _omp_report_file(omp_report_file)
            {
            }

            virtual void do_(ArgType arg) const
            {
                std::stringstream ss;
                ss
                    << OpenMP::Report::indent
                    << OpenMP::Report::indent
                    << arg.get_symbol().get_qualified_name()
                    ;

                int length = ss.str().size();
                int diff = 10 - length;
                if (diff > 0)
                    std::fill_n( std::ostream_iterator<const char*>(ss), diff, " ");

                ss << "reduction";

                length = ss.str().size();
                diff = 26 - length;
                if (diff > 0)
                    std::fill_n( std::ostream_iterator<const char*>(ss), diff, " ");

                ss
                    << " (explicitly declared as reduction in 'reduction' clause'."
                    " Using reduction declared in '"
                    << arg.get_reduction()->get_symbol().get_locus_str() << ")\n";

                *_omp_report_file
                    << ss.str();
            }
    };

    template <typename T>
    void Base::make_data_sharing_list(
            OpenMP::DataSharingEnvironment &data_sharing_env,
            OpenMP::DataSharingAttribute data_attr,
            const locus_t* locus,
            ObjectList<Nodecl::NodeclBase>& result_list)
    {
        TL::ObjectList<DataSharingEnvironment::DataSharingInfoPair> symbols;
        data_sharing_env.get_all_symbols_info(data_attr, symbols);

        if (!symbols.empty())
        {
            TL::ObjectList<Nodecl::NodeclBase> nodecl_symbols = symbols.map(SymbolReasonBuilder(locus));

            if (emit_omp_report())
            {
                symbols.map(ReportSymbols(locus, data_attr, this->_omp_report_file));
            }

            result_list.append(T::make(Nodecl::List::make(nodecl_symbols), locus));
        }
    }

    void Base::make_execution_environment_target_information(
            TargetInfo &target_info,
            TL::Symbol called_symbol,
            const locus_t* locus,
            // out
            TL::ObjectList<Nodecl::NodeclBase> &result_list)
    {
        TL::ObjectList<Nodecl::NodeclBase> devices;
        TL::ObjectList<Nodecl::NodeclBase> target_items;

        ObjectList<std::string> device_list = target_info.get_device_list();
        for (TL::ObjectList<std::string>::iterator it = device_list.begin(); it != device_list.end(); ++it)
        {
            devices.append(Nodecl::Text::make(*it, locus));
        }

        ObjectList<CopyItem> copy_in = target_info.get_copy_in();
        ObjectList<CopyItem> copy_out = target_info.get_copy_out();
        ObjectList<CopyItem> copy_inout = target_info.get_copy_inout();
        if (emit_omp_report())
        {
            if (!copy_in.empty()
                    || !copy_out.empty()
                    || !copy_inout.empty())
            {
                *_omp_report_file
                    << OpenMP::Report::indent
                    << "Copies\n"
                    ;
            }
        }
        make_copy_list<Nodecl::OpenMP::CopyIn>(
                copy_in,
                OpenMP::COPY_DIR_IN,
                locus,
                target_items);

        make_copy_list<Nodecl::OpenMP::CopyOut>(
                copy_out,
                OpenMP::COPY_DIR_OUT,
                locus,
                target_items);

        make_copy_list<Nodecl::OpenMP::CopyInout>(
                copy_inout,
                OpenMP::COPY_DIR_INOUT,
                locus,
                target_items);

        ObjectList<Nodecl::NodeclBase> ndrange_exprs = target_info.get_shallow_copy_of_ndrange();

        if (!ndrange_exprs.empty())
        {
            target_items.append(
                    Nodecl::OpenMP::NDRange::make(
                        Nodecl::List::make(ndrange_exprs),
                        Nodecl::Symbol::make(called_symbol, locus),
                        locus));
        }

        ObjectList<Nodecl::NodeclBase> shmem_exprs = target_info.get_shallow_copy_of_shmem();
        if (!shmem_exprs.empty())
        {
            target_items.append(
                    Nodecl::OpenMP::ShMem::make(
                        Nodecl::List::make(shmem_exprs),
                        Nodecl::Symbol::make(called_symbol, locus),
                        locus));
        }

        ObjectList<Nodecl::NodeclBase> onto_exprs = target_info.get_shallow_copy_of_onto();
        if (!onto_exprs.empty())
        {
            target_items.append(
                    Nodecl::OpenMP::Onto::make(
                        Nodecl::List::make(onto_exprs),
                        Nodecl::Symbol::make(called_symbol, locus),
                        locus));
        }

        std::string file = target_info.get_file();
        if (!file.empty())
        {
            target_items.append(
                    Nodecl::OpenMP::File::make(
                        Nodecl::Text::make(file),
                        Nodecl::Symbol::make(called_symbol, locus),
                        locus));
        }

        std::string name = target_info.get_name();
        if (!name.empty())
        {
            target_items.append(
                    Nodecl::OpenMP::Name::make(
                        Nodecl::Text::make(name),
                        Nodecl::Symbol::make(called_symbol, locus),
                        locus));
        }

        TargetInfo::implementation_table_t implementation_table = target_info.get_implementation_table();
        for (TargetInfo::implementation_table_t::iterator it = implementation_table.begin();
                it != implementation_table.end(); ++it)
        {
            std::string device_name = it->first;
            TL::ObjectList<Symbol> &implementors = it->second;
            for (TL::ObjectList<Symbol>::iterator it2 = implementors.begin();
                    it2 != implementors.end();
                    it2++)
            {
                TL::Symbol implementor = *it2;
                target_items.append(
                        Nodecl::OpenMP::Implements::make(
                            Nodecl::Text::make(device_name),
                            Nodecl::Symbol::make(implementor, locus),
                            locus));
            }
        }

        result_list.append(
                Nodecl::OpenMP::Target::make(
                    Nodecl::List::make(devices),
                    Nodecl::List::make(target_items),
                    locus));
    }

    Nodecl::List Base::make_execution_environment_for_combined_worksharings(
            OpenMP::DataSharingEnvironment &data_sharing_env,
            PragmaCustomLine pragma_line)
    {
        const locus_t* locus = pragma_line.get_locus();

        TL::ObjectList<Nodecl::NodeclBase> result_list;

        // We do not want a report for this sort of worksharings because it is confusing for users
        bool old_emit_omp_report = this->_omp_report;
        this->_omp_report = false;

        // Everything should go transparent here
        make_data_sharing_list<Nodecl::OpenMP::Shared>(
                data_sharing_env, OpenMP::DS_PRIVATE,
                locus,
                result_list);
        make_data_sharing_list<Nodecl::OpenMP::Shared>(
                data_sharing_env, OpenMP::DS_FIRSTPRIVATE,
                locus,
                result_list);
        make_data_sharing_list<Nodecl::OpenMP::Shared>(
                data_sharing_env, OpenMP::DS_LASTPRIVATE,
                locus,
                result_list);
        make_data_sharing_list<Nodecl::OpenMP::Shared>(
                data_sharing_env, OpenMP::DS_FIRSTLASTPRIVATE,
                locus,
                result_list);

        make_data_sharing_list<Nodecl::OpenMP::Shared>(
                data_sharing_env, OpenMP::DS_SHARED,
                locus,
                result_list);

        TL::ObjectList<ReductionSymbol> reductions;
        data_sharing_env.get_all_reduction_symbols(reductions);
        TL::ObjectList<Symbol> reduction_symbols = reductions.map(functor(&ReductionSymbol::get_symbol));
        if (!reduction_symbols.empty())
        {
            TL::ObjectList<Nodecl::NodeclBase> nodecl_symbols =
                reduction_symbols.map(SymbolBuilder(locus));

            result_list.append(Nodecl::OpenMP::Shared::make(Nodecl::List::make(nodecl_symbols),
                        locus));
        }

        TargetInfo& target_info = data_sharing_env.get_target_info();
        make_execution_environment_target_information(
                target_info,
                target_info.get_target_symbol(),
                locus,
                result_list);


        // FIXME - Dependences for combined worksharings???
        //
        // TL::ObjectList<OpenMP::DependencyItem> dependences;
        // data_sharing_env.get_all_dependences(dependences);

        // make_dependency_list<Nodecl::OpenMP::DepIn>(
        //         dependences,
        //         OpenMP::DEP_DIR_IN,
        //         pragma_line.get_locus(),
        //         result_list);

        // make_dependency_list<Nodecl::OpenMP::DepOut>(
        //         dependences,
        //         OpenMP::DEP_DIR_OUT,
        //         pragma_line.get_locus(),
        //         result_list);

        // make_dependency_list<Nodecl::OpenMP::DepInout>(
        //         dependences, OpenMP::DEP_DIR_INOUT,
        //         pragma_line.get_locus(),
        //         result_list);

        this->_omp_report = old_emit_omp_report;

        return Nodecl::List::make(result_list);
    }

    Nodecl::List Base::make_execution_environment(
            OpenMP::DataSharingEnvironment &data_sharing_env,
            PragmaCustomLine pragma_line,
            bool ignore_target_info)
    {
        const locus_t* locus = pragma_line.get_locus();

        TL::ObjectList<Nodecl::NodeclBase> result_list;

        if (emit_omp_report())
        {
            *_omp_report_file
                << OpenMP::Report::indent
                << "Data sharings of variables\n"
                ;
        }
        make_data_sharing_list<Nodecl::OpenMP::Private>(
                data_sharing_env, OpenMP::DS_PRIVATE,
                locus,
                result_list);
        make_data_sharing_list<Nodecl::OpenMP::Firstprivate>(
                data_sharing_env, OpenMP::DS_FIRSTPRIVATE,
                locus,
                result_list);
        make_data_sharing_list<Nodecl::OpenMP::Lastprivate>(
                data_sharing_env, OpenMP::DS_LASTPRIVATE,
                locus,
                result_list);
        make_data_sharing_list<Nodecl::OpenMP::FirstLastprivate>(
                data_sharing_env, OpenMP::DS_FIRSTLASTPRIVATE,
                locus,
                result_list);
        make_data_sharing_list<Nodecl::OpenMP::Auto>(
                data_sharing_env, OpenMP::DS_AUTO,
                locus,
                result_list);

        make_data_sharing_list<Nodecl::OpenMP::Shared>(
                data_sharing_env, OpenMP::DS_SHARED,
                locus,
                result_list);

        TL::ObjectList<ReductionSymbol> reductions;
        data_sharing_env.get_all_reduction_symbols(reductions);
        if (!reductions.empty())
        {
            TL::ObjectList<Nodecl::NodeclBase> reduction_nodes = reductions.map(ReductionSymbolBuilder(locus));

            if (emit_omp_report())
            {
                reductions.map(ReportReductions(locus, this->_omp_report_file));
            }

            result_list.append(
                    Nodecl::OpenMP::Reduction::make(Nodecl::List::make(reduction_nodes),
                        locus)
                    );
        }

        TL::ObjectList<ReductionSymbol> simd_reductions;
        data_sharing_env.get_all_simd_reduction_symbols(simd_reductions);
        if (!simd_reductions.empty())
        {
            TL::ObjectList<Nodecl::NodeclBase> simd_reduction_nodes = simd_reductions.map(ReductionSymbolBuilder(locus));

            result_list.append(
                    Nodecl::OpenMP::SimdReduction::make(Nodecl::List::make(simd_reduction_nodes),
                        locus)
                    );
        }

        TL::ObjectList<OpenMP::DependencyItem> dependences;
        data_sharing_env.get_all_dependences(dependences);

        if (emit_omp_report())
        {
            if (!dependences.empty())
            {
                *_omp_report_file
                    << OpenMP::Report::indent
                    << "Dependences\n"
                    ;
            }
        }
        make_dependency_list<Nodecl::OpenMP::DepIn>(
                dependences,
                OpenMP::DEP_DIR_IN,
                locus,
                result_list);

        make_dependency_list<Nodecl::OpenMP::DepInPrivate>(
                dependences,
                OpenMP::DEP_DIR_IN_PRIVATE,
                locus,
                result_list);

        make_dependency_list<Nodecl::OpenMP::DepOut>(
                dependences,
                OpenMP::DEP_DIR_OUT,
                locus,
                result_list);

        make_dependency_list<Nodecl::OpenMP::DepInout>(
                dependences, OpenMP::DEP_DIR_INOUT,
                locus,
                result_list);

        make_dependency_list<Nodecl::OpenMP::Concurrent>(
                dependences, OpenMP::DEP_CONCURRENT,
                locus,
                result_list);

        make_dependency_list<Nodecl::OpenMP::Commutative>(
                dependences, OpenMP::DEP_COMMUTATIVE,
                locus,
                result_list);

        if (!ignore_target_info)
        {
            // Build the tree which contains the target information
            TargetInfo& target_info = data_sharing_env.get_target_info();
            make_execution_environment_target_information(
                    target_info,
                    target_info.get_target_symbol(),
                    locus,
                    result_list);
        }

        return Nodecl::List::make(result_list);
    }

    } }

EXPORT_PHASE(TL::OpenMP::Base)
