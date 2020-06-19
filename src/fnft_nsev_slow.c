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
 * Sander Wahls (TU Delft) 2017-2018.
 * Shrinivas Chimmalgi (TU Delft) 2017-2020.
 * Marius Brehler (TU Dortmund) 2018.
 */

#define FNFT_ENABLE_SHORT_NAMES

#include <string.h> // for memcpy
#include <stdio.h>
#include "fnft__errwarn.h"
#include "fnft_nsev.h"
#include "fnft_nsev_slow.h"
#include "fnft__nse_scatter.h"
#include "fnft__nse_discretization.h"
#include "fnft__akns_discretization.h"
#include "fnft__misc.h" // for l2norm

static fnft_nsev_slow_opts_t default_opts = {
    .bound_state_filtering = nsev_bsfilt_FULL,
    .bound_state_localization = nsev_bsloc_NEWTON,
    .niter = 10,
    .discspec_type = nsev_dstype_NORMING_CONSTANTS,
    .contspec_type = nsev_cstype_REFLECTION_COEFFICIENT,
    .normalization_flag = 1,
    .discretization = nse_discretization_BO,
    .richardson_extrapolation_flag = 0
};

/**
 * Creates a new options variable for fnft_nsev with default settings.
 * See the header file for a detailed description.
 */
fnft_nsev_slow_opts_t fnft_nsev_slow_default_opts()
{
    return default_opts;
}



/**
 * Declare auxiliary routines used by the main routine fnft_nsev.
 * Their bodies follow below.
 */


static inline INT refine_roots_newton(
        const UINT D,
        COMPLEX const * const q,
        COMPLEX * r,
        REAL const * const T,
        const UINT K,
        COMPLEX * bound_states,
        nse_discretization_t discretization,
        const UINT niter);

static inline INT compute_normconsts_or_residues(
        const UINT D,
        COMPLEX const * const q,
        COMPLEX * r,
        REAL const * const T,
        const UINT K,
        COMPLEX * const bound_states,
        COMPLEX * const normconsts_or_residues,
        fnft_nsev_slow_opts_t * const opts);

static inline INT fnft_nsev_slow_base(
        const UINT D,
        COMPLEX * const q,
        COMPLEX * const r,
        REAL const * const T,
        const UINT M,
        COMPLEX * const contspec,
        REAL const * const XI,
        UINT * const K_ptr,
        COMPLEX * const bound_states,
        COMPLEX * const normconsts_or_residues,
        const INT kappa,
        fnft_nsev_slow_opts_t *opts);

static inline INT signal_effective_from_signal(
        const UINT D,
        COMPLEX const * const q,
        REAL const eps_t,
        const INT kappa,
        UINT * const Dsub_ptr,
        COMPLEX **q_effective_ptr,
        COMPLEX **r_effective_ptr,
        UINT * const first_last_index,
        nse_discretization_t discretization);

static inline REAL re_bound(const REAL eps_t);

static inline REAL im_bound(const UINT D, COMPLEX const * const q,
        REAL const * const T);

/**
 * Slow nonlinear Fourier transform for the nonlinear Schroedinger
 * equation with vanishing boundary conditions.
 * See the header file for documentation.
 */
