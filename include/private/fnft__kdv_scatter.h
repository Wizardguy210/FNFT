/*
* This file is part of FNFT.
*
* FNFT is free software; you can redistribute it and/or
* modify it under the terms of the version 2 of the GNU General
* Public License as published by the Free Software Foundation.
*
* FNFT is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program. If not, see <http://www.gnu.org/licenses/>.
*
* Contributors:
* Sander Wahls (TU Delft) 2017-2018, 2023.
* Shrinivas Chimmalgi (TU Delft) 2017.
* Peter J Prins (TU Delft) 2020.
*/

/**
 * @file fnft__kdv_scatter.h
 * @brief Slow forward scattering.
 * @ingroup kdv
 */

#ifndef FNFT__KDV_SCATTER_H
#define FNFT__KDV_SCATTER_H

#include "fnft__kdv_discretization.h"
#include "fnft__akns_scatter.h"
#include <stdio.h>
#include "fnft__misc.h"
#include <string.h> // for memcpy
#include "fnft__errwarn.h"

/**
 * @brief Computes \f$a(\lambda)\f$, \f$ a'(\lambda) = \frac{\partial a(\lambda)}{\partial \lambda}\f$
 * and \f$b(\lambda)\f$ for complex values \f$\lambda\f$ assuming that they are very close to the true
 * bound-states.
 *
 * The function performs slow direct scattering and is primarily based on the references
 *      - Boffetta and Osborne, <a href="https://doi.org/10.1016/0021-9991(92)90370-E">&quot; Computation of the direct scattering transform for the nonlinear Schroedinger  equation,&quot;</a> J. Comput. Phys. 102(2), 1992.
 *      - Chimmalgi, Prins and Wahls, <a href="https://doi.org/10.1109/ACCESS.2019.2945480">&quot; Fast Nonlinear Fourier Transform Algorithms Using Higher Order Exponential Integrators,&quot;</a> IEEE Access 7, 2019.
 *      - Medvedev, Vaseva, Chekhovskoy and  Fedoruk, <a href="https://doi.org/10.1364/OE.377140">&quot; Exponential fourth order schemes for direct Zakharov-Shabat problem,&quot;</a> Optics Express, vol. 28, pp. 20--39, 2020.
 *      - Prins and Wahls, <a href="https://doi.org/10.1109/ACCESS.2019.2932256">&quot; Soliton Phase Shift Calculation for the Korteweg–De Vries Equation,&quot;</a> IEEE Access, vol. 7, pp. 122914--122930, July 2019.
 *
 *
 * @param[in] D Number of samples
 * @param[in] q Array of length D, contains samples \f$ q_n\f$ for \f$n=0,1,\dots,D-1\f$
 * in ascending order (i.e., \f$ q_0, q_1, \dots, q_{D-1} \f$). The values
 * should be specifically precalculated based on the chosen discretization.
 * @param[in,out] r Array of length D, contains samples \f$ r_n\f$ for \f$n=0,1,\dots,D-1\f$
 * in ascending order (i.e., \f$ r_0, r_1, \dots, r_{D-1} \f$). The values
 * should be specifically precalculated based on the chosen discretization. Alternatively NULL can be
 * passed. When NULL is passed the routine allocates memory, assigns it to the pointer and calculates \f$ r_n\f$ from \f$ q_n\f$.
 * @param[in] T Array of length 2, contains the position in time of the first and
 *  of the last sample. It should be T[0]<T[1].
 * @param[in] K Number of bound-states.
 * @param[in] bound_states Array of length K, contains the bound-states \f$\lambda\f$.
 * @param[out] a_vals Array of length K, contains the values of \f$a(\lambda)\f$.
 * @param[out] aprime_vals Array of length K, contains the values of
 * \f$ a'(\lambda) = \frac{\partial a(\lambda)}{\partial \lambda}\f$.
 * @param[out] b Array of length K, contains the values of \f$b(\lambda)\f$.
 * The \f$b(\lambda)\f$ are calculated using the criterion from
 * Prins and Wahls, <a href="https://doi.org/10.1109/ACCESS.2019.2932256">&quot;
 * Soliton Phase Shift Calculation for the Korteweg–De Vries Equation,&quot;</a>.
 * @param[in] discretization The type of discretization to be used. Should be of type
 * \link fnft_kdv_discretization_t \endlink. Not all kdv_discretization_t discretizations are supported.
 * Check \link fnft_kdv_discretization_t \endlink for list of supported types.
 * @param[in] skip_b_flag If set to 1 the routine will not compute \f$b(\lambda)\f$.
 * @return \link FNFT_SUCCESS \endlink or one of the FNFT_EC_... error codes
 *  defined in \link fnft_errwarn.h \endlink.
 * @ingroup kdv
 */
