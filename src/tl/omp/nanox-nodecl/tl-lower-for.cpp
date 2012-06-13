/*--------------------------------------------------------------------
  (C) Copyright 2006-2012 Barcelona Supercomputing Center
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


#include "tl-source.hpp"
#include "tl-lowering-visitor.hpp"
#include "tl-nodecl-utils.hpp"
#include "cxx-cexpr.h"
#include "tl-predicateutils.hpp"

namespace TL { namespace Nanox {

    Source LoweringVisitor::get_loop_distribution_source(
            const Nodecl::OpenMP::For &construct,
            Nodecl::List& distribute_environment,
            Nodecl::List& ranges,
            OutlineInfo& outline_info,
            Nodecl::NodeclBase &placeholder1,
            Nodecl::NodeclBase &placeholder2)
    {
        Source for_code, reduction_code, reduction_initialization, barrier_code;
        if (ranges.size() == 1)
        {
            Nodecl::Range range_item = ranges.front().as<Nodecl::Range>();

            TL::Symbol ind_var = range_item.get_symbol();
            Nodecl::OpenMP::ForRange range(range_item.as<Nodecl::OpenMP::ForRange>());

            for_code
                << "while (nanos_item_loop.execute)"
                << "{"
                ;

            if (range.get_step().is_constant())
            {
                const_value_t* cval = range.get_step().get_constant();

                Nodecl::NodeclBase cval_nodecl = const_value_to_nodecl(cval);

                if (const_value_is_positive(cval))
                {

                    for_code
                        << "for (" << ind_var.get_name() << " = nanos_item_loop.lower;"
                        << ind_var.get_name() << " <= nanos_item_loop.upper;"
                        << ind_var.get_name() << " += " << cval_nodecl.prettyprint() << ")"
                        << "{"
                        ;
                }
                else
                {
                    for_code
                        << "for (" << ind_var.get_name() << " = nanos_item_loop.lower;"
                        << ind_var.get_name() << " >= nanos_item_loop.upper;"
                        << ind_var.get_name() << " += " << cval_nodecl.prettyprint() << ")"
                        << "{"
                        ;
                }

                for_code
                    << statement_placeholder(placeholder1)
                    << "}"
                    ;
            }
            else
            {

                for_code
                    << as_type(range.get_step().get_type()) << " nanos_step = " << as_expression(range.get_step()) << ";"
                    << "if (nanos_step > 0)"
                    << "{"
                    <<    "for (" << ind_var.get_name() << " = nanos_item_loop.lower;"
                    <<    ind_var.get_name() << " <= nanos_item_loop.upper;"
                    <<    ind_var.get_name() << " += nanos_step)"
                    <<    "{"
                    <<    statement_placeholder(placeholder1)
                    <<    "}"
                    << "}"
                    << "else"
                    << "{"
                    <<    "for (" << ind_var.get_name() << " = nanos_item_loop.lower;"
                    <<    ind_var.get_name() << " >= nanos_item_loop.upper;"
                    <<    ind_var.get_name() << " += nanos_step)"
                    <<    "{"
                    <<    statement_placeholder(placeholder2)
                    <<    "}"
                    << "}"
                    ;
            }

            for_code
                << "err = nanos_worksharing_next_item(wsd, (void**)&nanos_item_loop);"
                << "}"
                ;
        }
        else if (ranges.size() > 1)
        {
            internal_error("Collapsed ranges not implemented yet", 0);
        }
        else
        {
            internal_error("Code unreachable", 0);
        }

        Source distribute_loop_source;
        distribute_loop_source
            << "{"
            << reduction_initialization
            << "nanos_ws_item_loop_t nanos_item_loop;"
            << "nanos_err_t err;"
            << "err = nanos_worksharing_next_item(wsd, (void**)&nanos_item_loop);"
            << "if (err != NANOS_OK)"
            <<     "nanos_handle_error(err);"
            << for_code
            << reduction_code
            << "}"
            << barrier_code
            ;

        reduction_initialization << reduction_initialization_code(outline_info, construct);
        reduction_code << perform_partial_reduction(outline_info);

        if (!distribute_environment.find_first<Nodecl::OpenMP::BarrierAtEnd>().is_null())
        {
            barrier_code
                << full_barrier_source();
        }

        return distribute_loop_source;
    }

    void LoweringVisitor::distribute_loop_with_outline(
           const Nodecl::OpenMP::For& construct,
           Nodecl::List& distribute_environment,
           Nodecl::List& ranges,
           OutlineInfo& outline_info,
           Nodecl::NodeclBase& statements,
           Source &outline_distribute_loop_source,
           // Loop (in the outline distributed code)
           Nodecl::NodeclBase& outline_placeholder1,
           // Auxiliar loop (when the step is not known at compile time, in the outline distributed code)
           Nodecl::NodeclBase& outline_placeholder2
           )
    {
        Symbol function_symbol = Nodecl::Utils::get_enclosing_function(construct);

        std::string outline_name = get_outline_name(function_symbol);

        // Add field wsd
        TL::Symbol sym = ReferenceScope(construct).get_scope().get_symbol_from_name("nanos_ws_desc_t");
        ERROR_CONDITION(sym.is_invalid(), "Invalid symbol", 0);

        TL::Type nanos_ws_desc_type = ::get_user_defined_type(sym.get_internal_symbol());
        nanos_ws_desc_type = nanos_ws_desc_type.get_pointer_to();

        if (IS_FORTRAN_LANGUAGE)
        {
            nanos_ws_desc_type = nanos_ws_desc_type.get_lvalue_reference_to();
        }

        OutlineDataItem &wsd_data_item = outline_info.prepend_field("wsd", nanos_ws_desc_type);
        wsd_data_item.set_sharing(OutlineDataItem::SHARING_CAPTURE);

        // Build the structure
        TL::Symbol structure_symbol = declare_argument_structure(outline_info, construct);

        Nodecl::Utils::SymbolMap *symbol_map = NULL;
        emit_outline(outline_info, statements,
                outline_distribute_loop_source,
                outline_name, structure_symbol, symbol_map);

        // Now complete the placeholders
        if (IS_FORTRAN_LANGUAGE)
        {
            // Copy FUNCTIONs and other local stuff
            symbol_map = new Nodecl::Utils::FortranProgramUnitSymbolMap(symbol_map,
                    function_symbol,
                    outline_info.get_unpacked_function_symbol());
        }

        outline_placeholder1.integrate(
                Nodecl::Utils::deep_copy(statements, outline_placeholder1, *symbol_map)
                );

        if (!outline_placeholder2.is_null())
        {
            outline_placeholder2.integrate(
                    Nodecl::Utils::deep_copy(statements, outline_placeholder2, *symbol_map)
                    );
        }

        delete symbol_map; symbol_map = NULL;

        loop_spawn(outline_info, construct, distribute_environment, ranges, outline_name, structure_symbol);
    }

    void LoweringVisitor::visit(const Nodecl::OpenMP::For& construct)
    {
        Nodecl::List ranges = construct.get_ranges().as<Nodecl::List>();

        Nodecl::List distribute_environment = construct.get_environment().as<Nodecl::List>();

        Nodecl::NodeclBase statements = construct.get_statements();

        walk(statements);

        // Get the new statements
        statements = construct.get_statements();

        Nodecl::NodeclBase environment = construct.get_environment();

        OutlineInfo outline_info(environment);

        Nodecl::NodeclBase outline_placeholder1, outline_placeholder2;
        Source outline_distribute_loop_source = get_loop_distribution_source(construct,
                distribute_environment,
                ranges,
                outline_info,
                outline_placeholder1,
                outline_placeholder2);

        distribute_loop_with_outline(construct,
                distribute_environment, ranges,
                outline_info,
                statements,
                outline_distribute_loop_source,
                outline_placeholder1,
                outline_placeholder2);
    }

} }