INT fnft_nsev_slow(
        const UINT D,
        COMPLEX * const q,
        REAL const * const T,
        const UINT M,
        COMPLEX * const contspec,
        REAL const * const XI,
        UINT * const K_ptr,
        COMPLEX * const bound_states,
        COMPLEX * const normconsts_or_residues,
        const INT kappa,
        fnft_nsev_slow_opts_t *opts)
{
    
    COMPLEX *q_effective = NULL, *r_effective = NULL;
    
    UINT first_last_index[2];
    UINT K_sub;
    COMPLEX *contspec_sub = NULL;
    COMPLEX *bound_states_sub = NULL;
    COMPLEX *normconsts_or_residues_sub = NULL;
    COMPLEX *normconsts_or_residues_reserve = NULL;
    INT bs_loc_opt = 0, ds_type_opt = 0;
    INT ret_code = SUCCESS;
    UINT i, j, D_scale;// D_scale*D gives the effective number of samples
    
    // Check inputs
    if (D < 2)
        return E_INVALID_ARGUMENT(D);
    if (q == NULL)
        return E_INVALID_ARGUMENT(q);
    if (T == NULL || T[0] >= T[1])
        return E_INVALID_ARGUMENT(T);
    if (contspec != NULL) {
        if (XI == NULL || XI[0] >= XI[1])
            return E_INVALID_ARGUMENT(XI);
    }
    if (abs(kappa) != 1)
        return E_INVALID_ARGUMENT(kappa);
    if (bound_states != NULL) {
        if (K_ptr == NULL)
            return E_INVALID_ARGUMENT(K_ptr);
    }
    if (opts == NULL)
        opts = &default_opts;
    
    
    // Determine step size
    const REAL eps_t = (T[1] - T[0])/(D - 1);
    
    normconsts_or_residues_reserve = normconsts_or_residues;
    if (opts->richardson_extrapolation_flag == 1){
        ds_type_opt = opts->discspec_type;
        if (ds_type_opt == nsev_dstype_RESIDUES){
            opts->discspec_type = nsev_dstype_BOTH;
            normconsts_or_residues_reserve = malloc(*K_ptr*2 * sizeof(COMPLEX));
            if (normconsts_or_residues_reserve == NULL) {
                ret_code = E_NOMEM;
                goto release_mem;
            }
        }
    }
    D_scale = nse_discretization_D_scale(opts->discretization);
    if (D_scale == 0){
        ret_code =  E_INVALID_ARGUMENT(discretization);
        goto release_mem;
    }
    UINT Dsub = D;
    // Resample signal
    ret_code = signal_effective_from_signal(D, q, eps_t, kappa, &Dsub, &q_effective, &r_effective,
            first_last_index, opts->discretization);
    CHECK_RETCODE(ret_code, release_mem);
 
    ret_code = fnft_nsev_slow_base(D*D_scale, q_effective, r_effective, T, M, contspec, XI, K_ptr,
            bound_states, normconsts_or_residues, kappa, opts);
    CHECK_RETCODE(ret_code, release_mem);
    
    if (opts->richardson_extrapolation_flag == 1){
        // TODO - Optimize memory allocation
        // Allocating memory
        UINT contspec_len = 0;
        if (contspec != NULL && M > 0){
            switch (opts->contspec_type) {
                case nsev_cstype_BOTH:
                    contspec_len = 3*M;
                    break;
                case nsev_cstype_REFLECTION_COEFFICIENT:
                    contspec_len = M;
                    break;
                case nsev_cstype_AB:
                    contspec_len = 2*M;
                    break;
                default:
                    ret_code = E_INVALID_ARGUMENT(opts->contspec_type);
                    goto release_mem;
            }
            contspec_sub = malloc(contspec_len * sizeof(COMPLEX));
            if (contspec_sub == NULL) {
                ret_code = E_NOMEM;
                goto release_mem;
            }
        }
        UINT discspec_len = 0;
        if (kappa == +1 && bound_states != NULL && *K_ptr != 0) {
            K_sub = *K_ptr;
            bound_states_sub = malloc(K_sub * sizeof(COMPLEX));
            discspec_len = K_sub;
            switch (opts->discspec_type) {
                case nsev_dstype_BOTH:
                case nsev_dstype_RESIDUES:
                    discspec_len = 2*K_sub;
                    break;
                case nsev_dstype_NORMING_CONSTANTS:
                    discspec_len = K_sub;
                    break;
                default:
                    ret_code = E_INVALID_ARGUMENT(opts->discspec_type);
                    goto release_mem;
            }
            normconsts_or_residues_sub = malloc(discspec_len * sizeof(COMPLEX));
            if (normconsts_or_residues_sub == NULL || bound_states_sub == NULL) {
                ret_code = E_NOMEM;
                goto release_mem;
            }
            for (i=0; i<K_sub; i++)
                bound_states_sub[i] = bound_states[i];
        }
        // Preparing q_effective
        UINT method_order;
        method_order = nse_discretization_method_order(opts->discretization);
        if (method_order == 0){
            ret_code =  E_INVALID_ARGUMENT(discretization);
            goto release_mem;
        }
        Dsub = CEIL(D/2);
        ret_code = signal_effective_from_signal(D, q, eps_t, kappa, &Dsub, &q_effective, &r_effective,
                first_last_index, opts->discretization);
        CHECK_RETCODE(ret_code, release_mem);


        REAL Tsub[2] = {0,0};
        Tsub[0] = T[0] + first_last_index[0]*eps_t;
        Tsub[1] = T[0] + first_last_index[1]*eps_t;
        const REAL eps_t_sub = (Tsub[1] - Tsub[0])/(Dsub - 1);

        // Calling fnft_nsev_base with subsampled signal
        bs_loc_opt = opts->bound_state_localization;
        opts->bound_state_localization = nsev_bsloc_NEWTON;
        ret_code = fnft_nsev_slow_base(Dsub*D_scale, q_effective, r_effective, Tsub, M, contspec_sub,
                XI, &K_sub, bound_states_sub, normconsts_or_residues_sub, kappa, opts);
        CHECK_RETCODE(ret_code, release_mem);
        opts->bound_state_localization = bs_loc_opt;
        opts->discspec_type = ds_type_opt;
        // Richardson step
        REAL const scl_num = POW(((REAL)D / (REAL)Dsub),method_order);
        REAL const scl_den = scl_num - 1.0;
        REAL const dxi = (XI[1]-XI[0])/(M-1);
        if (contspec != NULL && M > 0){
            for (i=0; i<M; i++){
                if (FABS(XI[0]+dxi*i) < 0.9*PI/(2.0*eps_t_sub)){
                    for (j=0; j<contspec_len; j+=M)
                        contspec[i+j] = (scl_num*contspec[i+j] - contspec_sub[i+j])/scl_den;
                }
            }
        }
        if (kappa == +1 && bound_states != NULL && *K_ptr != 0 && K_sub != 0) {
            UINT loc = K_sub;
            REAL bs_err_thres = eps_t;
            REAL bs_err = eps_t;
            UINT K = *K_ptr;
            
            
            for (i=0; i<K; i++){
                loc = K_sub;
                bs_err_thres = eps_t;
                for (j=0; j<K_sub; j++){
                    bs_err = CABS(bound_states[i]-bound_states_sub[j])/CABS(bound_states[i]);
                    if (bs_err < bs_err_thres){
                        bs_err_thres = bs_err;
                        loc = j;
                    }
                }
                if (loc < K_sub){
                    bound_states[i] = (scl_num*bound_states[i] - bound_states_sub[loc])/scl_den;
                    if (ds_type_opt == nsev_dstype_RESIDUES || ds_type_opt == nsev_dstype_BOTH){
                        // Computing aprimes from residues and norming constants
                        normconsts_or_residues_reserve[K+i] = normconsts_or_residues_reserve[i]/normconsts_or_residues_reserve[K+i];
                        normconsts_or_residues_sub[K_sub+loc] = normconsts_or_residues_sub[loc]/normconsts_or_residues_sub[K_sub+loc];
                        // Richardson step on aprime
                        normconsts_or_residues_reserve[K+i] = (scl_num*normconsts_or_residues_reserve[K+i] - normconsts_or_residues_sub[loc+K_sub])/scl_den;
                        // Computing residue
                        normconsts_or_residues_reserve[K+i] = normconsts_or_residues_reserve[i]/normconsts_or_residues_reserve[K+i];
                    }
                }
            }
            if (ds_type_opt == nsev_dstype_RESIDUES)
                memcpy(normconsts_or_residues,normconsts_or_residues_reserve+K,K* sizeof(COMPLEX));
            else if(ds_type_opt == nsev_dstype_BOTH)
                memcpy(normconsts_or_residues,normconsts_or_residues_reserve,2*K* sizeof(COMPLEX));
            
        }
    }
    release_mem:
        free(q_effective);
        free(r_effective);
        return ret_code;
}
static inline INT fnft_nsev_slow_base(
        const UINT D,
        COMPLEX * const q,
        COMPLEX * const r,
        REAL const * const T,
        const UINT M,
        COMPLEX * const contspec,
        REAL const * const XI,
        UINT * const K_ptr,
        COMPLEX * const bound_states,
        COMPLEX * const normconsts_or_residues,
        const INT kappa,
        fnft_nsev_slow_opts_t *opts)
{
    INT ret_code = SUCCESS;
    UINT i, j, D_scale, D_given, K;
    COMPLEX * scatter_coeffs = NULL;
    COMPLEX * xi = NULL;
    UINT offset = 0;
    REAL phase_factor_rho = NAN, phase_factor_a = NAN, phase_factor_b = NAN;
    REAL bounding_box[4] = { NAN };
    COMPLEX * buffer = NULL;
    COMPLEX * q_tmp = NULL;
    
    // Check inputs
    if (D < 2)
        return E_INVALID_ARGUMENT(D);
    if (q == NULL)
        return E_INVALID_ARGUMENT(q);
    if (T == NULL || T[0] >= T[1])
        return E_INVALID_ARGUMENT(T);
    if (contspec != NULL) {
        if (XI == NULL || XI[0] >= XI[1])
            return E_INVALID_ARGUMENT(XI);
    }
    if (abs(kappa) != 1)
        return E_INVALID_ARGUMENT(kappa);
    if (bound_states != NULL) {
        if (K_ptr == NULL)
            return E_INVALID_ARGUMENT(K_ptr);
    }
    if (opts == NULL)
        opts = &default_opts;
    
    
    // Determine step size
    // D is interpolated number of samples but eps_t is the step-size
    // corresponding to original number of samples.
    D_scale = nse_discretization_D_scale(opts->discretization);
    if (D_scale == 0) {
        ret_code = E_INVALID_ARGUMENT(opts->discretization);
        goto release_mem;
    }
    D_given = D/D_scale;
    const REAL eps_t = (T[1] - T[0])/(D_given - 1);
    
    // Compute the continuous spectrum
    if (contspec != NULL && M > 0) {
        xi = malloc(M * sizeof(COMPLEX));
        scatter_coeffs = malloc(4 * M * sizeof(COMPLEX));
        if (xi == NULL || scatter_coeffs == NULL) {
            ret_code = E_NOMEM;
            goto release_mem;
        }
        
        REAL eps_xi = (XI[1] - XI[0])/(M - 1);
        for (i = 0; i < M; i++)
            xi[i] = XI[0] + eps_xi*i;
        
        
        ret_code = nse_scatter_matrix(D, q, r, eps_t, kappa, M,
                xi, scatter_coeffs, opts->discretization, 0);
        CHECK_RETCODE(ret_code, release_mem);
        
        REAL boundary_coeff;
        boundary_coeff = nse_discretization_boundary_coeff(opts->discretization);
        if (boundary_coeff == NAN){
            ret_code = E_INVALID_ARGUMENT(opts->discretization);
            goto release_mem;
        }
        
        switch (opts->contspec_type) {
            
            case nsev_cstype_BOTH:
                
                offset = M;
                // fall through
                
            case nsev_cstype_REFLECTION_COEFFICIENT:
                
                
                phase_factor_rho =  -2.0*(T[1] + eps_t*boundary_coeff);
                
                for (i = 0; i < M; i++) {
                    if (scatter_coeffs[i*4] == 0.0){
                        return E_DIV_BY_ZERO;
                        goto release_mem;
                    }
                    contspec[i] = scatter_coeffs[i*4 + 2] * CEXP(I*xi[i]*phase_factor_rho) / scatter_coeffs[i*4];
                }
                
                if (opts->contspec_type == nsev_cstype_REFLECTION_COEFFICIENT)
                    break;
                // fall through
                
            case nsev_cstype_AB:
                
                phase_factor_a = (T[1]+eps_t*boundary_coeff) - (T[0]-eps_t*boundary_coeff);
                
                phase_factor_b = - (T[1]+eps_t*boundary_coeff) - (T[0]-eps_t*boundary_coeff);
                
                for (i = 0; i < M; i++) {
                    contspec[offset + i] = scatter_coeffs[i*4] * CEXP(I*xi[i]*phase_factor_a);
                    contspec[offset + M + i] = scatter_coeffs[i*4 + 2] * CEXP(I*xi[i]*phase_factor_b);
                }
                
                break;
                
            default:
                
                ret_code = E_INVALID_ARGUMENT(opts->contspec_type);
                goto release_mem;
        }
    }
    
    // Compute the discrete spectrum
    if (kappa == +1 && bound_states != NULL) {
        
        
        // Compute the bound states
        // Localize bound states ...
        switch (opts->bound_state_localization) {
            
            // ... using Newton's method
            case nsev_bsloc_NEWTON:
                
                K = *K_ptr;
                buffer = bound_states; // Store intermediate results directly
                
                // Perform Newton iterations. Initial guesses of bound-states
                // should be in the continuous-time domain.
                ret_code = refine_roots_newton(D, q, r, T, K, buffer,
                        opts->discretization, opts->niter);
                CHECK_RETCODE(ret_code, release_mem);
                break;
                
            default:
                
                return E_INVALID_ARGUMENT(opts->bound_state_localization);
        }
        
        // Filter bound states
        if (opts->bound_state_filtering != nsev_bsfilt_NONE) {
            
            bounding_box[0] = -INFINITY;
            bounding_box[1] = INFINITY;
            bounding_box[2] = 0.0;
            bounding_box[3] = INFINITY;
            ret_code = misc_filter(&K, buffer, NULL, bounding_box);
            CHECK_RETCODE(ret_code, release_mem);
            
        }
        if (opts->bound_state_filtering == nsev_bsfilt_FULL) {
            bounding_box[1] = re_bound(eps_t);
            bounding_box[0] = -bounding_box[1];
            bounding_box[2] = 0;
            // This step is required as q contains scaled values on a
            // non-equispaced grid 
            if (D_scale == 1){
                bounding_box[3] = im_bound(D_given, q, T);
            } else {
                q_tmp = malloc(D_given * sizeof(COMPLEX));
                if (q_tmp == NULL) {
                    ret_code = E_NOMEM;
                    goto release_mem;
                }
                j = 1;
                for (i = 0; i < D_given; i++) {
                    q_tmp[i] = D_scale*q[j];
                    j = j+D_scale;
                }
                bounding_box[3] = im_bound(D_given, q_tmp, T);
            }
            ret_code = misc_filter(&K, buffer, NULL, bounding_box);
            CHECK_RETCODE(ret_code, release_mem);
        }
        
        ret_code = misc_merge(&K, buffer, SQRT(EPSILON));
        CHECK_RETCODE(ret_code, release_mem);
       
        // Norming constants and/or residues)
        if (normconsts_or_residues != NULL && *K_ptr != 0) {
            
            ret_code = compute_normconsts_or_residues(D, q, r, T, *K_ptr,
                    bound_states, normconsts_or_residues, opts);
            CHECK_RETCODE(ret_code, release_mem);
            
        }
        *K_ptr = K;
    } else if (K_ptr != NULL) {
        *K_ptr = 0;
    }
    release_mem:
        free(scatter_coeffs);
        free(xi);
        free(q_tmp);
        return ret_code;
}