FNFT_INT fnft__kdv_scatter_bound_states(const FNFT_UINT D, FNFT_COMPLEX const * const q,
    FNFT_COMPLEX const * const r, FNFT_REAL const * const T, FNFT_UINT const K,
    FNFT_COMPLEX * const bound_states, FNFT_COMPLEX * const a_vals,
    FNFT_COMPLEX * const aprime_vals, FNFT_COMPLEX * const b,
    fnft_kdv_discretization_t const discretization, FNFT_UINT const skip_b_flag);

/**
 * @brief Computes the scattering matrix and its derivative.
 *
 * The function computes the scattering matrix and the derivative of the scattering matrix with
 * respect to \f$\lambda\f$. The function performs slow direct scattering and is primarily based on the reference
 * Boffetta and Osborne
 * (<a href="http://dx.doi.org/10.1016/0021-9991(92)90370-E">J. Comput. Physics 1992 </a>).
 *
 * @param[in] D Number of samples
 * @param[in] q Array of length D, contains samples \f$ q_n\f$ for \f$n=0,1,\dots,D-1\f$
 * in ascending order (i.e., \f$ q_0, q_1, \dots, q_{D-1} \f$). The values
 * should be specifically precalculated based on the chosen discretization.
 * @param[in,out] r Array of length D, contains samples \f$ r_n\f$ for \f$n=0,1,\dots,D-1\f$
 * in ascending order (i.e., \f$ r_0, r_1, \dots, r_{D-1} \f$). The values
 * should be specifically precalculated based on the chosen discretization. Alternatively NULL can be
 * passed. When NULL is passed the routine allocates memory, assigns it to the pointer and calculates \f$ r_n\f$ from \f$ q_n\f$.
 * @param[in] eps_t Step-size, eps_t \f$= (T[1]-T[0])/(D-1) \f$.
 * @param[in] kappa Unused.
 * @param[in] K Number of values of \f$\lambda\f$.
 * @param[in] lambda Array of length K, contains the values of \f$\lambda\f$.
 * @param[out] result Array of length 8*K or 4*K, If derivative_flag=0 returns
 * [S11 S12 S21 S22] in result where S = [S11, S12; S21, S22] is the
 * scattering matrix computed using the chosen discretization.
 * If derivative_flag=1 returns [S11 S12 S21 S22 S11' S12' S21' S22'] in
 * result where S11' is the derivative of S11 w.r.t to lambda.
 * Should be preallocated with size 4*K or 8*K accordingly.
 * @param[in] discretization The type of discretization to be used. Should be of type
 * \link fnft_kdv_discretization_t \endlink. Not all kdv_discretization_t discretizations are supported.
 * Check \link fnft_kdv_discretization_t \endlink for list of supported types.
 * @param[in] derivative_flag Should be set to either 0 or 1. If set to 1
 * the derivatives [S11' S12' S21' S22'] are calculated. result should be
 * preallocated with size 8*K if flag is set to 1.
 * @return \link FNFT_SUCCESS \endlink or one of the FNFT_EC_... error codes
 *  defined in \link fnft_errwarn.h \endlink.
 * @ingroup kdv
 */
FNFT_INT fnft__kdv_scatter_matrix(const FNFT_UINT D, 
    FNFT_COMPLEX const * const q, FNFT_COMPLEX const * const r,
    const FNFT_REAL eps_t, const FNFT_INT kappa, const FNFT_UINT K,
    FNFT_COMPLEX const * const lambda, FNFT_COMPLEX * const result, 
    FNFT_INT * const W, fnft_kdv_discretization_t const discretization,
    const FNFT_UINT derivative_flag);

#ifdef FNFT_ENABLE_SHORT_NAMES
#define kdv_scatter_bound_states(...) fnft__kdv_scatter_bound_states(__VA_ARGS__)
#define kdv_scatter_matrix(...) fnft__kdv_scatter_matrix(__VA_ARGS__)
#endif

#endif
