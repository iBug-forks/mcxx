/*--------------------------------------------------------------------
  (C) Copyright 2006-2009 Barcelona Supercomputing Center 
                          Centro Nacional de Supercomputacion
  
  This file is part of Mercurium C/C++ source-to-source compiler.
  
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

#ifndef CXX_CEXPR_H
#define CXX_CEXPR_H

#include "libmcxx-common.h"
#include "cxx-ast-decls.h"
#include "cxx-scope-decls.h"
#include "cxx-buildscope-decls.h"
#include "cxx-macros.h"

MCXX_BEGIN_DECLS

enum literal_value_kind_t
{
    LVK_INVALID = 0,
    LVK_DEPENDENT_EXPR,
    LVK_SIGNED_INT,
    LVK_UNSIGNED_INT,
    LVK_SIGNED_LONG,
    LVK_UNSIGNED_LONG,
    LVK_SIGNED_LONG_LONG,
    LVK_UNSIGNED_LONG_LONG,
    LVK_BOOL,
    LVK_SIGNED_CHAR,
    LVK_UNSIGNED_CHAR,
    // TODO - Float values ?
};

typedef struct
{
    enum literal_value_kind_t kind;

    char template_dependent;

    union 
    {
        signed int signed_int;
        unsigned int unsigned_int;
        signed long int signed_long;
        unsigned long int unsigned_long;
        signed long long int signed_long_long;
        unsigned long long int unsigned_long_long;
        char boolean_value;
        signed char signed_char;
        unsigned char unsigned_char;
        // TODO - Float values ?
    } value;
} literal_value_t;

LIBMCXX_EXTERN literal_value_t evaluate_constant_expression(AST a, 
        decl_context_t decl_context);
LIBMCXX_EXTERN literal_value_t literal_value_false(void);
LIBMCXX_EXTERN literal_value_t literal_value_true(void);
LIBMCXX_EXTERN literal_value_t literal_value_zero(void);
LIBMCXX_EXTERN literal_value_t literal_value_one(void);
LIBMCXX_EXTERN literal_value_t literal_value_minus_one(void);
LIBMCXX_EXTERN literal_value_t increment_literal_value(literal_value_t e);
LIBMCXX_EXTERN AST tree_from_literal_value(literal_value_t e);
LIBMCXX_EXTERN char equal_literal_values(literal_value_t v1, literal_value_t v2);
LIBMCXX_EXTERN void gather_integer_literal_suffix(const char* text, char* is_long, char* is_unsigned);
LIBMCXX_EXTERN void gather_float_literal_suffix(const char* text, char* is_float, char* is_long_double);

LIBMCXX_EXTERN char literal_value_is_zero(literal_value_t e);
LIBMCXX_EXTERN char literal_value_is_negative(literal_value_t e);

LIBMCXX_EXTERN unsigned int literal_value_to_uint(literal_value_t v, char *valid);
LIBMCXX_EXTERN int literal_value_to_int(literal_value_t v, char *valid);

LIBMCXX_EXTERN char is_constant_expression(AST a, decl_context_t decl_context);

MCXX_END_DECLS

#endif // CXX_CEXPR_H
