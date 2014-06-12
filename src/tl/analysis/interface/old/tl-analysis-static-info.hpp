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

#ifndef TL_ANALYSIS_STATIC_INFO_HPP
#define TL_ANALYSIS_STATIC_INFO_HPP

#include "tl-analysis-singleton.hpp"
#include "tl-induction-variables-data.hpp"
#include "tl-nodecl-visitor.hpp"
#include "tl-objectlist.hpp"
#include "tl-omp.hpp"

namespace TL {
namespace Analysis {

    // ********************************************************************************************* //
    // ********************** Class to define which analysis are to be done ************************ //

    struct WhichAnalysis
    {
        // Macros defining the analysis to be computed
        enum Analysis_tag
        {
            PCFG_ANALYSIS           = 1u << 1,
            USAGE_ANALYSIS          = 1u << 2,
            LIVENESS_ANALYSIS       = 1u << 3,
            REACHING_DEFS_ANALYSIS  = 1u << 4,
            INDUCTION_VARS_ANALYSIS = 1u << 5,
            CONSTANTS_ANALYSIS      = 1u << 6,
            AUTO_SCOPING            = 1u << 7
        } _which_analysis;

        WhichAnalysis( Analysis_tag a );
        WhichAnalysis( int a );
        WhichAnalysis operator|( WhichAnalysis a );
    };

    struct WhereAnalysis
    {
        // Macros defining whether the Static Info must be computed in nested block
        enum Nested_analysis_tag
        {
            NESTED_NONE_STATIC_INFO         = 1u << 0,
            NESTED_IF_STATIC_INFO           = 1u << 2,
            NESTED_DO_STATIC_INFO           = 1u << 3,
            NESTED_WHILE_STATIC_INFO        = 1u << 4,
            NESTED_FOR_STATIC_INFO          = 1u << 5,
            NESTED_OPENMP_TASK_STATIC_INFO  = 1u << 6,
            NESTED_ALL_STATIC_INFO          = 0xFF
        } _where_analysis;

        WhereAnalysis( Nested_analysis_tag a );
        WhereAnalysis( int a );
        WhereAnalysis operator|( WhereAnalysis a );
    };

    // ******************** END class to define which analysis are to be done ********************** //
    // ********************************************************************************************* //



    // ********************************************************************************************* //
    // **************** Class to retrieve analysis info about one specific nodecl ****************** //

    class NodeclStaticInfo
    {
        private:
            Utils::InductionVarList _induction_variables;
            ObjectList<Symbol> _reductions;
            NodeclSet _killed;
            ObjectList<ExtensibleGraph*> _pcfgs;
            Node* _autoscoped_task;

            Node* find_node_from_nodecl(const NBase& n) const;
            Node* find_node_from_nodecl_pointer(const NBase& n) const;
            Node* find_node_from_nodecl_in_scope(const NBase& n, const NBase& scope) const;
            ExtensibleGraph* find_extensible_graph_from_nodecl_pointer(const NBase& n) const;

        public:
            NodeclStaticInfo(Utils::InductionVarList induction_variables,
                              ObjectList<Symbol> reductions,
                             NodeclSet killed, ObjectList<ExtensibleGraph*> pcfgs,
                              Node* autoscoped_task );

            ~NodeclStaticInfo();
            
            // *** Queries about Use-Def analysis *** //

            bool is_constant(const NBase& n) const;

            bool has_been_defined(const NBase& n, const NBase& s, const NBase& scope) const;

            // *** Queries about induction variables *** //

            bool is_induction_variable(const NBase& n) const;

            bool is_basic_induction_variable(const NBase& n) const;

            bool is_non_reduction_basic_induction_variable(const NBase& n) const;

            // This methods traverse the PCFG to analyze, so we do not need the static info.
            // This kind of usage of the analysis should be implemented in a different interface
            static bool is_nested_induction_variable(Node* scope_node, Node* node, const NBase& n);
            static Utils::InductionVar* get_nested_induction_variable(Node* scope_node, Node* node, const NBase& n);

            NBase get_induction_variable_lower_bound(const NBase& n) const;

            NBase get_induction_variable_increment(const NBase& n) const;

            NodeclList get_induction_variable_increment_list(const NBase& n) const;

            NBase get_induction_variable_upper_bound(const NBase& n) const;

            bool is_induction_variable_increment_one(const NBase& n) const;

            //! Returns the induction variable containing the given nodecl
            //! If the nodecl is not an induction variable, then returns NULL
            Utils::InductionVar* get_induction_variable(const NBase& n) const;

            Utils::InductionVarList get_induction_variables() const;


            // *** Queries for Vectorization *** //

            bool is_induction_variable_dependent_expression(const NBase& n, Node* scope_node) const;

//             bool contains_induction_variable(const NBase& n, Node* scope_node) const;

            bool var_is_iv_dependent_in_scope(const NBase& n, Node* scope_node) const;

            bool is_constant_access(const NBase& n) const;

            // *** Queries about Auto-Scoping *** //

            void print_auto_scoping_results( ) const;

            Utils::AutoScopedVariables get_auto_scoped_variables( );

        friend class AnalysisStaticInfo;
    };

    // ************** END class to retrieve analysis info about one specific nodecl **************** //
    // ********************************************************************************************* //



    // ********************************************************************************************* //
    // **************************** User interface for static analysis ***************************** //

    typedef std::map<NBase, NodeclStaticInfo> static_info_map_t;
    typedef std::pair<NBase, NodeclStaticInfo> static_info_pair_t;