// Auxiliary function: Computes the norming constants and/or residues
// using slow scattering schemes
static inline INT compute_normconsts_or_residues(
        const UINT D,
        COMPLEX const * const q,
        COMPLEX * r,
        REAL const * const T,
        const UINT K,
        COMPLEX * const bound_states,
        COMPLEX * const normconsts_or_residues,
        fnft_nsev_slow_opts_t * const opts)
{
    
    COMPLEX *a_vals = NULL, *aprime_vals = NULL;
    UINT i, offset = 0;
    INT ret_code = SUCCESS;
    // Check inputs
    if (K == 0) // no bound states to refine
        return SUCCESS;
    if (D == 0)
        return E_INVALID_ARGUMENT(D);
    if (q == NULL)
        return E_INVALID_ARGUMENT(q);
    
    a_vals = malloc(K * sizeof(COMPLEX));
    aprime_vals = malloc(K * sizeof(COMPLEX));
    if (a_vals == NULL || aprime_vals == NULL) {
        ret_code = E_NOMEM;
        goto leave_fun;
    }
    
    ret_code = nse_scatter_bound_states(D, q, r, T, K,
            bound_states, a_vals, aprime_vals, normconsts_or_residues, opts->discretization, 0);
    CHECK_RETCODE(ret_code, leave_fun);
    
    // Update to or add residues if requested
    if (opts->discspec_type != nsev_dstype_NORMING_CONSTANTS) {
        
        if (opts->discspec_type == nsev_dstype_RESIDUES) {
            offset = 0;
        } else if (opts->discspec_type == nsev_dstype_BOTH) {
            offset = K;
            memcpy(normconsts_or_residues + offset,
                    normconsts_or_residues,
                    offset*sizeof(complex double));
        } else
            return E_INVALID_ARGUMENT(opts->discspec_type);
        
        // Divide norming constants by derivatives to get residues
        for (i = 0; i < K; i++) {
            if (aprime_vals[i] == 0.0)
                return E_DIV_BY_ZERO;
            normconsts_or_residues[offset + i] /= aprime_vals[i];
        }
    }
    
    leave_fun:
        free(a_vals);
        free(aprime_vals);
        return ret_code;
}


