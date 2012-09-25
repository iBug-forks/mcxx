! --------------------------------------------------------------------
!   (C) Copyright 2006-2011 Barcelona Supercomputing Center 
!                           Centro Nacional de Supercomputacion
!   
!   This file is part of Mercurium C/C++ source-to-source compiler.
!   
!   See AUTHORS file in the top level directory for information 
!   regarding developers and contributors.
!   
!   This library is free software; you can redistribute it and/or
!   modify it under the terms of the GNU Lesser General Public
!   License as published by the Free Software Foundation; either
!   version 3 of the License, or (at your option) any later version.
!   
!   Mercurium C/C++ source-to-source compiler is distributed in the hope
!   that it will be useful, but WITHOUT ANY WARRANTY; without even the
!   implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
!   PURPOSE.  See the GNU Lesser General Public License for more
!   details.
!   
!   You should have received a copy of the GNU Lesser General Public
!   License along with Mercurium C/C++ source-to-source compiler; if
!   not, write to the Free Software Foundation, Inc., 675 Mass Ave,
!   Cambridge, MA 02139, USA.
! --------------------------------------------------------------------

! <testinfo>
! test_generator=config/mercurium-fortran
! compile_versions="cache nocache"
! test_FFLAGS_cache=""
! test_FFLAGS_nocache="--debug-flags=disable_module_cache"
! </testinfo>

MODULE M
CONTAINS

    SUBROUTINE FOO(X)
        IMPLICIT NONE
        INTEGER :: X
        X = X + 1
    END SUBROUTINE FOO

END MODULE M

MODULE N
    USE M
    INTERFACE INTER_FOO
        MODULE PROCEDURE FOO
        MODULE PROCEDURE FOO1
    END INTERFACE INTER_FOO
    CONTAINS
        SUBROUTINE  FOO1
        END SUBROUTINE  FOO1
END MODULE N