    typedef std::map<Nodecl::NodeclBase, ExtensibleGraph*> nodecl_to_pcfg_map_t;

    class AnalysisStaticInfo
    {
        private:
            NBase _node;
            static_info_map_t _static_info_map;
            bool _ompss_mode_enabled;

        protected:
            nodecl_to_pcfg_map_t _func_to_pcfg_map;

        public:
            // *** Constructors *** //
            //! Constructor useful to make queries that do not require previous analyses
            AnalysisStaticInfo( );

            AnalysisStaticInfo(const NBase& n, WhichAnalysis analysis_mask,
                                WhereAnalysis nested_analysis_mask, int nesting_level, bool ompss_mode_enabled);

            virtual ~AnalysisStaticInfo();


            // *** Getters and Setters *** //

            static_info_map_t get_static_info_map( ) const;

            //! Returns the nodecl that originated the analysis
            NBase get_nodecl_origin() const;


            // *** Queries about Use-Def analysis *** //

            //! Returns true when an object is constant in a given scope
            virtual bool is_constant(const NBase& scope, const NBase& n) const;

            virtual bool has_been_defined(const NBase& scope, const NBase& n,
                                   const NBase& s) const;


            // *** Queries about induction variables *** //

            //! Returns true when an object is an induction variable in a given scope
            virtual bool is_induction_variable(const NBase& scope, const NBase& n) const;

            //! Returns true when an object is an induction variable in a given scope
            virtual bool is_basic_induction_variable(const NBase& scope, const NBase& n) const;

            virtual bool is_non_reduction_basic_induction_variable(const NBase& scope, const NBase& n) const;

            //! Returns the const_value corresponding to the lower bound of an induction variable in a given scope
            virtual NBase get_induction_variable_lower_bound(const NBase& scope, const NBase& n) const;

            //! Returns the const_value corresponding to the increment of an induction variable in a given scope
            virtual NBase get_induction_variable_increment(const NBase& scope, const NBase& n) const;

            //! Returns the list of const_values containing the increments of an induction variable in a given scope
            virtual NodeclList get_induction_variable_increment_list(const NBase& scope, const NBase& n) const;

            //! Returns the const_value corresponding to the upper bound of an induction variable in a given scope
            virtual NBase get_induction_variable_upper_bound(const NBase& scope, const NBase& n) const;

            //! Returns true when the increment of a given induction variable is constant and equal to 1
            virtual bool is_induction_variable_increment_one(const NBase& scope, const NBase& n) const;

            //! Returns a list with the induction variables of a given scope
            virtual Utils::InductionVarList get_induction_variables(const NBase& scope, const NBase& n) const;


            // *** Queries about OmpSs *** //

            virtual bool is_ompss_reduction(const NBase& n, RefPtr<OpenMP::FunctionTaskSet> function_tasks) const;


            // *** Queries for Vectorization *** //

            //! Returns true if the given nodecl is an expression that contains an IV from ivs_scope
            virtual bool is_induction_variable_dependent_expression(const NBase& ivs_scope, const NBase& n) const;

            virtual bool contains_induction_variable(const NBase& scope, const NBase& n) const;

            //! Returns true if the given nodecl is an array accessed by a constant expression
            virtual bool is_constant_access(const NBase& scope, const NBase& n) const;


            // *** Queries about Auto-Scoping *** //

            virtual void print_auto_scoping_results(const NBase& scope);

            virtual Utils::AutoScopedVariables get_auto_scoped_variables(const NBase scope);
    };

    // ************************** END User interface for static analysis *************************** //
    // ********************************************************************************************* //



    // ********************************************************************************************* //
    // ********************* Visitor retrieving the analysis of a given Nodecl ********************* //

    class LIBTL_CLASS NestedBlocksStaticInfoVisitor : public Nodecl::ExhaustiveVisitor<void>
    {
    private:
        //! State of the analysis for the given nodecl
        PCFGAnalysis_memento _state;

        //! Mask containing the analysis to be performed
        WhichAnalysis _analysis_mask;

        //! Mask containing the nested constructs to be parsed
        WhereAnalysis _nested_analysis_mask;

        //! Level of nesting of blocks inside the visited nodecl to be parsed
        int _nesting_level;

        //! Temporary member used during computation. Contains the current level of nesting
        int _current_level;

        //! Member where the analysis info is stored during the visit
        static_info_map_t _analysis_info;

        void retrieve_current_node_static_info(NBase n);

    public:
        // *** Constructor *** //
        NestedBlocksStaticInfoVisitor( WhichAnalysis analysis_mask, WhereAnalysis nested_analysis_mask,
                                       PCFGAnalysis_memento state, int nesting_level );

        // *** Getters and Setters *** //
        static_info_map_t get_analysis_info( );

        // *** Visiting methods *** //
        void join_list( ObjectList<static_info_map_t>& list );

        void visit( const Nodecl::DoStatement& n );
        void visit( const Nodecl::ForStatement& n );
        void visit( const Nodecl::FunctionCode& n );
        void visit( const Nodecl::IfElseStatement& n );
        void visit( const Nodecl::OpenMP::Task& n );
        void visit( const Nodecl::WhileStatement& n );
    };

    // ******************* END Visitor retrieving the analysis of a given Nodecl ******************* //
    // ********************************************************************************************* //
}
}

#endif // TL_ANALYSIS_STATIC_INFO_HPP