// Auxiliary function for filtering: We assume that bound states must have
// real part in the interval [-re_bound, re_bound].
static inline REAL re_bound(const REAL eps_t)
{
    // -pi/(2*eps_t)<Re(lam)<pi/(2*eps_t).
    // Numerical artefacts often occur close to the border of this
    // region, which is why we filter such bound_states
    return 0.9*PI/FABS(2 * eps_t);
}

// Auxiliary function for filtering: We assume that bound states must have
// imaginary part in the interval [0, im_bound].
static inline REAL im_bound(const UINT D, COMPLEX const * const q,
        REAL const * const T)
{
    // The nonlinear Parseval relation tells us that the squared L2 norm of
    // q(t) is >= 4*(sum of the imaginary parts of the bound states). Thus,
    // any bound state with an imaginary part greater than four times the
    // squared L2 norm of q(t) can be removed. A factor of 1.5 has been
    // added to account for numerical discrepancies when computing the norm
    // numerically (e.g., truncation errors or large step sizes).
    return 1.5 * 0.25 * misc_l2norm2(D, q, T[0], T[1]);
}

// Auxiliary function: Refines the bound-states using Newtons method
static inline INT refine_roots_newton(
        const UINT D,
        COMPLEX const * const q,
        COMPLEX * r,
        REAL const * const T,
        const UINT K,
        COMPLEX * bound_states,
        nse_discretization_t discretization,
        const UINT niter)
{
    INT ret_code = SUCCESS;
    UINT i, iter, D_scale, D_given;
    COMPLEX a_val, b_val, aprime_val, error;
    REAL eprecision = EPSILON * 100;
    REAL re_bound_val, im_bound_val = NAN;
    // Check inputs
    if (K == 0) // no bound states to refine
        return SUCCESS;
    if (niter == 0) // no refinement requested
        return SUCCESS;
    if (bound_states == NULL)
        return E_INVALID_ARGUMENT(bound_states);
    if (q == NULL)
        return E_INVALID_ARGUMENT(q);
    if (T == NULL)
        return E_INVALID_ARGUMENT(T);
    
    D_scale = nse_discretization_D_scale(discretization);
    if (D_scale == 0)
        return E_INVALID_ARGUMENT(discretization);
    
    D_given = D/D_scale;
    const REAL eps_t = (T[1] - T[0])/(D_given - 1);
    
    im_bound_val = D_scale*D_scale*im_bound(D, q, T);
    if (im_bound_val == NAN){
        ret_code = E_OTHER("Upper bound on imaginary part of bound states is NaN");
        CHECK_RETCODE(ret_code, leave_fun);
    }
    re_bound_val = re_bound(eps_t);
    
    // Perform iterations of Newton's method
    for (i = 0; i < K; i++) {
        iter = 0;
        do {
            // Compute a(lam) and a'(lam) at the current root
            ret_code = nse_scatter_bound_states(D, q, r, T, 1,
                    bound_states + i, &a_val, &aprime_val, &b_val, discretization, 1);
            if (ret_code != SUCCESS){
                ret_code = E_SUBROUTINE(ret_code);
                CHECK_RETCODE(ret_code, leave_fun);
            }
            // Perform Newton updates: lam[i] <- lam[i] - a(lam[i])/a'(lam[i])
            if (aprime_val == 0.0)
                return E_DIV_BY_ZERO;
            error = a_val / aprime_val;
            bound_states[i] -= error;
            iter++;
            if (CIMAG(bound_states[i]) > im_bound_val
                    || CREAL(bound_states[i]) > re_bound_val
                    || CREAL(bound_states[i]) < -re_bound_val
                    || CIMAG(bound_states[i]) < 0.0)
                break;
            
        } while (CABS(error) > eprecision && iter < niter);
    }
    
    leave_fun:
        return ret_code;
}

