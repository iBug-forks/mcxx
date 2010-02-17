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


struct true_value { };
struct false_value { };

template <bool _B>
struct check
{
    static true_value f();
};

template <>
struct check<false>
{
    static false_value f();
};

struct A1
{
};

struct A2
{
};

struct A3 : A1
{
};

struct A4 : A3
{
};

struct A5 : A2, A3
{
};

int main(int argc, char* argv[])
{
    {
        false_value tr;
        tr = check<__is_base_of(A1, A2)>::f();
    }

    {
        true_value tr;
        tr = check<__is_base_of(A1, A3)>::f();
    }

    {
        true_value tr;
        tr = check<__is_base_of(A1, A4)>::f();
    }

    {
        true_value tr;
        tr = check<__is_base_of(A1, A5)>::f();
    }
}