static inline INT signal_effective_from_signal(
        const UINT D, COMPLEX const * const q, REAL const eps_t, const INT kappa,
        UINT * const Dsub_ptr, COMPLEX **q_effective_ptr, COMPLEX **r_effective_ptr,
        UINT * const first_last_index,  nse_discretization_t discretization)
{
    
    UINT i, D_effective, isub;
    INT ret_code = SUCCESS;
    COMPLEX *q_1 = NULL;
    COMPLEX *q_2 = NULL;
    COMPLEX *q_3 = NULL;
    
    COMPLEX *r_1 = NULL;
    COMPLEX *r_2 = NULL;
    COMPLEX *r_3 = NULL;
    
    // Check inputs
    if (D < 2)
        return E_INVALID_ARGUMENT(D);
    if (q == NULL)
        return E_INVALID_ARGUMENT(q);
    if (Dsub_ptr == NULL)
        return E_INVALID_ARGUMENT(Dsub_ptr);
    if (q_effective_ptr == NULL)
        return E_INVALID_ARGUMENT(q_effective_ptr);
    if (r_effective_ptr == NULL)
        return E_INVALID_ARGUMENT(r_effective_ptr);
    if (eps_t <= 0.0)
        return E_INVALID_ARGUMENT(esp_t);
    if (abs(kappa) != 1)
        return E_INVALID_ARGUMENT(kappa);
    if (first_last_index == NULL)
        return E_INVALID_ARGUMENT(first_last_index);
    
    
    
    // Determine number of samples after downsampling, Dsub
    UINT Dsub = *Dsub_ptr; // desired Dsub
    if (Dsub < 2)
        Dsub = 2;
    if (Dsub > D)
        Dsub = D;
    const UINT nskip_per_step = ROUND((REAL)D / Dsub);
    Dsub = ROUND((REAL)D / nskip_per_step); // actual Dsub
    
    UINT D_scale = nse_discretization_D_scale(discretization);
    if (D_scale == 0){
        ret_code =  E_INVALID_ARGUMENT(discretization);
        goto release_mem;
    }
    D_effective = Dsub * D_scale;
    COMPLEX * const q_effective = malloc(D_effective * sizeof(COMPLEX));
    COMPLEX * const r_effective = malloc(D_effective * sizeof(COMPLEX));
    if (q_effective == NULL || r_effective == NULL) {
        ret_code = E_NOMEM;
        goto release_mem;
    }
//
//     printf("%d\n",nskip_per_step);
//      printf("%d\n",D);
//       printf("%d\n",Dsub);
//         printf("%d\n",D_effective);
    switch (discretization) {
        
        case nse_discretization_BO: // Bofetta-Osborne scheme
            i = 0;
            for (isub=0; isub<D_effective; isub++) {
                q_effective[isub] = q[i];
                r_effective[isub] = -kappa*CONJ(q[i]);
                i += nskip_per_step;
            }
            break;
        case nse_discretization_CF4_2:
            q_1 = malloc(D * sizeof(COMPLEX));
            q_2 = malloc(D * sizeof(COMPLEX));
            if (q_1 == NULL || q_2 == NULL) {
                ret_code = E_NOMEM;
                goto release_mem;
            }
            REAL scl_factor = SQRT(3.0)/6.0;
            ret_code = misc_resample(D, eps_t, q, -eps_t*scl_factor*nskip_per_step, q_1);
            CHECK_RETCODE(ret_code, release_mem);
            ret_code = misc_resample(D, eps_t, q, eps_t*scl_factor*nskip_per_step, q_2);
            CHECK_RETCODE(ret_code, release_mem);
            
            
            i = 0;
            for (isub=0; isub<D_effective; isub=isub+2) {
                q_effective[isub] = (q_1[i]+q_2[i])/4.0 - (q_2[i]-q_1[i])*scl_factor;
                q_effective[isub+1] = (q_1[i]+q_2[i])/4.0 + (q_2[i]-q_1[i])*scl_factor;
                r_effective[isub] = -kappa*CONJ(q_effective[isub]);
                r_effective[isub+1] = -kappa*CONJ(q_effective[isub+1]);
                i += nskip_per_step;
            }
            
            
            break;
        case nse_discretization_CF4_3:
            q_1 = malloc(D * sizeof(COMPLEX));
            q_3 = malloc(D * sizeof(COMPLEX));
            if (q_1 == NULL || q_3 == NULL) {
                ret_code = E_NOMEM;
                goto release_mem;
            }
            
            ret_code = misc_resample(D, eps_t, q, -eps_t*SQRT(3.0/20.0)*nskip_per_step, q_1);
            CHECK_RETCODE(ret_code, release_mem);
            ret_code = misc_resample(D, eps_t, q, eps_t*SQRT(3.0/20.0)*nskip_per_step, q_3);
            CHECK_RETCODE(ret_code, release_mem);
            
            
            i = 0;
            for (isub=0; isub<D_effective; isub=isub+3) {
                q_effective[isub] = 0.302556833188024*q_1[i]  -0.033333333333333*q[i] + 0.005776500145310*q_3[i];
                q_effective[isub+1] = -0.030555555555556*q_1[i]+ 0.511111111111111*q[i] -0.030555555555556*q_3[i];
                q_effective[isub+2] = 0.005776500145310*q_1[i] -0.033333333333333*q[i]+  0.302556833188024*q_3[i];
                r_effective[isub] = -kappa*CONJ(q_effective[isub]);
                r_effective[isub+1] = -kappa*CONJ(q_effective[isub+1]);
                r_effective[isub+2] = -kappa*CONJ(q_effective[isub+2]);
                i += nskip_per_step;
            }
            
            break;
        case nse_discretization_CF5_3:
            q_1 = malloc(D * sizeof(COMPLEX));
            q_3 = malloc(D * sizeof(COMPLEX));
            r_1 = malloc(D * sizeof(COMPLEX));
            r_2 = malloc(D * sizeof(COMPLEX));
            r_3 = malloc(D * sizeof(COMPLEX));
            if (q_1 == NULL || q_3 == NULL || r_1 == NULL || r_2 == NULL || r_3 == NULL) {
                ret_code = E_NOMEM;
                goto release_mem;
            }
            
            ret_code = misc_resample(D, eps_t, q, -eps_t*SQRT(15.0)/10.0*nskip_per_step, q_1);
            CHECK_RETCODE(ret_code, release_mem);
            ret_code = misc_resample(D, eps_t, q, eps_t*SQRT(15.0)/10.0*nskip_per_step, q_3);
            CHECK_RETCODE(ret_code, release_mem);
            
            for (i=0; i < D; i++){
                r_1[i] = -kappa*CONJ(q_1[i]);
                r_2[i] = -kappa*CONJ(q[i]);
                r_3[i] = -kappa*CONJ(q_3[i]);
            }
            
            
            i = 0;
            for (isub=0; isub<D_effective; isub=isub+3) {
                q_effective[isub] = (0.320333759788527 + 0.055396500128741*I)*q_1[i]  + (-0.022222222222222 + 0.066666666666667*I)*q[i] + (0.001888462433695 - 0.022063166795408*I)*q_3[i];
                r_effective[isub] = (0.320333759788527 + 0.055396500128741*I)*r_1[i]  + (-0.022222222222222 + 0.066666666666667*I)*r_2[i] + (0.001888462433695 - 0.022063166795408*I)*r_3[i];
                q_effective[isub+1] = (-0.044444444444444 - 0.077459666924148*I)*q_1[i]+ (0.488888888888889)*q[i] + (-0.044444444444444 + 0.077459666924148*I)*q_3[i];
                r_effective[isub+1] = (-0.044444444444444 - 0.077459666924148*I)*r_1[i]+ (0.488888888888889)*r_2[i] + (-0.044444444444444 + 0.077459666924148*I)*r_3[i];
                q_effective[isub+2] = ( 0.001888462433695 + 0.022063166795408*I)*q_1[i] + (-0.022222222222222 - 0.066666666666667*I)*q[i] +  (0.320333759788527 - 0.055396500128741*I)*q_3[i];
                r_effective[isub+2] = ( 0.001888462433695 + 0.022063166795408*I)*r_1[i] + (-0.022222222222222 - 0.066666666666667*I)*r_2[i] +  (0.320333759788527 - 0.055396500128741*I)*r_3[i];
                i += nskip_per_step;
            }
            
            break;
        case nse_discretization_CF6_4:
            q_1 = malloc(D * sizeof(COMPLEX));
            q_3 = malloc(D * sizeof(COMPLEX));
            r_1 = malloc(D * sizeof(COMPLEX));
            r_2 = malloc(D * sizeof(COMPLEX));
            r_3 = malloc(D * sizeof(COMPLEX));
            if (q_1 == NULL || q_3 == NULL || r_1 == NULL || r_2 == NULL || r_3 == NULL) {
                ret_code = E_NOMEM;
                goto release_mem;
            }
            
            ret_code = misc_resample(D, eps_t, q, -eps_t*nskip_per_step*SQRT(15.0)/10.0, q_1);
            CHECK_RETCODE(ret_code, release_mem);
            ret_code = misc_resample(D, eps_t, q, eps_t*nskip_per_step*SQRT(15.0)/10.0, q_3);
            CHECK_RETCODE(ret_code, release_mem);
            
            for (i=0; i < D; i++){
                r_1[i] = -kappa*CONJ(q_1[i]);
                r_2[i] = -kappa*CONJ(q[i]);
                r_3[i] = -kappa*CONJ(q_3[i]);
            }
            
            
            i = 0;
            for (isub=0; isub<D_effective; isub=isub+4) {
                q_effective[isub] = (0.245985577298764 + 0.038734389227165*I)*q_1[i]  + (-0.046806149832549 + 0.012442141491185*I)*q[i] + (0.010894359342569 - 0.004575808769067*I)*q_3[i];
                r_effective[isub] = (0.245985577298764 + 0.038734389227165*I)*r_1[i]  + (-0.046806149832549 + 0.012442141491185*I)*r_2[i] + (0.010894359342569 - 0.004575808769067*I)*r_3[i];
                q_effective[isub+1] = (0.062868370946917 - 0.048761268117765*I)*q_1[i]+ (0.269028372054771 - 0.012442141491185*I)*q[i] + (-0.041970529810473 + 0.014602687659668*I)*q_3[i];
                r_effective[isub+1] = (0.062868370946917 - 0.048761268117765*I)*r_1[i]+ (0.269028372054771 - 0.012442141491185*I)*r_2[i] + (-0.041970529810473 + 0.014602687659668*I)*r_3[i];
                q_effective[isub+2] = (-0.041970529810473 + 0.014602687659668*I)*q_1[i] + (0.269028372054771 - 0.012442141491185*I)*q[i] +  (0.062868370946917 - 0.048761268117765*I)*q_3[i];
                r_effective[isub+2] = (-0.041970529810473 + 0.014602687659668*I)*r_1[i] + (0.269028372054771 - 0.012442141491185*I)*r_2[i] +  (0.062868370946917 - 0.048761268117765*I)*r_3[i];
                q_effective[isub+3] = (0.010894359342569 - 0.004575808769067*I)*q_1[i] + (-0.046806149832549 + 0.012442141491185*I)*q[i] +  (0.245985577298764 + 0.038734389227165*I)*q_3[i];
                r_effective[isub+3] = (0.010894359342569 - 0.004575808769067*I)*r_1[i] + (-0.046806149832549 + 0.012442141491185*I)*r_2[i] +  (0.245985577298764 + 0.038734389227165*I)*r_3[i];
                i += nskip_per_step;
            }
            break;
        case nse_discretization_ES4:
        case nse_discretization_TES4:
            i = 0;
            for (isub=0; isub<D_effective; isub=isub+3) {
                q_effective[isub] = q[i];
                i += nskip_per_step;
            }
            
            REAL eps_t_sub = eps_t*nskip_per_step;
            REAL eps_t_sub_2 = POW(eps_t_sub,2);        
            q_effective[1] = (q_effective[3]-q_effective[0])/eps_t_sub; 
            q_effective[2] = (q_effective[6]-2*q_effective[3]+q_effective[0])/eps_t_sub_2;
            q_effective[D_effective-2] = (q_effective[D_effective-3]-q_effective[D_effective-6])/eps_t_sub; 
            q_effective[D_effective-1] = (q_effective[D_effective-3]-2*q_effective[D_effective-6]+q_effective[D_effective-9])/eps_t_sub_2;
//             q_effective[1] = (q_effective[3]-0)/(2*eps_t_sub); 
//             q_effective[2] = (q_effective[3]-2*q_effective[0]+0)/eps_t_sub_2;
//             q_effective[D_effective-2] = (0-q_effective[D_effective-6])/(2*eps_t_sub); 
//             q_effective[D_effective-1] = (0-2*q_effective[D_effective-3]+q_effective[D_effective-6])/eps_t_sub_2;
            

            for (isub=3; isub<D_effective-3; isub=isub+3) {
                q_effective[isub+1] = (q_effective[isub+3]-q_effective[isub-3])/(2*eps_t_sub); 
                q_effective[isub+2] = (q_effective[isub+3]-2*q_effective[isub]+q_effective[isub-3])/eps_t_sub_2;
            }

            for (i=0; i<D_effective; i++) {
                r_effective[i] = -kappa*CONJ(q_effective[i]);
            }
            break;
        default: // Unknown discretization
            
            ret_code = E_INVALID_ARGUMENT(discretization);
            goto  release_mem;
    }
    
    // Original index of the first and last sample in qsub
    first_last_index[0] = 0;
    first_last_index[1] = (Dsub-1)*nskip_per_step;
    *q_effective_ptr = q_effective;
    *r_effective_ptr = r_effective;
    *Dsub_ptr = Dsub;
    
    release_mem:
        free(q_1);
        free(q_2);
        free(q_3);
        free(r_1);
        free(r_2);
        free(r_3);
        return ret_code;
}
