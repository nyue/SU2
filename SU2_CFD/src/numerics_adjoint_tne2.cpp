/*!
 * \file numerics_adjoint_mean.cpp
 * \brief This file contains all the convective term discretization.
 * \author Aerospace Design Laboratory (Stanford University) <http://su2.stanford.edu>.
 * \version 3.0.0 "eagle"
 *
 * SU2, Copyright (C) 2012-2014 Aerospace Design Laboratory (ADL).
 *
 * SU2 is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * SU2 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with SU2. If not, see <http://www.gnu.org/licenses/>.
 */

#include "../include/numerics_structure.hpp"
#include <limits>

CUpwRoe_AdjTNE2::CUpwRoe_AdjTNE2(unsigned short val_nDim,
                                 unsigned short val_nVar,
                                 unsigned short val_nPrimVar,
                                 unsigned short val_nPrimVarGrad,
                                 CConfig *config) : CNumerics(val_nDim,
                                                              val_nVar,
                                                              config) {
  
  /*--- Read configuration parameters ---*/
	implicit   = (config->GetKind_TimeIntScheme_AdjTNE2() == EULER_IMPLICIT);
  
  /*--- Define useful constants ---*/
  nVar         = val_nVar;
  nPrimVar     = val_nPrimVar;
  nPrimVarGrad = val_nPrimVarGrad;
  nDim         = val_nDim;
  nSpecies     = config->GetnSpecies();
  
  UnitNormal = new double[nDim];
  MeanU      = new double[nVar];
  MeanV      = new double[nPrimVar];
  MeandPdU   = new double[nVar];
  DiffPsi    = new double[nVar];
  Lambda     = new double[nVar];
  Ai     = new double* [nVar];
  Aj     = new double* [nVar];
  P      = new double* [nVar];
  invP   = new double* [nVar];
  PLPinv = new double* [nVar];
  for (unsigned short iVar = 0; iVar < nVar; iVar++) {
    Ai[iVar]     = new double [nVar];
    Aj[iVar]     = new double [nVar];
    P[iVar]      = new double [nVar];
    invP[iVar]   = new double [nVar];
    PLPinv[iVar] = new double [nVar];
  }  
}

CUpwRoe_AdjTNE2::~CUpwRoe_AdjTNE2(void) {
  
  delete [] UnitNormal;
  delete [] MeanU;
  delete [] MeanV;
  delete [] MeandPdU;
  delete [] DiffPsi;
	delete [] Lambda;
  for (unsigned short iVar = 0; iVar < nVar; iVar++) {
    delete [] Ai[iVar];
    delete [] Aj[iVar];
    delete [] P[iVar];
    delete [] invP[iVar];
    delete [] PLPinv[iVar];
  }
  delete [] Ai;
  delete [] Aj;
  delete [] P;
  delete [] invP;
  delete [] PLPinv;
  
}

void CUpwRoe_AdjTNE2::ComputeResidual (double *val_residual_i,
                                       double *val_residual_j,
                                       double **val_Jacobian_ii,
                                       double **val_Jacobian_ij,
                                       double **val_Jacobian_ji,
                                       double **val_Jacobian_jj,
                                       CConfig *config) {
  
  unsigned short iDim, iVar, jVar, kVar;
  double Area, ProjVel;
  double MeanSoundSpeed;
  
  /*--- Roe flux: Fij = (Fi + Fj)/2 - 1/2*P|Lam|P^-1 * (Uj - Ui) ---*/
  // Notes:
  // 1) Non-conservative method, so for Fij -> Fi = A_i*Psi_i & Fj = A_i*Psi_j
  //    and Fji -> Fi = A_j*Psi_i & Fj = A_j*Psi_j
  // 2) Linear upwinding, so eigenvalue & eigenvector decomposition can be
  //    calculated using interface ij state (mean variables)
  
  /*--- Initialize the residuals (and Jacobians) ---*/
  for (iVar = 0; iVar < nVar; iVar++) {
    val_residual_i[iVar] = 0.0;
    val_residual_j[iVar] = 0.0;
  }
  if (implicit) {
    for (iVar = 0; iVar < nVar; iVar++)
      for (jVar = 0; jVar < nVar; jVar++) {
        val_Jacobian_ii[iVar][jVar] = 0.0;
        val_Jacobian_ij[iVar][jVar] = 0.0;
        val_Jacobian_ji[iVar][jVar] = 0.0;
        val_Jacobian_jj[iVar][jVar] = 0.0;
      }
  }
  
  /*--- Calculate geometric quantities ---*/
  Area = 0.0;
  for (iDim = 0; iDim < nDim; iDim++)
    Area += Normal[iDim]*Normal[iDim];
  Area = sqrt(Area);
  for (iDim = 0; iDim < nDim; iDim++)
    UnitNormal[iDim] = Normal[iDim]/Area;
  
  /*--- Calculate inviscid projected Jacobians ---*/
  GetInviscidProjJac(U_i, V_i, dPdU_i, Normal, 1.0, Ai);
  GetInviscidProjJac(U_j, V_j, dPdU_j, Normal, 1.0, Aj);
  
  /*--- Inviscid portion of the flux, A^T*Psi ---*/
  for (iVar = 0; iVar < nVar; iVar++) {
    for (jVar = 0; jVar < nVar; jVar++) {
      val_residual_i[iVar] +=  0.5*Ai[jVar][iVar]*(Psi_i[jVar]+Psi_j[jVar]);
      val_residual_j[iVar] += -0.5*Aj[jVar][iVar]*(Psi_i[jVar]+Psi_j[jVar]);
    }
  }
  
  /*--- Calculate mean variables ---*/
  for (iVar = 0; iVar < nVar; iVar++)
    MeanU[iVar] = 0.5*(U_i[iVar]+U_j[iVar]);
  for (iVar = 0; iVar < nPrimVar; iVar++)
    MeanV[iVar] = 0.5*(V_i[iVar]+V_j[iVar]);
  var->CalcdPdU(MeanV, config, MeandPdU);
  MeanSoundSpeed = sqrt((1.0+MeandPdU[nSpecies+nDim]) *
                        MeanV[P_INDEX]/MeanV[RHO_INDEX]);
  
  for (iVar = 0; iVar < nVar; iVar++)
    DiffPsi[iVar] = Psi_j[iVar] - Psi_i[iVar];
  
  ProjVel = 0.0;
  for (iDim = 0; iDim < nDim; iDim++)
    ProjVel += MeanV[VEL_INDEX+iDim]*UnitNormal[iDim];
  
  /*--- Calculate eigenvalues of the interface state, ij ---*/
  for (iVar = 0; iVar < nSpecies+nDim-1; iVar++)
    Lambda[iVar] = ProjVel;
  Lambda[nSpecies+nDim-1] = ProjVel + MeanSoundSpeed;
  Lambda[nSpecies+nDim]   = ProjVel - MeanSoundSpeed;
  Lambda[nSpecies+nDim+1] = ProjVel;
  for (iVar = 0; iVar < nVar; iVar++)
    Lambda[iVar] = fabs(Lambda[iVar]);
  
  /*--- Calculate left and right eigenvector matrices ---*/
  CreateBasis(UnitNormal);
  GetPMatrix(MeanU, MeanV, MeandPdU, UnitNormal, l, m, P);
  GetPMatrix_inv(MeanU, MeanV, MeandPdU, UnitNormal, l, m, invP);
  
  /*--- Calculate eigenvalue/eigenvector decomposition ---*/
  // |PLPinv| = P x |Lambda| x inverse P
  for (iVar = 0; iVar < nVar; iVar++) {
    for (jVar = 0; jVar < nVar; jVar++) {
      PLPinv[iVar][jVar] = 0.0;
      for (kVar = 0; kVar < nVar; kVar++)
        PLPinv[iVar][jVar] += P[iVar][kVar]*Lambda[kVar]*invP[kVar][jVar];
    }
  }
  
  /*--- Calculate the 'viscous' portion of the flux ---*/
  // 1/2*(P|Lam|P^-1)^T * (Uj - Ui)
  for (iVar = 0; iVar < nVar; iVar++) {
    for (jVar = 0; jVar < nVar; jVar++) {
      val_residual_i[iVar] -= 0.5*PLPinv[jVar][iVar]*(Psi_i[jVar]-Psi_j[jVar])*Area;
      val_residual_j[iVar] += 0.5*PLPinv[jVar][iVar]*(Psi_i[jVar]-Psi_j[jVar])*Area;
    }
  }
  
  /*--- Populate Jacobian matrices ---*/
  // Note: Ai/j calculated using 'Normal', but PLPinv computed using UnitNormal.
  //       Only multiply PLP by area to properly account for integration.
  if (implicit) {
    for (iVar = 0; iVar < nVar; iVar++) {
      for (jVar = 0; jVar < nVar; jVar++) {
        val_Jacobian_ii[iVar][jVar] = 0.5*Ai[jVar][iVar] - 0.5*PLPinv[jVar][iVar]*Area;
        val_Jacobian_ij[iVar][jVar] = 0.5*Ai[jVar][iVar] + 0.5*PLPinv[jVar][iVar]*Area;
        val_Jacobian_ji[iVar][jVar] = -0.5*Aj[jVar][iVar] + 0.5*PLPinv[jVar][iVar]*Area;
        val_Jacobian_jj[iVar][jVar] = -0.5*Aj[jVar][iVar] - 0.5*PLPinv[jVar][iVar]*Area;
      }
    }
  }
}

CUpwSW_AdjTNE2::CUpwSW_AdjTNE2(unsigned short val_nDim,
                                 unsigned short val_nVar,
                                 unsigned short val_nPrimVar,
                                 unsigned short val_nPrimVarGrad,
                                 CConfig *config) : CNumerics(val_nDim,
                                                              val_nVar,
                                                              config) {
  
  /*--- Read configuration parameters ---*/
	implicit   = (config->GetKind_TimeIntScheme_AdjTNE2() == EULER_IMPLICIT);
  
  /*--- Define useful constants ---*/
  nVar         = val_nVar;
  nPrimVar     = val_nPrimVar;
  nPrimVarGrad = val_nPrimVarGrad;
  nDim         = val_nDim;
  nSpecies     = config->GetnSpecies();
  
  UnitNormal = new double[nDim];
  DiffPsi    = new double[nVar];
  Lambda_i   = new double[nVar];
  Lambda_j   = new double[nVar];
  Ai     = new double* [nVar];
  Aj     = new double* [nVar];
  P      = new double* [nVar];
  invP   = new double* [nVar];
  PLPinv = new double* [nVar];
  for (unsigned short iVar = 0; iVar < nVar; iVar++) {
    Ai[iVar]     = new double [nVar];
    Aj[iVar]     = new double [nVar];
    P[iVar]      = new double [nVar];
    invP[iVar]   = new double [nVar];
    PLPinv[iVar] = new double [nVar];
  }
  
}

CUpwSW_AdjTNE2::~CUpwSW_AdjTNE2(void) {
  
  delete [] UnitNormal;
  delete [] DiffPsi;
	delete [] Lambda_i;
  delete [] Lambda_j;
  for (unsigned short iVar = 0; iVar < nVar; iVar++) {
    delete [] Ai[iVar];
    delete [] Aj[iVar];
    delete [] P[iVar];
    delete [] invP[iVar];
    delete [] PLPinv[iVar];
  }
  delete [] P;
  delete [] invP;
  delete [] PLPinv;
}

void CUpwSW_AdjTNE2::ComputeResidual (double *val_residual_i,
                                      double *val_residual_j,
                                      double **val_Jacobian_ii,
                                      double **val_Jacobian_ij,
                                      double **val_Jacobian_ji,
                                      double **val_Jacobian_jj,
                                      CConfig *config) {
  
  unsigned short iDim, iVar, jVar, kVar;
  double Area, ProjVel_i, ProjVel_j;
  
  /*--- Roe flux: Fij = (Fi + Fj)/2 - 1/2*P|Lam|P^-1 * (Uj - Ui) ---*/
  // Notes:
  // 1) Non-conservative method, so for Fij -> Fi = A_i*Psi_i & Fj = A_i*Psi_j
  //    and Fji -> Fi = A_j*Psi_i & Fj = A_j*Psi_j
  // 2) Linear upwinding, so eigenvalue & eigenvector decomposition can be
  //    calculated using interface ij state (mean variables)
  
  /*--- Initialize the residuals (and Jacobians) ---*/
  for (iVar = 0; iVar < nVar; iVar++) {
    val_residual_i[iVar] = 0.0;
    val_residual_j[iVar] = 0.0;
  }
  if (implicit) {
    for (iVar = 0; iVar < nVar; iVar++)
      for (jVar = 0; jVar < nVar; jVar++) {
        val_Jacobian_ii[iVar][jVar] = 0.0;
        val_Jacobian_ij[iVar][jVar] = 0.0;
        val_Jacobian_ji[iVar][jVar] = 0.0;
        val_Jacobian_jj[iVar][jVar] = 0.0;
      }
  }
  
  /*--- Calculate geometric quantities ---*/
  Area = 0.0;
  for (iDim = 0; iDim < nDim; iDim++)
    Area += Normal[iDim]*Normal[iDim];
  Area = sqrt(Area);
  for (iDim = 0; iDim < nDim; iDim++)
    UnitNormal[iDim] = Normal[iDim]/Area;
  CreateBasis(UnitNormal);
  
  /*--- Calculate inviscid projected Jacobians ---*/
  GetInviscidProjJac(U_i, V_i, dPdU_i, Normal, 1.0, Ai);
  GetInviscidProjJac(U_j, V_j, dPdU_j, Normal, 1.0, Aj);
  
  /*--- Inviscid portion of the flux, A^T*Psi ---*/
  for (iVar = 0; iVar < nVar; iVar++) {
    for (jVar = 0; jVar < nVar; jVar++) {
      val_residual_i[iVar] +=  0.5*Ai[jVar][iVar]*(Psi_i[jVar]+Psi_j[jVar]);
      val_residual_j[iVar] += -0.5*Aj[jVar][iVar]*(Psi_i[jVar]+Psi_j[jVar]);
    }
  }
  
  for (iVar = 0; iVar < nVar; iVar++)
    DiffPsi[iVar] = Psi_j[iVar] - Psi_i[iVar];
  
  ProjVel_i = 0.0;
  ProjVel_j = 0.0;
  for (iDim = 0; iDim < nDim; iDim++) {
    ProjVel_i += V_i[VEL_INDEX+iDim]*UnitNormal[iDim];
    ProjVel_j += V_j[VEL_INDEX+iDim]*UnitNormal[iDim];
  }
  
  /*--- Calculate eigenvalues of the i & j states ---*/
  for (iVar = 0; iVar < nSpecies+nDim-1; iVar++) {
    Lambda_i[iVar] = ProjVel_i;
    Lambda_j[iVar] = ProjVel_j;
  }
  Lambda_i[nSpecies+nDim-1] = ProjVel_i + V_i[A_INDEX];
  Lambda_j[nSpecies+nDim-1] = ProjVel_j + V_j[A_INDEX];
  Lambda_i[nSpecies+nDim]   = ProjVel_i - V_i[A_INDEX];
  Lambda_j[nSpecies+nDim]   = ProjVel_j - V_j[A_INDEX];
  Lambda_i[nSpecies+nDim+1] = ProjVel_i;
  Lambda_j[nSpecies+nDim+1] = ProjVel_j;
  for (iVar = 0; iVar < nVar; iVar++) {
    Lambda_i[iVar] = fabs(Lambda_i[iVar]);
    Lambda_j[iVar] = fabs(Lambda_j[iVar]);
  }
  
  /*--- Calculate left and right eigenvector matrices ---*/
  GetPMatrix(U_i, V_i, dPdU_i, UnitNormal, l, m, P);
  GetPMatrix_inv(U_i, V_i, dPdU_i, UnitNormal, l, m, invP);
  
  /*--- Calculate eigenvalue/eigenvector decomposition for i ---*/
  // |PLPinv| = P x |Lambda| x inverse P
  for (iVar = 0; iVar < nVar; iVar++) {
    for (jVar = 0; jVar < nVar; jVar++) {
      PLPinv[iVar][jVar] = 0.0;
      for (kVar = 0; kVar < nVar; kVar++)
        PLPinv[iVar][jVar] += P[iVar][kVar]*Lambda_i[kVar]*invP[kVar][jVar];
    }
  }
  
  /*--- Calculate the 'viscous' portion of the flux at i ---*/
  // 1/2*(P|Lam|P^-1)^T * (Uj - Ui)
  for (iVar = 0; iVar < nVar; iVar++)
    for (jVar = 0; jVar < nVar; jVar++)
      val_residual_i[iVar] -= 0.5*PLPinv[jVar][iVar]*(Psi_i[jVar]-Psi_j[jVar])*Area;
  
  /*--- Populate Jacobian matrices ---*/
  // Note: Ai/j calculated using 'Normal', but PLPinv computed using UnitNormal.
  //       Only multiply PLP by area to properly account for integration.
  if (implicit) {
    for (iVar = 0; iVar < nVar; iVar++) {
      for (jVar = 0; jVar < nVar; jVar++) {
        val_Jacobian_ii[iVar][jVar] = 0.5*Ai[jVar][iVar] - 0.5*PLPinv[jVar][iVar]*Area;
        val_Jacobian_ij[iVar][jVar] = 0.5*Ai[jVar][iVar] + 0.5*PLPinv[jVar][iVar]*Area;
      }
    }
  }
  
  /*--- Calculate left and right eigenvector matrices for j ---*/
  GetPMatrix(U_j, V_j, dPdU_j, UnitNormal, l, m, P);
  GetPMatrix_inv(U_j, V_j, dPdU_j, UnitNormal, l, m, invP);
  
  /*--- Calculate eigenvalue/eigenvector decomposition for i ---*/
  // |PLPinv| = P x |Lambda| x inverse P
  for (iVar = 0; iVar < nVar; iVar++) {
    for (jVar = 0; jVar < nVar; jVar++) {
      PLPinv[iVar][jVar] = 0.0;
      for (kVar = 0; kVar < nVar; kVar++)
        PLPinv[iVar][jVar] += P[iVar][kVar]*Lambda_j[kVar]*invP[kVar][jVar];
    }
  }
  
  /*--- Calculate the 'viscous' portion of the flux at j ---*/
  // 1/2*(P|Lam|P^-1)^T * (Uj - Ui)
  for (iVar = 0; iVar < nVar; iVar++)
    for (jVar = 0; jVar < nVar; jVar++)
      val_residual_j[iVar] += 0.5*PLPinv[jVar][iVar]*(Psi_i[jVar]-Psi_j[jVar])*Area;
  
  /*--- Populate Jacobian matrices ---*/
  // Note: Ai/j calculated using 'Normal', but PLPinv computed using UnitNormal.
  //       Only multiply PLP by area to properly account for integration.
  if (implicit) {
    for (iVar = 0; iVar < nVar; iVar++) {
      for (jVar = 0; jVar < nVar; jVar++) {
        val_Jacobian_ji[iVar][jVar] = -0.5*Aj[jVar][iVar] + 0.5*PLPinv[jVar][iVar]*Area;
        val_Jacobian_jj[iVar][jVar] = -0.5*Aj[jVar][iVar] - 0.5*PLPinv[jVar][iVar]*Area;
      }
    }
  }
}


CCentJST_AdjTNE2::CCentJST_AdjTNE2(unsigned short val_nDim, unsigned short val_nVar,
                                   CConfig *config) : CNumerics(val_nDim, val_nVar, config) {
  
	Gamma = config->GetGamma();
	Gamma_Minus_One = Gamma - 1.0;
  
	grid_movement = config->GetGrid_Movement();
	rotating_frame = config->GetRotating_Frame();
  
	Diff_Psi = new double [nVar]; Diff_Lapl = new double [nVar];
	Und_Lapl_i = new double [nVar]; Und_Lapl_j = new double [nVar];
	Velocity_i = new double [nDim]; Velocity_j = new double [nDim];
	MeanPhi = new double [nDim];
  
	Param_p = 0.3;
	Param_Kappa_2 = config->GetKappa_2nd_AdjTNE2();
	Param_Kappa_4 = config->GetKappa_4th_AdjTNE2();
	implicit = (config->GetKind_TimeIntScheme_AdjTNE2() == EULER_IMPLICIT);
}

CCentJST_AdjTNE2::~CCentJST_AdjTNE2(void) {
  
	delete [] Diff_Psi; delete [] Diff_Lapl;
	delete [] Und_Lapl_i; delete [] Und_Lapl_j;
	delete [] Velocity_i; delete [] Velocity_j;
	delete [] MeanPhi;
}

void CCentJST_AdjTNE2::ComputeResidual (double *val_resconv_i, double *val_resvisc_i,
                                        double *val_resconv_j, double *val_resvisc_j,
                                        double **val_Jacobian_ii, double **val_Jacobian_ij,
                                        double **val_Jacobian_ji, double **val_Jacobian_jj,
                                        CConfig *config) {
  
	/*--- Mean Values ---*/
	MeanPsiRho =  0.5*(Psi_i[0]+Psi_j[0]);
	for (iDim = 0; iDim < nDim; iDim++)
		MeanPhi[iDim] =  0.5*(Psi_i[iDim+1]+Psi_j[iDim+1]);
	MeanPsiE =  0.5*(Psi_i[nVar-1]+Psi_j[nVar-1]);
  
	/*--- Point i convective residual evaluation ---*/
	ProjVelocity_i = 0; ProjPhi = 0; ProjPhi_Vel = 0; sq_vel = 0; Area = 0;
	for (iDim = 0; iDim < nDim; iDim++) {
		Velocity_i[iDim] = U_i[iDim+1] / U_i[0];
		ProjVelocity_i += Velocity_i[iDim]*Normal[iDim];
		ProjPhi += MeanPhi[iDim]*Normal[iDim];
		ProjPhi_Vel += MeanPhi[iDim]*Velocity_i[iDim];
		sq_vel += 0.5*Velocity_i[iDim]*Velocity_i[iDim];
		Area += Normal[iDim]*Normal[iDim];
	}
	Area = sqrt(Area);
	phis1 = ProjPhi + ProjVelocity_i*MeanPsiE;
	phis2 = MeanPsiRho + ProjPhi_Vel + Enthalpy_i*MeanPsiE;
  
	val_resconv_i[0] = ProjVelocity_i*MeanPsiRho - phis2*ProjVelocity_i + Gamma_Minus_One*phis1*sq_vel;
	for (iDim = 0; iDim < nDim; iDim++)
		val_resconv_i[iDim+1] = ProjVelocity_i*MeanPhi[iDim] + phis2*Normal[iDim] - Gamma_Minus_One*phis1*Velocity_i[iDim];
	val_resconv_i[nVar-1] = ProjVelocity_i*MeanPsiE + Gamma_Minus_One*phis1;
  
	/*--- Flux contributions due to grid movement at point i (TDE) ---*/
	if (grid_movement) {
		double ProjGridVel = 0.0;
		for (iDim = 0; iDim < nDim; iDim++)
			ProjGridVel += 0.5*(GridVel_i[iDim]+GridVel_j[iDim])*Normal[iDim];
		val_resconv_i[0] -= ProjGridVel*MeanPsiRho;
		for (iDim = 0; iDim < nDim; iDim++)
			val_resconv_i[iDim+1] -= ProjGridVel*MeanPhi[iDim];
		val_resconv_i[nVar-1] -= ProjGridVel*MeanPsiE;
	}
  
	/*--- Jacobians of the inviscid flux ---*/
	if (implicit) {
		val_Jacobian_ii[0][0] = 0.0;
		for (jDim = 0; jDim < nDim; jDim++)
			val_Jacobian_ii[0][jDim+1] = -0.5*ProjVelocity_i*Velocity_i[jDim] + Gamma_Minus_One*sq_vel*0.5*Normal[jDim];
		val_Jacobian_ii[0][nVar-1] = 0.5*ProjVelocity_i*(Gamma_Minus_One*sq_vel - Enthalpy_i);
		for (iDim = 0; iDim < nDim; iDim++) {
			val_Jacobian_ii[iDim+1][0] = 0.5*Normal[iDim];
			for (jDim = 0; jDim < nDim; jDim++)
				val_Jacobian_ii[iDim+1][jDim+1] = 0.5*Normal[iDim]*Velocity_i[jDim] - 0.5*Gamma_Minus_One*Velocity_i[iDim]*Normal[jDim];
			val_Jacobian_ii[iDim+1][iDim+1] += 0.5*ProjVelocity_i;
			val_Jacobian_ii[iDim+1][nVar-1] = 0.5*Enthalpy_i*Normal[iDim] - 0.5*Gamma_Minus_One*Velocity_i[iDim]*ProjVelocity_i;
		}
		val_Jacobian_ii[nVar-1][0] = 0;
		for (jDim = 0; jDim < nDim; jDim++)
			val_Jacobian_ii[nVar-1][jDim+1] = 0.5*Gamma_Minus_One*Normal[jDim];
		val_Jacobian_ii[nVar-1][nVar-1] = 0.5*Gamma*ProjVelocity_i;
    
		for (iVar = 0; iVar < nVar; iVar++)
			for (jVar = 0; jVar < nVar; jVar++)
				val_Jacobian_ij[iVar][jVar] = val_Jacobian_ii[iVar][jVar];
    
		/*--- Jacobian contributions due to grid movement at point i (TDE) ---*/
		if (grid_movement) {
			double ProjGridVel = 0.0;
			for (iDim = 0; iDim < nDim; iDim++)
				ProjGridVel += 0.5*(GridVel_i[iDim]+GridVel_j[iDim])*Normal[iDim];
			for (iVar = 0; iVar < nVar; iVar++) {
				val_Jacobian_ii[iVar][iVar] -= 0.5*ProjGridVel;
				val_Jacobian_ij[iVar][iVar] -= 0.5*ProjGridVel;
			}
		}
	}
  
  
	/*--- Point j convective residual evaluation ---*/
	ProjVelocity_j = 0; ProjPhi_Vel = 0; sq_vel = 0;
	for (iDim = 0; iDim < nDim; iDim++) {
		Velocity_j[iDim] = U_j[iDim+1] / U_j[0];
		ProjVelocity_j += Velocity_j[iDim]*Normal[iDim];
		ProjPhi_Vel += MeanPhi[iDim]*Velocity_j[iDim];
		sq_vel += 0.5*Velocity_j[iDim]*Velocity_j[iDim];
	}
  
	phis1 = ProjPhi + ProjVelocity_j*MeanPsiE;
	phis2 = MeanPsiRho + ProjPhi_Vel + Enthalpy_j*MeanPsiE;
  
	val_resconv_j[0] = -(ProjVelocity_j*MeanPsiRho - phis2*ProjVelocity_j + Gamma_Minus_One*phis1*sq_vel);
	for (iDim = 0; iDim < nDim; iDim++)
		val_resconv_j[iDim+1] = -(ProjVelocity_j*MeanPhi[iDim] + phis2*Normal[iDim] - Gamma_Minus_One*phis1*Velocity_j[iDim]);
	val_resconv_j[nVar-1] = -(ProjVelocity_j*MeanPsiE + Gamma_Minus_One*phis1);
  
	/*--- Flux contributions due to grid movement at point j (TDE) ---*/
	if (grid_movement) {
		double ProjGridVel = 0.0;
		for (iDim = 0; iDim < nDim; iDim++)
			ProjGridVel += 0.5*(GridVel_i[iDim]+GridVel_j[iDim])*Normal[iDim];
		val_resconv_j[0] += ProjGridVel*MeanPsiRho;
		for (iDim = 0; iDim < nDim; iDim++)
			val_resconv_j[iDim+1] += ProjGridVel*MeanPhi[iDim];
		val_resconv_j[nVar-1] += ProjGridVel*MeanPsiE;
	}
  
	/*--- Jacobians of the inviscid flux ---*/
	if (implicit) {
		val_Jacobian_jj[0][0] = 0.0;
		for (jDim = 0; jDim < nDim; jDim++)
			val_Jacobian_jj[0][jDim+1] = 0.5*ProjVelocity_j*Velocity_j[jDim] - Gamma_Minus_One*sq_vel*0.5*Normal[jDim];
		val_Jacobian_jj[0][nVar-1] = -0.5*ProjVelocity_j*(Gamma_Minus_One*sq_vel - Enthalpy_j);
		for (iDim = 0; iDim < nDim; iDim++) {
			val_Jacobian_jj[iDim+1][0] = -0.5*Normal[iDim];
			for (jDim = 0; jDim < nDim; jDim++)
				val_Jacobian_jj[iDim+1][jDim+1] = -0.5*Normal[iDim]*Velocity_j[jDim] + 0.5*Gamma_Minus_One*Velocity_j[iDim]*Normal[jDim];
			val_Jacobian_jj[iDim+1][iDim+1] -= 0.5*ProjVelocity_j;
			val_Jacobian_jj[iDim+1][nVar-1] = -0.5*Enthalpy_j*Normal[iDim] + 0.5*Gamma_Minus_One*Velocity_j[iDim]*ProjVelocity_j;
		}
		val_Jacobian_jj[nVar-1][0] = 0;
		for (jDim = 0; jDim < nDim; jDim++)
			val_Jacobian_jj[nVar-1][jDim+1] = -0.5*Gamma_Minus_One*Normal[jDim];
		val_Jacobian_jj[nVar-1][nVar-1] = -0.5*Gamma*ProjVelocity_j;
    
		for (iVar = 0; iVar < nVar; iVar++)
			for (jVar = 0; jVar < nVar; jVar++)
				val_Jacobian_ji[iVar][jVar] = val_Jacobian_jj[iVar][jVar];
    
		/*--- Jacobian contributions due to grid movement at point j (TDE) ---*/
		if (grid_movement) {
			double ProjGridVel = 0.0;
			for (iDim = 0; iDim < nDim; iDim++)
				ProjGridVel += 0.5*(GridVel_i[iDim]+GridVel_j[iDim])*Normal[iDim];
			for (iVar = 0; iVar < nVar; iVar++) {
				val_Jacobian_jj[iVar][iVar] += 0.5*ProjGridVel;
				val_Jacobian_ji[iVar][iVar] += 0.5*ProjGridVel;
			}
		}
	}
  
	/*--- Computes differences btw. variables and Laplacians ---*/
	for (iVar = 0; iVar < nVar; iVar++) {
		Diff_Lapl[iVar] = Und_Lapl_i[iVar]-Und_Lapl_j[iVar];
		Diff_Psi[iVar]  = Psi_i[iVar]-Psi_j[iVar];
	}
  
	/*--- Adjustment to projected velocity due to mesh motion (TDE) ---*/
	if (grid_movement) {
		double ProjGridVel_i = 0.0; double ProjGridVel_j = 0.0; double ProjGridVel = 0.0;
		for (iDim = 0; iDim < nDim; iDim++) {
			ProjGridVel += 0.5*(GridVel_i[iDim]+GridVel_j[iDim])*Normal[iDim];
			ProjGridVel_i += GridVel_i[iDim]*Normal[iDim];
			ProjGridVel_j += GridVel_j[iDim]*Normal[iDim];
		}
		ProjVelocity_i -= ProjGridVel;
		ProjVelocity_j += ProjGridVel;
	}
  
	/*--- Compute the spectral radius and stretching factor ---*/
	Local_Lambda_i = (fabs(ProjVelocity_i)+SoundSpeed_i*Area);
	Local_Lambda_j = (fabs(ProjVelocity_j)+SoundSpeed_j*Area);
	MeanLambda = 0.5*(Local_Lambda_i+Local_Lambda_j);
  
	Phi_i = pow(Lambda_i/(4.0*MeanLambda+EPS),Param_p);
	Phi_j = pow(Lambda_j/(4.0*MeanLambda+EPS),Param_p);
	StretchingFactor = 4.0*Phi_i*Phi_j/(Phi_i+Phi_j+EPS);
  
	double sc2 = 3.0*(double(Neighbor_i)+double(Neighbor_j))/(double(Neighbor_i)*double(Neighbor_j));
	sc4 = sc2*sc2/4.0;
	Epsilon_2 = Param_Kappa_2*0.5*(Sensor_i+Sensor_j)*sc2;
	Epsilon_4 = max(0.0, Param_Kappa_4-Epsilon_2)*sc4;
  
	/*--- Compute viscous residual 1st- & 3rd-order dissipation ---*/
	for (iVar = 0; iVar < nVar; iVar++) {
		Residual = (Epsilon_2*Diff_Psi[iVar]-Epsilon_4*Diff_Lapl[iVar])*StretchingFactor*MeanLambda;
		val_resvisc_i[iVar] = -Residual;
		val_resvisc_j[iVar] =  Residual;
		if (implicit) {
			val_Jacobian_ii[iVar][iVar] -= Epsilon_2 + double(Neighbor_i+1)*Epsilon_4*StretchingFactor*MeanLambda;
			val_Jacobian_ij[iVar][iVar] += Epsilon_2 + double(Neighbor_j+1)*Epsilon_4*StretchingFactor*MeanLambda;
			val_Jacobian_ji[iVar][iVar] += Epsilon_2 + double(Neighbor_i+1)*Epsilon_4*StretchingFactor*MeanLambda;
			val_Jacobian_jj[iVar][iVar] -= Epsilon_2 + double(Neighbor_j+1)*Epsilon_4*StretchingFactor*MeanLambda;
		}
	}
}


CCentLax_AdjTNE2::CCentLax_AdjTNE2(unsigned short val_nDim,
                                   unsigned short val_nVar,
                                   unsigned short val_nPrimVar,
                                   unsigned short val_nPrimVarGrad,
                                   CConfig *config) : CNumerics(val_nDim,
                                                                val_nVar,
                                                                config) {

	implicit   = (config->GetKind_TimeIntScheme_AdjTNE2() == EULER_IMPLICIT);
  
  nDim         = val_nDim;
  nSpecies     = config->GetnSpecies();
  nVar         = val_nVar;
  nPrimVar     = val_nPrimVar;
  nPrimVarGrad = val_nPrimVarGrad;
  
	DiffPsi   = new double [nVar];
  MeanPsi    = new double [nVar];
  
  Proj_Jac_Tensor_i = new double*[nVar];
  Proj_Jac_Tensor_j = new double*[nVar];
  for (unsigned short iVar = 0; iVar < nVar; iVar++) {
    Proj_Jac_Tensor_i[iVar] = new double [nVar];
    Proj_Jac_Tensor_j[iVar] = new double [nVar];
  }
  
	Param_p = 0.3;
	Param_Kappa_0 = config->GetKappa_1st_AdjTNE2();
  
}

CCentLax_AdjTNE2::~CCentLax_AdjTNE2(void) {
  
	delete [] DiffPsi;
  delete [] MeanPsi;

  for (unsigned short iVar = 0; iVar < nVar; iVar++) {
    delete [] Proj_Jac_Tensor_i[iVar];
    delete [] Proj_Jac_Tensor_j[iVar];
  }
  delete [] Proj_Jac_Tensor_i;
  delete [] Proj_Jac_Tensor_j;
}

void CCentLax_AdjTNE2::ComputeResidual (double *val_resconv_i,
                                        double *val_resvisc_i,
                                        double *val_resconv_j,
                                        double *val_resvisc_j,
                                        double **val_Jacobian_ii,
                                        double **val_Jacobian_ij,
                                        double **val_Jacobian_ji,
                                        double **val_Jacobian_jj,
                                        CConfig *config) {

  unsigned short iDim, iVar, jVar;
  double ProjVel_i, ProjVel_j;
  double Phi_i, Phi_j;
  double Local_Lambda_i, Local_Lambda_j, MeanLambda;
  double Residual;
  double StretchingFactor, sc2, Epsilon_0;
  
  /*--- Initialize the residuals ---*/
  for (iVar = 0; iVar < nVar; iVar++) {
    val_resconv_i[iVar] = 0.0;
    val_resconv_j[iVar] = 0.0;
    val_resvisc_i[iVar] = 0.0;
    val_resvisc_j[iVar] = 0.0;
  }
  
  /*--- Compute geometric parameters ---*/
	Area = 0.0;
  for (iDim = 0; iDim < nDim; iDim++) Area += Normal[iDim]*Normal[iDim];
	Area = sqrt(Area);
	for (iDim = 0; iDim < nDim; iDim++) {
		UnitNormal[iDim] = Normal[iDim]/Area;
    if (fabs(UnitNormal[iDim]) < EPS) UnitNormal[iDim] = EPS;
  }
  
  /*--- Calculate the mean & differences of the adjoint variables ---*/
  for (iVar = 0; iVar < nVar; iVar++) {
    MeanPsi[iVar] = 0.5 * (Psi_i[iVar]+Psi_j[iVar]);
    DiffPsi[iVar] = Psi_i[iVar]-Psi_j[iVar];
  }
  
  /*--- Calculate inviscid projected flux Jacobians ---*/
  GetInviscidProjJac(U_i, V_i, dPdU_i, Normal, 1.0, Proj_Jac_Tensor_i);
  GetInviscidProjJac(U_j, V_j, dPdU_j, Normal, 1.0, Proj_Jac_Tensor_j);
  
  /*--- Compute inviscid residual at point i, A^T*Psi ---*/
  for (iVar = 0; iVar < nVar; iVar++) {
    for (jVar = 0; jVar < nVar; jVar++) {
      val_resconv_i[iVar] += Proj_Jac_Tensor_i[jVar][iVar]*MeanPsi[jVar];
      val_resconv_j[iVar] -= Proj_Jac_Tensor_j[jVar][iVar]*MeanPsi[jVar];
    }
  }
  
  if (implicit) {
    for (iVar = 0; iVar < nVar; iVar++) {
      for (jVar = 0; jVar < nVar; jVar++) {
        val_Jacobian_ii[iVar][jVar] = Proj_Jac_Tensor_i[jVar][iVar];
        val_Jacobian_ij[iVar][jVar] = Proj_Jac_Tensor_i[jVar][iVar];
        val_Jacobian_jj[iVar][jVar] = -Proj_Jac_Tensor_j[jVar][iVar];
        val_Jacobian_ji[iVar][jVar] = -Proj_Jac_Tensor_j[jVar][iVar];
      }
    }
  }
  
  /*--- Compute spectral radius ---*/
  ProjVel_i = 0.0;
  ProjVel_j = 0.0;
  for (iDim = 0; iDim < nDim; iDim++) {
    ProjVel_i += V_i[VEL_INDEX+iDim]*Normal[iDim];
    ProjVel_j -= V_j[VEL_INDEX+iDim]*Normal[iDim];
  }
	Local_Lambda_i = (fabs(ProjVel_i)+V_i[A_INDEX]*Area);
	Local_Lambda_j = (fabs(ProjVel_j)+V_j[A_INDEX]*Area);
	MeanLambda = 0.5*(Local_Lambda_i+Local_Lambda_j);
  
	/*--- Compute streching factor ---*/
	Phi_i = pow(Lambda_i/(4.0*MeanLambda+EPS),Param_p);
	Phi_j = pow(Lambda_j/(4.0*MeanLambda+EPS),Param_p);
	StretchingFactor = 4.0*Phi_i*Phi_j/(Phi_i+Phi_j+EPS);
  
	sc2 = 3.0*(double(Neighbor_i)+double(Neighbor_j))/(double(Neighbor_i)*double(Neighbor_j));
	Epsilon_0 = Param_Kappa_0*sc2*double(nDim)/3.0;
  
	/*--- Artifical dissipation evaluation ---*/
	for (iVar = 0; iVar < nVar; iVar++) {
		Residual = Epsilon_0*StretchingFactor*MeanLambda*DiffPsi[iVar];
		val_resvisc_i[iVar] = -Residual;
		val_resvisc_j[iVar] =  Residual;
	}
  
	/*--- Contribution to implicit part ---*/
	if (implicit) {
		for (iVar = 0; iVar < nVar; iVar++) {
			val_Jacobian_ii[iVar][iVar] -= Epsilon_0*StretchingFactor*MeanLambda;
			val_Jacobian_ij[iVar][iVar] += Epsilon_0*StretchingFactor*MeanLambda;
			val_Jacobian_ji[iVar][iVar] += Epsilon_0*StretchingFactor*MeanLambda;
			val_Jacobian_jj[iVar][iVar] -= Epsilon_0*StretchingFactor*MeanLambda;
		}
	}
}


CAvgGrad_AdjTNE2::CAvgGrad_AdjTNE2(unsigned short val_nDim,
                                   unsigned short val_nVar,
                                   CConfig *config) : CNumerics(val_nDim,
                                                                val_nVar,
                                                                config) {
	unsigned short iVar;
  
  implicit = (config->GetKind_TimeIntScheme_AdjFlow() == EULER_IMPLICIT);
  
  nDim         = val_nDim;
  nSpecies     = config->GetnSpecies();
  nVar         = val_nVar;
  
  Mean_GPsi = new double*[nVar];
  Dxx = new double*[nVar];
  Dxy = new double*[nVar];
  Dxz = new double*[nVar];
  Dyx = new double*[nVar];
  Dyy = new double*[nVar];
  Dyz = new double*[nVar];
  Dzx = new double*[nVar];
  Dzy = new double*[nVar];
  Dzz = new double*[nVar];
  for (iVar = 0; iVar < nVar; iVar++) {
    Mean_GPsi[iVar] = new double[nDim];
    Dxx[iVar] = new double[nVar];
    Dxy[iVar] = new double[nVar];
    Dxz[iVar] = new double[nVar];
    Dyx[iVar] = new double[nVar];
    Dyy[iVar] = new double[nVar];
    Dyz[iVar] = new double[nVar];
    Dzx[iVar] = new double[nVar];
    Dzy[iVar] = new double[nVar];
    Dzz[iVar] = new double[nVar];
  }
  
  
  
  
//	Velocity_i = new double [nDim];
//	Velocity_j = new double [nDim];
//	Mean_Velocity = new double [nDim];
//	Mean_GradPhi = new double* [nDim];
//	for (iDim = 0; iDim < nDim; iDim++)
//		Mean_GradPhi[iDim] = new double [nDim];
//	Mean_GradPsiE = new double [nDim];
//	Edge_Vector = new double [nDim];
  
}

CAvgGrad_AdjTNE2::~CAvgGrad_AdjTNE2(void) {
  unsigned short iVar;
  
  for (iVar = 0; iVar < nVar; iVar++) {
    delete [] Mean_GPsi[iVar];
    delete [] Dxx[iVar];
    delete [] Dxy[iVar];
    delete [] Dxz[iVar];
    delete [] Dyx[iVar];
    delete [] Dyy[iVar];
    delete [] Dyz[iVar];
    delete [] Dzx[iVar];
    delete [] Dzy[iVar];
    delete [] Dzz[iVar];
  }
  delete [] Mean_GPsi;
  delete [] Dxx;
  delete [] Dxy;
  delete [] Dxz;
  delete [] Dyx;
  delete [] Dyy;
  delete [] Dyz;
  delete [] Dzx;
  delete [] Dzy;
  delete [] Dzz;
  
//	delete [] Velocity_i;
//	delete [] Velocity_j;
//	delete [] Mean_Velocity;
//	delete [] Edge_Vector;
//	delete [] Mean_GradPsiE;
//	for (unsigned short iDim = 0; iDim < nDim; iDim++)
//		delete [] Mean_GradPhi[iDim];
}

void CAvgGrad_AdjTNE2::ComputeResidual(double *val_residual_i, double *val_residual_j,
                                       double **val_Jacobian_ii, double **val_Jacobian_ij,
                                       double **val_Jacobian_ji, double **val_Jacobian_jj, CConfig *config) {
  unsigned short iDim, iSpecies, iVar, jVar;
	double rho;
  double u, v, w, sqvel;
  
  if (nDim == 2) {
    cout << "ERROR!!!  CAvgGrad_AdjTNE2 not configured for 2D simulations!  Exiting..." << endl;
    exit(1);
  }
  
  /*--- Caclulate mean quantities ---*/
  for (iVar = 0; iVar < nVar; iVar++)
    for (iDim = 0; iDim < nDim; iDim++)
      Mean_GPsi[iVar][iDim] = 0.5*(PsiVar_Grad_i[iVar][iDim] +
                                   PsiVar_Grad_j[iVar][iDim]  );
  
  /*--- Rename for convenience ---*/
  rho = 0.5*(V_i[RHO_INDEX]   + V_j[RHO_INDEX]  );
  u   = 0.5*(V_i[VEL_INDEX]   + V_j[VEL_INDEX]  );
  v   = 0.5*(V_i[VEL_INDEX+1] + V_j[VEL_INDEX+1]);
  w   = 0.5*(V_i[VEL_INDEX+2] + V_j[VEL_INDEX+2]);
  sqvel = u*u+v*v+w*w;
  
  for (iVar = 0; iVar < nVar; iVar++) {
    val_residual_i[iVar] = 0.0;
    val_residual_j[iVar] = 0.0;
    for (jVar = 0; jVar < nVar; jVar++) {
      Dxx[iVar][jVar] = 0.0;
      Dxy[iVar][jVar] = 0.0;
      Dxz[iVar][jVar] = 0.0;
      Dyx[iVar][jVar] = 0.0;
      Dyy[iVar][jVar] = 0.0;
      Dyz[iVar][jVar] = 0.0;
      Dzx[iVar][jVar] = 0.0;
      Dzy[iVar][jVar] = 0.0;
      Dzz[iVar][jVar] = 0.0;
    }
  }
  
  /*--- Dxxv2 ---*/
  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
    Dxx[nSpecies][iSpecies]   = -(4.0/3.0)*u/rho;
    Dxx[nSpecies+1][iSpecies] = -v/rho;
    Dxx[nSpecies+2][iSpecies] = -w/rho;
    Dxx[nSpecies+3][iSpecies] = -sqvel/rho - 1.0/3.0*u*u/rho;
  }
  //
  Dxx[nSpecies][nSpecies]     = (4.0/3.0)*1/rho;
  //
  Dxx[nSpecies+1][nSpecies+1] = 1/rho;
  //
  Dxx[nSpecies+2][nSpecies+2] = 1/rho;
  //
  Dxx[nSpecies+3][nSpecies] = (4.0/3.0)*u/rho;
  Dxx[nSpecies+3][nSpecies+1] = v/rho;
  Dxx[nSpecies+3][nSpecies+2] = w/rho;
  
  /*--- Dxyv2 ---*/
  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
    Dxy[nSpecies][iSpecies]   = (2.0/3.0)*v/rho;
    Dxy[nSpecies+1][iSpecies] = -u/rho;
    Dxy[nSpecies+3][iSpecies] = -u*v/(3*rho);
  }
  //
  Dxy[nSpecies][nSpecies+1]   = -(2.0/3.0)*1/rho;
  //
  Dxy[nSpecies+1][nSpecies]   = 1/rho;
  //
  //
  Dxy[nSpecies+3][nSpecies] = v/rho;
  Dxy[nSpecies+3][nSpecies+1] = -(2.0/3.0)*u/rho;
  
  /*--- Dxzv2 ---*/
  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
    Dxz[nSpecies][iSpecies]   = (2.0/3.0)*w/rho;
    Dxz[nSpecies+2][iSpecies] = -u/rho;
    Dxz[nSpecies+3][iSpecies] = -u*w/(3*rho);
  }
  //
  Dxz[nSpecies][nSpecies+2]   = -(2.0/3.0)*1/rho;
  //
  //
  Dxz[nSpecies+2][nSpecies]   = 1/rho;
  //
  Dxz[nSpecies+3][nSpecies] = w/rho;
  Dxz[nSpecies+3][nSpecies+2] = -(2.0/3.0)*u/rho;
  
  /*--- Dyxv2 ---*/
  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
    Dyx[nSpecies][iSpecies]   = -v/rho;
    Dyx[nSpecies+1][iSpecies] = 2.0/3.0*u/rho;
    Dyx[nSpecies+3][iSpecies] = -u*v/(3*rho);
  }
  //
  Dyx[nSpecies][nSpecies+1]   = 1/rho;
  //
  Dyx[nSpecies+1][nSpecies]   = -(2.0/3.0)*1/rho;
  //
  //
  Dyx[nSpecies+3][nSpecies]   = -(2.0/3.0)*v/rho;
  Dyx[nSpecies+3][nSpecies+1] = u/rho;
  
  /*--- Dyyv2 ---*/
  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
    Dyy[nSpecies][iSpecies]   = -u/rho;
    Dyy[nSpecies+1][iSpecies] = -(4.0/3.0)*v/rho;
    Dyy[nSpecies+2][iSpecies] = -w/rho;
    Dyy[nSpecies+3][iSpecies] = -sqvel/rho - 1.0/3.0*v*v/rho;
  }
  //
  Dyy[nSpecies][nSpecies]     = 1/rho;
  //
  Dyy[nSpecies+1][nSpecies+1] = (4.0/3.0)*1/rho;
  //
  Dyy[nSpecies+2][nSpecies+2] = 1/rho;
  //
  Dyy[nSpecies+3][nSpecies]   = u/rho;
  Dyy[nSpecies+3][nSpecies+1] = (4.0/3.0)*v/rho;
  Dyy[nSpecies+3][nSpecies+2] = w/rho;
  
  /*--- Dyzv2 ---*/
  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
    Dyz[nSpecies+1][iSpecies] = 2.0/3.0*w/rho;
    Dyz[nSpecies+2][iSpecies] = -v/rho;
    Dyz[nSpecies+3][iSpecies] = -v*w/(3*rho);
  }
  //
  //
  Dyz[nSpecies+1][nSpecies+2] = -(2.0/3.0)*1/rho;
  //
  Dyz[nSpecies+2][nSpecies+1] = 1/rho;
  //
  //
  Dyz[nSpecies+3][nSpecies+1] = w/rho;
  Dyz[nSpecies+3][nSpecies+2] = -(2.0/3.0)*v/rho;
  
  /*--- Dzxv2 ---*/
  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
    Dzx[nSpecies][iSpecies]   = -w/rho;
    Dzx[nSpecies+2][iSpecies] = (2.0/3.0)*u/rho;
    Dzx[nSpecies+3][iSpecies] = -u*w/(3*rho);
  }
  //
  Dzx[nSpecies][nSpecies+2]   = 1/rho;
  //
  //
  Dzx[nSpecies+2][nSpecies]   = -(2.0/3.0)*1/rho;
  //
  Dzx[nSpecies+3][nSpecies]   = -(2.0/3.0)*w/rho;
  Dzx[nSpecies+3][nSpecies+2] = u/rho;
  
  /*--- Dzyv2 ---*/
  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
    Dzy[nSpecies+1][iSpecies] = -w/rho;
    Dzy[nSpecies+2][iSpecies] = (2.0/3.0)*v/rho;
    Dzy[nSpecies+3][iSpecies] = -v*w/(3*rho);
  }
  //
  //
  Dzy[nSpecies+1][nSpecies+2] = 1/rho;
  //
  Dzy[nSpecies+2][nSpecies+1] = -(2.0/3.0)*1/rho;
  //
  //
  Dzy[nSpecies+3][nSpecies+1] = -(2.0/3.0)*w/rho;
  Dzy[nSpecies+3][nSpecies+2] = v/rho;
  
  /*--- Dzzv2 ---*/
  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
    Dzz[nSpecies][iSpecies]   = -u/rho;
    Dzz[nSpecies+1][iSpecies] = -v/rho;
    Dzz[nSpecies+1][iSpecies] = -(4.0/3.0)*w/rho;
    Dzz[nSpecies+3][iSpecies] = -sqvel/rho - 1.0/3.0*w*w/rho;
  }
  //
  Dzz[nSpecies][nSpecies]     = 1/rho;
  //
  Dzz[nSpecies+1][nSpecies+1] = 1/rho;
  //
  Dzz[nSpecies+2][nSpecies+2] = (4.0/3.0)*1/rho;
  //
  Dzz[nSpecies+3][nSpecies]   = u/rho;
  Dzz[nSpecies+3][nSpecies+1] = v/rho;
  Dzz[nSpecies+3][nSpecies+2] = (4.0/3.0)*w/rho;
  
  /*--- Residual calculation ---*/
  
//  unsigned short iDim, jDim, iVar, jVar;
//	double sq_vel_i, Energy_i, ViscDens_i, XiDens_i,
//  sq_vel_j, Energy_j, ViscDens_j, XiDens_j, dist_ij_2, dPhiE_dn,
//	Sigma_xx, Sigma_yy, Sigma_zz, Sigma_xy, Sigma_xz, Sigma_yz,
//	Sigma_xx5, Sigma_yy5, Sigma_zz5, Sigma_xy5, Sigma_xz5,
//	Sigma_yz5, Sigma_5, eta_xx, eta_yy, eta_zz, eta_xy, eta_xz, eta_yz;
//  
//  /*--- Local variables needed for Jacobian calculations ---*/
//  double dSigmaxx_phi1, dSigmayy_phi1, dSigmazz_phi1, dSigmaxy_phi1, dSigmaxz_phi1, dSigmayz_phi1;
//  double dSigmaxx_phi2, dSigmayy_phi2, dSigmazz_phi2, dSigmaxy_phi2, dSigmaxz_phi2, dSigmayz_phi2;
//  double dSigmaxx_phi3, dSigmayy_phi3, dSigmazz_phi3, dSigmaxy_phi3, dSigmaxz_phi3, dSigmayz_phi3;
//  double dSigmaxx5_psi5, dSigmayy5_psi5, dSigmazz5_psi5, dSigmaxy5_psi5, dSigmaxz5_psi5, dSigmayz5_psi5, dSigma5_psi5;
//  
//	/*--- States at the point i ---*/
//	Density_i = U_i[0];
//	sq_vel_i = 0;
//	for (iDim = 0; iDim < nDim; iDim++) {
//		Velocity_i[iDim] = U_i[iDim+1] / Density_i;
//		sq_vel_i += 0.5*Velocity_i[iDim]*Velocity_i[iDim];
//	}
//	Energy_i = U_i[nDim+1] / Density_i;
//	SoundSpeed_i = sqrt(Gamma*Gamma_Minus_One*(Energy_i-sq_vel_i));
//	Pressure_i = (SoundSpeed_i * SoundSpeed_i * Density_i) / Gamma;
//	ViscDens_i = (Laminar_Viscosity_i + Eddy_Viscosity_i) / Density_i;
//	XiDens_i = Gamma * (Laminar_Viscosity_i/PRANDTL + Eddy_Viscosity_i/PRANDTL_TURB) / Density_i;
//  
//	/*--- States at the point j ---*/
//	Density_j = U_j[0];
//	sq_vel_j = 0;
//	for (iDim = 0; iDim < nDim; iDim++) {
//		Velocity_j[iDim] = U_j[iDim+1] / Density_j;
//		sq_vel_j += 0.5*Velocity_j[iDim]*Velocity_j[iDim];
//	}
//	Energy_j = U_j[nDim+1] / Density_j;
//	SoundSpeed_j = sqrt(Gamma*Gamma_Minus_One*(Energy_j-sq_vel_j));
//	Pressure_j = (SoundSpeed_j * SoundSpeed_j * Density_j) / Gamma;
//	ViscDens_j = (Laminar_Viscosity_j + Eddy_Viscosity_j) / Density_j;
//	XiDens_j = Gamma *(Laminar_Viscosity_j/PRANDTL + Eddy_Viscosity_j/PRANDTL_TURB) / Density_j;
//  
//	/*--- Compute vector going from iPoint to jPoint ---*/
//	dist_ij_2 = 0.0;
//	for (iDim = 0; iDim < nDim; iDim++) {
//		Edge_Vector[iDim] = Coord_j[iDim]-Coord_i[iDim];
//		dist_ij_2 += Edge_Vector[iDim]*Edge_Vector[iDim];
//	}
//  
//	/*--- Average of the derivatives of the adjoint variables ---*/
//	for (iDim = 0; iDim < nDim; iDim++) {
//		Mean_GradPsiE[iDim] =  0.5*(PsiVar_Grad_i[nVar-1][iDim]+PsiVar_Grad_j[nVar-1][iDim]);
//		for (jDim = 0; jDim < nDim; jDim++)
//			Mean_GradPhi[iDim][jDim] =  0.5*(PsiVar_Grad_i[iDim+1][jDim]+PsiVar_Grad_j[iDim+1][jDim]);
//	}
//  
//	dPhiE_dn = 0;
//	for (iDim = 0; iDim < nDim; iDim++)
//		dPhiE_dn += Mean_GradPsiE[iDim]*Normal[iDim];
//  
//	/*--- Compute the viscous residual ---*/
//	if (nDim == 3) {
//    
//		/*--- Residual at iPoint ---*/
//		Sigma_xx = ViscDens_i * (FOUR3 * Mean_GradPhi[0][0] -  TWO3 * Mean_GradPhi[1][1] - TWO3  * Mean_GradPhi[2][2]);
//		Sigma_yy = ViscDens_i * (-TWO3 * Mean_GradPhi[0][0] + FOUR3 * Mean_GradPhi[1][1] - TWO3  * Mean_GradPhi[2][2]);
//		Sigma_zz = ViscDens_i * (-TWO3 * Mean_GradPhi[0][0] -  TWO3 * Mean_GradPhi[1][1] + FOUR3 * Mean_GradPhi[2][2]);
//		Sigma_xy = ViscDens_i * (Mean_GradPhi[1][0] + Mean_GradPhi[0][1]);
//		Sigma_xz = ViscDens_i * (Mean_GradPhi[2][0] + Mean_GradPhi[0][2]);
//		Sigma_yz = ViscDens_i * (Mean_GradPhi[2][1] + Mean_GradPhi[1][2]);
//		Sigma_xx5 = ViscDens_i * ( FOUR3 * Velocity_i[0] * Mean_GradPsiE[0] -  TWO3 * Velocity_i[1] * Mean_GradPsiE[1] -  TWO3 * Velocity_i[2] * Mean_GradPsiE[2]);
//		Sigma_yy5 = ViscDens_i * (- TWO3 * Velocity_i[0] * Mean_GradPsiE[0] + FOUR3 * Velocity_i[1] * Mean_GradPsiE[1] -  TWO3 * Velocity_i[2] * Mean_GradPsiE[2]);
//		Sigma_zz5 = ViscDens_i * (- TWO3 * Velocity_i[0] * Mean_GradPsiE[0] -  TWO3 * Velocity_i[1] * Mean_GradPsiE[1] + FOUR3 * Velocity_i[2] * Mean_GradPsiE[2]);
//		Sigma_xy5 = ViscDens_i * (Velocity_i[0] * Mean_GradPsiE[1] + Velocity_i[1] * Mean_GradPsiE[0]);
//		Sigma_xz5 = ViscDens_i * (Velocity_i[0] * Mean_GradPsiE[2] + Velocity_i[2] * Mean_GradPsiE[0]);
//		Sigma_yz5 = ViscDens_i * (Velocity_i[1] * Mean_GradPsiE[2] + Velocity_i[2] * Mean_GradPsiE[1]);
//		Sigma_5   = XiDens_i * dPhiE_dn;
//		eta_xx = Sigma_xx + Sigma_xx5; eta_yy = Sigma_yy + Sigma_yy5; eta_zz = Sigma_zz + Sigma_zz5;
//		eta_xy = Sigma_xy + Sigma_xy5; eta_xz = Sigma_xz + Sigma_xz5; eta_yz = Sigma_yz + Sigma_yz5;
//    
//		val_residual_i[0] = - (Velocity_i[0] * Normal[0] * eta_xx  + Velocity_i[1] * Normal[1] * eta_yy + Velocity_i[2] * Normal[2] * eta_zz
//                           + (Velocity_i[0] * Normal[1] + Velocity_i[1] * Normal[0]) * eta_xy
//                           + (Velocity_i[0] * Normal[2] + Velocity_i[2] * Normal[0]) * eta_xz
//                           + (Velocity_i[2] * Normal[1] + Velocity_i[1] * Normal[2]) * eta_yz
//                           - (sq_vel_i - Pressure_i/(Density_i*Gamma_Minus_One)) * Sigma_5);
//    
//		val_residual_i[1] = (eta_xx * Normal[0] + eta_xy * Normal[1] + eta_xz * Normal[2] - Velocity_i[0] * Sigma_5);
//		val_residual_i[2] = (eta_xy * Normal[0] + eta_yy * Normal[1] + eta_yz * Normal[2] - Velocity_i[1] * Sigma_5);
//		val_residual_i[3] = (eta_xz * Normal[0] + eta_yz * Normal[1] + eta_zz * Normal[2] - Velocity_i[2] * Sigma_5);
//		val_residual_i[4] = (Sigma_5);
//    
//		/*--- Computation of the Jacobians at Point i---*/
//    
//    if (implicit) {
//      dSigmaxx_phi1 = -FOUR3 * ViscDens_i * Edge_Vector[0]/dist_ij_2;
//      dSigmaxx_phi2 =   TWO3 * ViscDens_i * Edge_Vector[1]/dist_ij_2;
//      dSigmaxx_phi3 =   TWO3 * ViscDens_i * Edge_Vector[2]/dist_ij_2;
//      dSigmayy_phi1 =   TWO3 * ViscDens_i * Edge_Vector[0]/dist_ij_2;
//      dSigmayy_phi2 = -FOUR3 * ViscDens_i * Edge_Vector[1]/dist_ij_2;
//      dSigmayy_phi3 =   TWO3 * ViscDens_i * Edge_Vector[2]/dist_ij_2;
//      dSigmazz_phi1 =   TWO3 * ViscDens_i * Edge_Vector[0]/dist_ij_2;
//      dSigmazz_phi2 =   TWO3 * ViscDens_i * Edge_Vector[1]/dist_ij_2;
//      dSigmazz_phi3 = -FOUR3 * ViscDens_i * Edge_Vector[2]/dist_ij_2;
//      dSigmaxy_phi1 = -ViscDens_i * Edge_Vector[1]/dist_ij_2;
//      dSigmaxy_phi2 = -ViscDens_i * Edge_Vector[0]/dist_ij_2;
//      dSigmaxy_phi3 = 0;
//      dSigmaxz_phi1 = -ViscDens_i * Edge_Vector[2]/dist_ij_2;
//      dSigmaxz_phi2 = 0;
//      dSigmaxz_phi3 = -ViscDens_i * Edge_Vector[0]/dist_ij_2;
//      dSigmayz_phi1 = 0;
//      dSigmayz_phi2 = -ViscDens_i * Edge_Vector[2]/dist_ij_2;
//      dSigmayz_phi3 = -ViscDens_i * Edge_Vector[1]/dist_ij_2;
//      
//      dSigmaxx5_psi5 = -ViscDens_i * ( FOUR3*Velocity_i[0]*Edge_Vector[0] -  TWO3*Velocity_i[1]*Edge_Vector[1] -  TWO3*Velocity_i[2]*Edge_Vector[2])/dist_ij_2;
//      dSigmayy5_psi5 = -ViscDens_i * (- TWO3*Velocity_i[0]*Edge_Vector[0] + FOUR3*Velocity_i[1]*Edge_Vector[1] -  TWO3*Velocity_i[2]*Edge_Vector[2])/dist_ij_2;
//      dSigmazz5_psi5 = -ViscDens_i * (- TWO3*Velocity_i[0]*Edge_Vector[0] -  TWO3*Velocity_i[1]*Edge_Vector[1] + FOUR3*Velocity_i[2]*Edge_Vector[2])/dist_ij_2;
//      dSigmaxy5_psi5 = -ViscDens_i * ( Velocity_i[0]*Edge_Vector[1] + Velocity_i[1]*Edge_Vector[0] )/dist_ij_2;
//      dSigmaxz5_psi5 = -ViscDens_i * ( Velocity_i[0]*Edge_Vector[2] + Velocity_i[2]*Edge_Vector[0] )/dist_ij_2;
//      dSigmayz5_psi5 = -ViscDens_i * ( Velocity_i[1]*Edge_Vector[2] + Velocity_i[2]*Edge_Vector[1] )/dist_ij_2;
//      dSigma5_psi5   = -XiDens_i * ( Edge_Vector[0]*Normal[0] + Edge_Vector[1]*Normal[1] + Edge_Vector[2]*Normal[2] )/dist_ij_2;
//      
//      val_Jacobian_ii[0][0] = 0;
//      val_Jacobian_ii[0][1] = -( Velocity_i[0]*Normal[0]*dSigmaxx_phi1 + Velocity_i[1]*Normal[1]*dSigmayy_phi1 + Velocity_i[2]*Normal[2]*dSigmazz_phi1
//                                + (Velocity_i[0]*Normal[1] + Velocity_i[1]*Normal[0])*dSigmaxy_phi1
//                                + (Velocity_i[0]*Normal[2] + Velocity_i[2]*Normal[0])*dSigmaxz_phi1
//                                + (Velocity_i[2]*Normal[1] + Velocity_i[1]*Normal[2])*dSigmayz_phi1 );
//      val_Jacobian_ii[0][2] = -( Velocity_i[0]*Normal[0]*dSigmaxx_phi2 + Velocity_i[1]*Normal[1]*dSigmayy_phi2 + Velocity_i[2]*Normal[2]*dSigmazz_phi2
//                                + (Velocity_i[0]*Normal[1] + Velocity_i[1]*Normal[0])*dSigmaxy_phi2
//                                + (Velocity_i[0]*Normal[2] + Velocity_i[2]*Normal[0])*dSigmaxz_phi2
//                                + (Velocity_i[2]*Normal[1] + Velocity_i[1]*Normal[2])*dSigmayz_phi2 );
//      val_Jacobian_ii[0][3] = -( Velocity_i[0]*Normal[0]*dSigmaxx_phi3 + Velocity_i[1]*Normal[1]*dSigmayy_phi3 + Velocity_i[2]*Normal[2]*dSigmazz_phi3
//                                + (Velocity_i[0]*Normal[1] + Velocity_i[1]*Normal[0])*dSigmaxy_phi3
//                                + (Velocity_i[0]*Normal[2] + Velocity_i[2]*Normal[0])*dSigmaxz_phi3
//                                + (Velocity_i[2]*Normal[1] + Velocity_i[1]*Normal[2])*dSigmayz_phi3 );
//      val_Jacobian_ii[0][4] = (sq_vel_i - Pressure_i/(Density_i*Gamma_Minus_One)) * dSigma5_psi5;
//      
//      val_Jacobian_ii[1][0] = 0;
//      val_Jacobian_ii[1][1] = Normal[0]*dSigmaxx_phi1 + Normal[1]*dSigmaxy_phi1 + Normal[2]*dSigmaxz_phi1;
//      val_Jacobian_ii[1][2] = Normal[0]*dSigmaxx_phi2 + Normal[1]*dSigmaxy_phi2 + Normal[2]*dSigmaxz_phi2;
//      val_Jacobian_ii[1][3] = Normal[0]*dSigmaxx_phi3 + Normal[1]*dSigmaxy_phi3 + Normal[2]*dSigmaxz_phi3;
//      val_Jacobian_ii[1][4] = -Velocity_i[0]*dSigma5_psi5;
//      
//      val_Jacobian_ii[2][0] = 0;
//      val_Jacobian_ii[2][1] = Normal[0]*dSigmaxy_phi1 + Normal[1]*dSigmayy_phi1 + Normal[2]*dSigmayz_phi1;
//      val_Jacobian_ii[2][2] = Normal[0]*dSigmaxy_phi2 + Normal[1]*dSigmayy_phi2 + Normal[2]*dSigmayz_phi2;
//      val_Jacobian_ii[2][3] = Normal[0]*dSigmaxy_phi3 + Normal[1]*dSigmayy_phi3 + Normal[2]*dSigmayz_phi3;
//      val_Jacobian_ii[2][4] = -Velocity_i[1]*dSigma5_psi5;
//      
//      val_Jacobian_ii[3][0] = 0;
//      val_Jacobian_ii[3][1] = Normal[0]*dSigmaxz_phi1 + Normal[1]*dSigmayz_phi1 + Normal[2]*dSigmazz_phi1;
//      val_Jacobian_ii[3][2] = Normal[0]*dSigmaxz_phi2 + Normal[1]*dSigmayz_phi2 + Normal[2]*dSigmazz_phi2;
//      val_Jacobian_ii[3][3] = Normal[0]*dSigmaxz_phi3 + Normal[1]*dSigmayz_phi3 + Normal[2]*dSigmazz_phi3;
//      val_Jacobian_ii[3][4] = -Velocity_i[2]*dSigma5_psi5;
//      
//      val_Jacobian_ii[4][0] = 0;
//      val_Jacobian_ii[4][1] = 0;
//      val_Jacobian_ii[4][2] = 0;
//      val_Jacobian_ii[4][3] = 0;
//      val_Jacobian_ii[4][4] = dSigma5_psi5;
//      
//      for (iVar = 0; iVar < nVar; iVar++)
//        for (jVar = 0; jVar < nVar; jVar++)
//          val_Jacobian_ij[iVar][jVar] = -val_Jacobian_ii[iVar][jVar];
//    }
//    
//		/*--- Residual at jPoint ---*/
//		Sigma_xx = ViscDens_j * (FOUR3 * Mean_GradPhi[0][0] -  TWO3 * Mean_GradPhi[1][1] - TWO3  * Mean_GradPhi[2][2]);
//		Sigma_yy = ViscDens_j * (-TWO3 * Mean_GradPhi[0][0] + FOUR3 * Mean_GradPhi[1][1] - TWO3  * Mean_GradPhi[2][2]);
//		Sigma_zz = ViscDens_j * (-TWO3 * Mean_GradPhi[0][0] -  TWO3 * Mean_GradPhi[1][1] + FOUR3 * Mean_GradPhi[2][2]);
//		Sigma_xy = ViscDens_j * (Mean_GradPhi[1][0] + Mean_GradPhi[0][1]);
//		Sigma_xz = ViscDens_j * (Mean_GradPhi[2][0] + Mean_GradPhi[0][2]);
//		Sigma_yz = ViscDens_j * (Mean_GradPhi[2][1] + Mean_GradPhi[1][2]);
//		Sigma_xx5 = ViscDens_j * ( FOUR3 * Velocity_j[0] * Mean_GradPsiE[0] -  TWO3 * Velocity_j[1] * Mean_GradPsiE[1] -  TWO3 * Velocity_j[2] * Mean_GradPsiE[2]);
//		Sigma_yy5 = ViscDens_j * (- TWO3 * Velocity_j[0] * Mean_GradPsiE[0] + FOUR3 * Velocity_j[1] * Mean_GradPsiE[1] -  TWO3 * Velocity_j[2] * Mean_GradPsiE[2]);
//		Sigma_zz5 = ViscDens_j * (- TWO3 * Velocity_j[0] * Mean_GradPsiE[0] -  TWO3 * Velocity_j[1] * Mean_GradPsiE[1] + FOUR3 * Velocity_j[2] * Mean_GradPsiE[2]);
//		Sigma_xy5 = ViscDens_j * (Velocity_j[0] * Mean_GradPsiE[1] + Velocity_j[1] * Mean_GradPsiE[0]);
//		Sigma_xz5 = ViscDens_j * (Velocity_j[0] * Mean_GradPsiE[2] + Velocity_j[2] * Mean_GradPsiE[0]);
//		Sigma_yz5 = ViscDens_j * (Velocity_j[1] * Mean_GradPsiE[2] + Velocity_j[2] * Mean_GradPsiE[1]);
//		Sigma_5   = XiDens_j * dPhiE_dn;
//		eta_xx = Sigma_xx + Sigma_xx5; eta_yy = Sigma_yy + Sigma_yy5; eta_zz = Sigma_zz + Sigma_zz5;
//		eta_xy = Sigma_xy + Sigma_xy5; eta_xz = Sigma_xz + Sigma_xz5; eta_yz = Sigma_yz + Sigma_yz5;
//    
//		val_residual_j[0] = - (Velocity_j[0] * Normal[0] * eta_xx  + Velocity_j[1] * Normal[1] * eta_yy + Velocity_j[2] * Normal[2] * eta_zz
//                           + (Velocity_j[0] * Normal[1] + Velocity_j[1] * Normal[0]) * eta_xy
//                           + (Velocity_j[0] * Normal[2] + Velocity_j[2] * Normal[0]) * eta_xz
//                           + (Velocity_j[2] * Normal[1] + Velocity_j[1] * Normal[2]) * eta_yz
//                           - (sq_vel_j - Pressure_j/(Density_j*Gamma_Minus_One)) * Sigma_5);
//		val_residual_j[1] = (eta_xx * Normal[0] + eta_xy * Normal[1] + eta_xz * Normal[2] - Velocity_j[0] * Sigma_5);
//		val_residual_j[2] = (eta_xy * Normal[0] + eta_yy * Normal[1] + eta_yz * Normal[2] - Velocity_j[1] * Sigma_5);
//		val_residual_j[3] = (eta_xz * Normal[0] + eta_yz * Normal[1] + eta_zz * Normal[2] - Velocity_j[2] * Sigma_5);
//		val_residual_j[4] = (Sigma_5);
//    
//		/*--- Computation of the Jacobians at Point j---*/
//    if (implicit) {
//      dSigmaxx_phi1 = FOUR3 * ViscDens_j * Edge_Vector[0]/dist_ij_2;
//      dSigmaxx_phi2 = -TWO3 * ViscDens_j * Edge_Vector[1]/dist_ij_2;
//      dSigmaxx_phi3 = -TWO3 * ViscDens_j * Edge_Vector[2]/dist_ij_2;
//      dSigmayy_phi1 = -TWO3 * ViscDens_j * Edge_Vector[0]/dist_ij_2;
//      dSigmayy_phi2 = FOUR3 * ViscDens_j * Edge_Vector[1]/dist_ij_2;
//      dSigmayy_phi3 = -TWO3 * ViscDens_j * Edge_Vector[2]/dist_ij_2;
//      dSigmazz_phi1 = -TWO3 * ViscDens_j * Edge_Vector[0]/dist_ij_2;
//      dSigmazz_phi2 = -TWO3 * ViscDens_j * Edge_Vector[1]/dist_ij_2;
//      dSigmazz_phi3 = FOUR3 * ViscDens_j * Edge_Vector[2]/dist_ij_2;
//      dSigmaxy_phi1 = ViscDens_j * Edge_Vector[1]/dist_ij_2;
//      dSigmaxy_phi2 = ViscDens_j * Edge_Vector[0]/dist_ij_2;
//      dSigmaxy_phi3 = 0;
//      dSigmaxz_phi1 = ViscDens_j * Edge_Vector[2]/dist_ij_2;
//      dSigmaxz_phi2 = 0;
//      dSigmaxz_phi3 = ViscDens_j * Edge_Vector[0]/dist_ij_2;
//      dSigmayz_phi1 = 0;
//      dSigmayz_phi2 = ViscDens_j * Edge_Vector[2]/dist_ij_2;
//      dSigmayz_phi3 = ViscDens_j * Edge_Vector[1]/dist_ij_2;
//      
//      dSigmaxx5_psi5 = ViscDens_j * ( FOUR3*Velocity_j[0]*Edge_Vector[0] -  TWO3*Velocity_j[1]*Edge_Vector[1] -  TWO3*Velocity_j[2]*Edge_Vector[2])/dist_ij_2;
//      dSigmayy5_psi5 = ViscDens_j * (- TWO3*Velocity_j[0]*Edge_Vector[0] + FOUR3*Velocity_j[1]*Edge_Vector[1] -  TWO3*Velocity_j[2]*Edge_Vector[2])/dist_ij_2;
//      dSigmazz5_psi5 = ViscDens_j * (- TWO3*Velocity_j[0]*Edge_Vector[0] -  TWO3*Velocity_j[1]*Edge_Vector[1] + FOUR3*Velocity_j[2]*Edge_Vector[2])/dist_ij_2;
//      dSigmaxy5_psi5 = ViscDens_j * ( Velocity_j[0]*Edge_Vector[1] + Velocity_j[1]*Edge_Vector[0] )/dist_ij_2;
//      dSigmaxz5_psi5 = ViscDens_j * ( Velocity_j[0]*Edge_Vector[2] + Velocity_j[2]*Edge_Vector[0] )/dist_ij_2;
//      dSigmayz5_psi5 = ViscDens_j * ( Velocity_j[1]*Edge_Vector[2] + Velocity_j[2]*Edge_Vector[1] )/dist_ij_2;
//      dSigma5_psi5   = XiDens_j * ( Edge_Vector[0]*Normal[0] + Edge_Vector[1]*Normal[1] + Edge_Vector[2]*Normal[2] )/dist_ij_2;
//      
//      val_Jacobian_jj[0][0] = 0;
//      val_Jacobian_jj[0][1] = -( Velocity_j[0]*Normal[0]*dSigmaxx_phi1 + Velocity_j[1]*Normal[1]*dSigmayy_phi1 + Velocity_j[2]*Normal[2]*dSigmazz_phi1
//                                + (Velocity_j[0]*Normal[1] + Velocity_j[1]*Normal[0])*dSigmaxy_phi1
//                                + (Velocity_j[0]*Normal[2] + Velocity_j[2]*Normal[0])*dSigmaxz_phi1
//                                + (Velocity_j[2]*Normal[1] + Velocity_j[1]*Normal[2])*dSigmayz_phi1 );
//      val_Jacobian_jj[0][2] = -( Velocity_j[0]*Normal[0]*dSigmaxx_phi2 + Velocity_j[1]*Normal[1]*dSigmayy_phi2 + Velocity_j[2]*Normal[2]*dSigmazz_phi2
//                                + (Velocity_j[0]*Normal[1] + Velocity_j[1]*Normal[0])*dSigmaxy_phi2
//                                + (Velocity_j[0]*Normal[2] + Velocity_j[2]*Normal[0])*dSigmaxz_phi2
//                                + (Velocity_j[2]*Normal[1] + Velocity_j[1]*Normal[2])*dSigmayz_phi2 );
//      val_Jacobian_jj[0][3] = -( Velocity_j[0]*Normal[0]*dSigmaxx_phi3 + Velocity_j[1]*Normal[1]*dSigmayy_phi3 + Velocity_j[2]*Normal[2]*dSigmazz_phi3
//                                + (Velocity_j[0]*Normal[1] + Velocity_j[1]*Normal[0])*dSigmaxy_phi3
//                                + (Velocity_j[0]*Normal[2] + Velocity_j[2]*Normal[0])*dSigmaxz_phi3
//                                + (Velocity_j[2]*Normal[1] + Velocity_j[1]*Normal[2])*dSigmayz_phi3 );
//      val_Jacobian_jj[0][4] = (sq_vel_j - Pressure_j/(Density_j*Gamma_Minus_One)) * dSigma5_psi5;
//      
//      val_Jacobian_jj[1][0] = 0;
//      val_Jacobian_jj[1][1] = Normal[0]*dSigmaxx_phi1 + Normal[1]*dSigmaxy_phi1 + Normal[2]*dSigmaxz_phi1;
//      val_Jacobian_jj[1][2] = Normal[0]*dSigmaxx_phi2 + Normal[1]*dSigmaxy_phi2 + Normal[2]*dSigmaxz_phi2;
//      val_Jacobian_jj[1][3] = Normal[0]*dSigmaxx_phi3 + Normal[1]*dSigmaxy_phi3 + Normal[2]*dSigmaxz_phi3;
//      val_Jacobian_jj[1][4] = -Velocity_j[0]*dSigma5_psi5;
//      
//      val_Jacobian_jj[2][0] = 0;
//      val_Jacobian_jj[2][1] = Normal[0]*dSigmaxy_phi1 + Normal[1]*dSigmayy_phi1 + Normal[2]*dSigmayz_phi1;
//      val_Jacobian_jj[2][2] = Normal[0]*dSigmaxy_phi2 + Normal[1]*dSigmayy_phi2 + Normal[2]*dSigmayz_phi2;
//      val_Jacobian_jj[2][3] = Normal[0]*dSigmaxy_phi3 + Normal[1]*dSigmayy_phi3 + Normal[2]*dSigmayz_phi3;
//      val_Jacobian_jj[2][4] = -Velocity_j[1]*dSigma5_psi5;
//      
//      val_Jacobian_jj[3][0] = 0;
//      val_Jacobian_jj[3][1] = Normal[0]*dSigmaxz_phi1 + Normal[1]*dSigmayz_phi1 + Normal[2]*dSigmazz_phi1;
//      val_Jacobian_jj[3][2] = Normal[0]*dSigmaxz_phi2 + Normal[1]*dSigmayz_phi2 + Normal[2]*dSigmazz_phi2;
//      val_Jacobian_jj[3][3] = Normal[0]*dSigmaxz_phi3 + Normal[1]*dSigmayz_phi3 + Normal[2]*dSigmazz_phi3;
//      val_Jacobian_jj[3][4] = -Velocity_j[2]*dSigma5_psi5;
//      
//      val_Jacobian_jj[4][0] = 0;
//      val_Jacobian_jj[4][1] = 0;
//      val_Jacobian_jj[4][2] = 0;
//      val_Jacobian_jj[4][3] = 0;
//      val_Jacobian_jj[4][4] = dSigma5_psi5;
//      
//      for (iVar = 0; iVar < nVar; iVar++)
//        for (jVar = 0; jVar < nVar; jVar++)
//          val_Jacobian_ji[iVar][jVar] = -val_Jacobian_jj[iVar][jVar];
//    }
//    
//	} else if (nDim == 2) {
//		/*--- Residual at iPoint ---*/
//		Sigma_xx = ViscDens_i * (FOUR3 * Mean_GradPhi[0][0] -  TWO3 * Mean_GradPhi[1][1]);
//		Sigma_yy = ViscDens_i * (-TWO3 * Mean_GradPhi[0][0] + FOUR3 * Mean_GradPhi[1][1]);
//		Sigma_xy = ViscDens_i * (Mean_GradPhi[1][0] + Mean_GradPhi[0][1]);
//		Sigma_xx5 = ViscDens_i * ( FOUR3 * Velocity_i[0] * Mean_GradPsiE[0] -  TWO3 * Velocity_i[1] * Mean_GradPsiE[1]);
//		Sigma_yy5 = ViscDens_i * (- TWO3 * Velocity_i[0] * Mean_GradPsiE[0] + FOUR3 * Velocity_i[1] * Mean_GradPsiE[1]);
//		Sigma_xy5 = ViscDens_i * (Velocity_i[0] * Mean_GradPsiE[1] + Velocity_i[1] * Mean_GradPsiE[0]);
//		Sigma_5   = XiDens_i * dPhiE_dn;
//		eta_xx = Sigma_xx + Sigma_xx5; eta_yy = Sigma_yy + Sigma_yy5; eta_xy = Sigma_xy + Sigma_xy5;
//    
//		val_residual_i[0] = - (Velocity_i[0] * Normal[0] * eta_xx  + Velocity_i[1] * Normal[1] * eta_yy
//                           + (Velocity_i[0] * Normal[1] + Velocity_i[1] * Normal[0]) * eta_xy
//                           - (sq_vel_i - Pressure_i/(Density_i*Gamma_Minus_One)) * Sigma_5);
//		val_residual_i[1] = (eta_xx * Normal[0] + eta_xy * Normal[1] - Velocity_i[0] * Sigma_5);
//		val_residual_i[2] = (eta_xy * Normal[0] + eta_yy * Normal[1] - Velocity_i[1] * Sigma_5);
//		val_residual_i[3] = (Sigma_5);
//    
//		/*--- Computation of the Jacobians at Point i---*/
//		if (implicit) {
//      
//      dSigmaxx_phi1 = -FOUR3 * ViscDens_i * Edge_Vector[0]/dist_ij_2;
//      dSigmaxx_phi2 =   TWO3 * ViscDens_i * Edge_Vector[1]/dist_ij_2;
//      dSigmayy_phi1 =   TWO3 * ViscDens_i * Edge_Vector[0]/dist_ij_2;
//      dSigmayy_phi2 = -FOUR3 * ViscDens_i * Edge_Vector[1]/dist_ij_2;
//      dSigmaxy_phi1 = -ViscDens_i * Edge_Vector[1]/dist_ij_2;
//      dSigmaxy_phi2 = -ViscDens_i * Edge_Vector[0]/dist_ij_2;
//      
//      dSigmaxx5_psi5 = -ViscDens_i * ( FOUR3*Velocity_i[0]*Edge_Vector[0] -  TWO3*Velocity_i[1]*Edge_Vector[1] )/dist_ij_2;
//      dSigmayy5_psi5 = -ViscDens_i * (- TWO3*Velocity_i[0]*Edge_Vector[0] + FOUR3*Velocity_i[1]*Edge_Vector[1] )/dist_ij_2;
//      dSigmaxy5_psi5 = -ViscDens_i * ( Velocity_i[0]*Edge_Vector[1] + Velocity_i[1]*Edge_Vector[0] )/dist_ij_2;
//      dSigma5_psi5   = -XiDens_i * ( Edge_Vector[0]*Normal[0] + Edge_Vector[1]*Normal[1] )/dist_ij_2;
//      
//      val_Jacobian_ii[0][0] = 0;
//      val_Jacobian_ii[0][1] = -( Velocity_i[0]*Normal[0]*dSigmaxx_phi1 + Velocity_i[1]*Normal[1]*dSigmayy_phi1
//                                + (Velocity_i[0]*Normal[1] + Velocity_i[1]*Normal[0])*dSigmaxy_phi1 );
//      val_Jacobian_ii[0][2] = -( Velocity_i[0]*Normal[0]*dSigmaxx_phi2 + Velocity_i[1]*Normal[1]*dSigmayy_phi2
//                                + (Velocity_i[0]*Normal[1] + Velocity_i[1]*Normal[0])*dSigmaxy_phi2 );
//      val_Jacobian_ii[0][3] = (sq_vel_i - Pressure_i/(Density_i*Gamma_Minus_One)) * dSigma5_psi5;
//      
//      val_Jacobian_ii[1][0] = 0;
//      val_Jacobian_ii[1][1] = Normal[0]*dSigmaxx_phi1 + Normal[1]*dSigmaxy_phi1;
//      val_Jacobian_ii[1][2] = Normal[0]*dSigmaxx_phi2 + Normal[1]*dSigmaxy_phi2;
//      val_Jacobian_ii[1][3] = -Velocity_i[0]*dSigma5_psi5;
//      
//      val_Jacobian_ii[2][0] = 0;
//      val_Jacobian_ii[2][1] = Normal[0]*dSigmaxy_phi1 + Normal[1]*dSigmayy_phi1;
//      val_Jacobian_ii[2][2] = Normal[0]*dSigmaxy_phi2 + Normal[1]*dSigmayy_phi2;
//      val_Jacobian_ii[2][3] = -Velocity_i[1]*dSigma5_psi5;
//      
//      val_Jacobian_ii[3][0] = 0;
//      val_Jacobian_ii[3][1] = 0;
//      val_Jacobian_ii[3][2] = 0;
//      val_Jacobian_ii[3][3] = dSigma5_psi5;
//      
//      for (iVar = 0; iVar < nVar; iVar++)
//        for (jVar = 0; jVar < nVar; jVar++)
//          val_Jacobian_ij[iVar][jVar] = -val_Jacobian_ii[iVar][jVar];
//    }
//    
//		/*--- Residual at jPoint ---*/
//		Sigma_xx = ViscDens_j * (FOUR3 * Mean_GradPhi[0][0] -  TWO3 * Mean_GradPhi[1][1]);
//		Sigma_yy = ViscDens_j * (-TWO3 * Mean_GradPhi[0][0] + FOUR3 * Mean_GradPhi[1][1]);
//		Sigma_xy = ViscDens_j * (Mean_GradPhi[1][0] + Mean_GradPhi[0][1]);
//		Sigma_xx5 = ViscDens_j * ( FOUR3 * Velocity_j[0] * Mean_GradPsiE[0] -  TWO3 * Velocity_j[1] * Mean_GradPsiE[1]);
//		Sigma_yy5 = ViscDens_j * (- TWO3 * Velocity_j[0] * Mean_GradPsiE[0] + FOUR3 * Velocity_j[1] * Mean_GradPsiE[1]);
//		Sigma_xy5 = ViscDens_j * (Velocity_j[0] * Mean_GradPsiE[1] + Velocity_j[1] * Mean_GradPsiE[0]);
//		Sigma_5   = XiDens_j * dPhiE_dn;
//		eta_xx = Sigma_xx + Sigma_xx5; eta_yy = Sigma_yy + Sigma_yy5; eta_xy = Sigma_xy + Sigma_xy5;
//    
//		val_residual_j[0] = - (Velocity_j[0] * Normal[0] * eta_xx  + Velocity_j[1] * Normal[1] * eta_yy
//                           + (Velocity_j[0] * Normal[1] + Velocity_j[1] * Normal[0]) * eta_xy
//                           - (sq_vel_j - Pressure_j/(Density_j*Gamma_Minus_One)) * Sigma_5);
//		val_residual_j[1] = (eta_xx * Normal[0] + eta_xy * Normal[1]  - Velocity_j[0] * Sigma_5);
//		val_residual_j[2] = (eta_xy * Normal[0] + eta_yy * Normal[1]  - Velocity_j[1] * Sigma_5);
//		val_residual_j[3] = (Sigma_5);
//    
//		/*--- Computation of the Jacobians at Point j---*/
//    if (implicit) {
//      dSigmaxx_phi1 = FOUR3 * ViscDens_j * Edge_Vector[0]/dist_ij_2;
//      dSigmaxx_phi2 = -TWO3 * ViscDens_j * Edge_Vector[1]/dist_ij_2;
//      dSigmayy_phi1 = -TWO3 * ViscDens_j * Edge_Vector[0]/dist_ij_2;
//      dSigmayy_phi2 = FOUR3 * ViscDens_j * Edge_Vector[1]/dist_ij_2;
//      dSigmaxy_phi1 = ViscDens_j * Edge_Vector[1]/dist_ij_2;
//      dSigmaxy_phi2 = ViscDens_j * Edge_Vector[0]/dist_ij_2;
//      
//      dSigmaxx5_psi5 = ViscDens_j * ( FOUR3*Velocity_j[0]*Edge_Vector[0] -  TWO3*Velocity_j[1]*Edge_Vector[1] )/dist_ij_2;
//      dSigmayy5_psi5 = ViscDens_j * (- TWO3*Velocity_j[0]*Edge_Vector[0] + FOUR3*Velocity_j[1]*Edge_Vector[1] )/dist_ij_2;
//      dSigmaxy5_psi5 = ViscDens_j * ( Velocity_j[0]*Edge_Vector[1] + Velocity_j[1]*Edge_Vector[0] )/dist_ij_2;
//      dSigma5_psi5   = XiDens_j * ( Edge_Vector[0]*Normal[0] + Edge_Vector[1]*Normal[1] )/dist_ij_2;
//      
//      val_Jacobian_jj[0][0] = 0;
//      val_Jacobian_jj[0][1] = -( Velocity_j[0]*Normal[0]*dSigmaxx_phi1 + Velocity_j[1]*Normal[1]*dSigmayy_phi1
//                                + (Velocity_j[0]*Normal[1] + Velocity_j[1]*Normal[0])*dSigmaxy_phi1 );
//      val_Jacobian_jj[0][2] = -( Velocity_j[0]*Normal[0]*dSigmaxx_phi2 + Velocity_j[1]*Normal[1]*dSigmayy_phi2
//                                + (Velocity_j[0]*Normal[1] + Velocity_j[1]*Normal[0])*dSigmaxy_phi2 );
//      val_Jacobian_jj[0][3] = (sq_vel_j - Pressure_j/(Density_j*Gamma_Minus_One)) * dSigma5_psi5;
//      
//      val_Jacobian_jj[1][0] = 0;
//      val_Jacobian_jj[1][1] = Normal[0]*dSigmaxx_phi1 + Normal[1]*dSigmaxy_phi1;
//      val_Jacobian_jj[1][2] = Normal[0]*dSigmaxx_phi2 + Normal[1]*dSigmaxy_phi2;
//      val_Jacobian_jj[1][3] = -Velocity_j[0]*dSigma5_psi5;
//      
//      val_Jacobian_jj[2][0] = 0;
//      val_Jacobian_jj[2][1] = Normal[0]*dSigmaxy_phi1 + Normal[1]*dSigmayy_phi1;
//      val_Jacobian_jj[2][2] = Normal[0]*dSigmaxy_phi2 + Normal[1]*dSigmayy_phi2;
//      val_Jacobian_jj[2][3] = -Velocity_j[1]*dSigma5_psi5;
//      
//      val_Jacobian_jj[3][0] = 0;
//      val_Jacobian_jj[3][1] = 0;
//      val_Jacobian_jj[3][2] = 0;
//      val_Jacobian_jj[3][3] = dSigma5_psi5;
//      
//      for (iVar = 0; iVar < nVar; iVar++)
//        for (jVar = 0; jVar < nVar; jVar++)
//          val_Jacobian_ji[iVar][jVar] = -val_Jacobian_jj[iVar][jVar];
//    }
//	}
}

CSource_AdjTNE2::CSource_AdjTNE2(unsigned short val_nDim,
                                 unsigned short val_nVar,
                                 unsigned short val_nPrimVar,
                                 unsigned short val_nPrimVarGrad,
                                 CConfig *config) : CNumerics(val_nDim,
                                                              val_nVar,
                                                              config) {
  
  unsigned short iDim, iVar;
  
  /*--- Assign booleans from CConfig ---*/
  implicit   = (config->GetKind_TimeIntScheme_AdjTNE2() == EULER_IMPLICIT);
  
  /*--- Set array sizes ---*/
  nDim         = val_nDim;
  nSpecies     = config->GetnSpecies();
  nVar         = val_nVar;
  nPrimVar     = val_nPrimVar;
  nPrimVarGrad = val_nPrimVarGrad;
  
  rhos = new double[nSpecies];
  vel = new double[nDim];
  
  GInvRho  = new double[nDim];
  GVeloRho = new double*[nDim];
  GPhiGInvRho = new double[nDim];
  GPsiEZetaTau = new double[nDim];
  tau      = new double*[nDim];
  eta      = new double*[nDim];
  pi       = new double*[nDim];
  zeta     = new double*[nDim];
  for (iDim = 0; iDim < nDim; iDim++) {
    GVeloRho[iDim] = new double[nDim];
    tau[iDim]      = new double[nDim];
    eta[iDim]      = new double[nDim];
    pi[iDim]       = new double[nDim];
    zeta[iDim]     = new double[nDim];
  }
  Av2 = new double *[nVar];
  Av3 = new double *[nVar];
  Av4 = new double *[nVar];
  for (iVar = 0; iVar < nVar; iVar++) {
    Av2[iVar] = new double[nVar];
    Av3[iVar] = new double[nVar];
    Av4[iVar] = new double[nVar];
  }
  
  
//	unsigned short iDim;
//  
//	Velocity = new double [nVar];
//	GradDensity = new double [nDim];
//	GradInvDensity = new double [nDim];
//	dPoDensity2 = new double [nDim];
//	alpha = new double [nDim];
//	beta = new double [nDim];
//	Sigma_5_vec = new double [nDim];
//  
//	GradVel_o_Rho = new double* [nDim];
//	sigma = new double* [nDim];
//	Sigma_phi = new double* [nDim];
//	Sigma_5_Tensor = new double* [nDim];
//	Sigma = new double* [nDim];
//	for (iDim = 0; iDim < nDim; iDim++) {
//		GradVel_o_Rho[iDim] = new double [nDim];
//		sigma[iDim] = new double [nDim];
//		Sigma_phi[iDim] = new double [nDim];
//		Sigma_5_Tensor[iDim] = new double [nDim];
//		Sigma[iDim] = new double [nDim];
//	}
}

CSource_AdjTNE2::~CSource_AdjTNE2(void) {

  unsigned short iDim, iVar;
  
  delete [] rhos;
  delete [] vel;
  delete [] GInvRho;
  delete [] GPhiGInvRho;
  delete [] GPsiEZetaTau;
  for (iDim = 0; iDim < nDim; iDim++) {
    delete [] GVeloRho[iDim];
    delete [] tau[iDim];
    delete [] eta[iDim];
    delete [] pi[iDim];
    delete [] zeta[iDim];
  }
  delete [] GVeloRho;
  delete [] tau;
  delete [] eta;
  delete [] pi;
  delete [] zeta;
  for (iVar = 0; iVar < nVar; iVar++) {
    delete [] Av2[iVar];
    delete [] Av3[iVar];
    delete [] Av4[iVar];
  }
  delete [] Av2;
  delete [] Av3;
  delete [] Av4;
  
  
//	for (iDim = 0; iDim < nDim; iDim++) {
//		delete [] GradVel_o_Rho[iDim];
//		delete [] sigma[iDim];
//		delete [] Sigma_phi[iDim];
//		delete [] Sigma_5_Tensor[iDim];
//		delete [] Sigma[iDim];
//	}
//	delete [] GradVel_o_Rho;
//	delete [] sigma;
//	delete [] Sigma_phi;
//	delete [] Sigma_5_Tensor;
//	delete [] Sigma;
//  
//	delete [] Velocity;
//	delete [] GradDensity;
//	delete [] GradInvDensity;
//	delete [] dPoDensity2;
//	delete [] alpha;
//	delete [] beta;
//	delete [] Sigma_5_vec;
}

void CSource_AdjTNE2::ComputeSourceViscous (double *val_residual, CConfig *config) {
  
	unsigned short iDim, jDim, iSpecies, iVar, jVar;
  double rho, sqvel, rhoCvtr, rhoCvve, Cvtrs, Cvves, *Ms, *xi, Ru;
  double div_vel, div_velorho, velGInvRho, GPhiEta, GPsiEvelPi;
  double mu2, mu3, mu4;
  double **GPsi;
  
  /*--- Initialize arrays ---*/
  for (iVar = 0; iVar < nVar; iVar++) {
    val_residual[iVar] = 0.0;
    for (jVar = 0; jVar < nVar; jVar++) {
      Av2[iVar][jVar] = 0.0;
      Av3[iVar][jVar] = 0.0;
      Av4[iVar][jVar] = 0.0;
    }
  }
  
  /*--- Rename for convenience ---*/
  Ms      = config->GetMolar_Mass();
  xi      = config->GetRotationModes();
  Ru      = UNIVERSAL_GAS_CONSTANT;
  rhoCvtr = V_i[RHOCVTR_INDEX];
  rhoCvve = V_i[RHOCVVE_INDEX];
  GPsi    = PsiVar_Grad_i;
  
  /*--- Assign useful values ---*/
  // Mixture & species density
  rho = V_i[RHO_INDEX];
  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++)
    rhos[iSpecies] = V_i[RHOS_INDEX+iSpecies];
  // Velocity
  sqvel = 0.0;
  div_vel = 0.0;
  for (iDim = 0; iDim < nDim; iDim++) {
		vel[iDim] = V_i[VEL_INDEX+iDim];
		sqvel += vel[iDim]*vel[iDim];
    div_vel += PrimVar_Grad_i[VEL_INDEX+iDim][iDim];
	}
  // Transport coefficients
  mu2 = Laminar_Viscosity_i;
  mu3 = Thermal_Conductivity_i;
  mu4 = Thermal_Conductivity_ve_i;

  /*--- Gradients of flow variables ---*/
  for (iDim = 0; iDim < nDim; iDim++) {
    GInvRho[iDim] = -1.0/(rho*rho)*PrimVar_Grad_i[RHO_INDEX][iDim];
    for (jDim = 0; jDim < nDim; jDim++)
      GVeloRho[iDim][jDim] = -vel[iDim]/(rho*rho)*(PrimVar_Grad_i[RHO_INDEX][jDim])
                           +  1.0/rho*(PrimVar_Grad_i[VEL_INDEX+iDim][jDim]);
  }
  div_velorho = 0.0;
  velGInvRho = 0.0;
  for (iDim = 0; iDim < nDim; iDim++) {
    div_velorho += GVeloRho[iDim][iDim];
    velGInvRho += vel[iDim]*GInvRho[iDim];
  }
  
  /*--- Supporting matrices ---*/
  // Tau: Stress tensor matrix
  for (iDim = 0; iDim < nDim; iDim++) {
    for (jDim = 0; jDim < nDim; jDim++) {
      tau[iDim][jDim] = PrimVar_Grad_i[VEL_INDEX+iDim][jDim] +
                        PrimVar_Grad_i[VEL_INDEX+jDim][iDim];
    }
    tau[iDim][iDim] -= 2.0/3.0*div_vel;
  }
  // Eta
  for (iDim = 0; iDim < nDim; iDim++) {
    for (jDim = 0; jDim < nDim; jDim++) {
      eta[iDim][jDim] = GVeloRho[iDim][jDim] + GVeloRho[jDim][iDim];
    }
    eta[iDim][iDim] -= 2.0/3.0*div_velorho;
  }
  // Pi
  for (iDim = 0; iDim < nDim; iDim++) {
    for (jDim = 0; jDim < nDim; jDim++) {
      pi[iDim][jDim] = eta[iDim][jDim] - 1.0/rho*tau[iDim][jDim];
    }
  }
  // Zeta
  for (iDim = 0; iDim < nDim; iDim++) {
    for (jDim = 0; jDim < nDim; jDim++) {
      zeta[iDim][jDim] = vel[jDim]*GInvRho[iDim] - 2.0/3.0*vel[iDim]*GInvRho[jDim];
    }
  }
  
  /*--- Calculate supporting quantities ---*/
  // GradPhi dot GradInvRho - e.g. (diPhix*di(1/rho), diPhiy*di(1/rho), ... )
  for (iDim = 0; iDim < nDim; iDim++) {
    GPhiGInvRho[iDim] = 0.0;
    for (jDim = 0; jDim < nDim; jDim++) {
      GPhiGInvRho[iDim] += GPsi[nSpecies+iDim][jDim]*GInvRho[jDim];
    }
  }
  // (di(PsiE)*(Zetai1 + 1/rho*Taui1), di(PsiE)*(Zetai2 + 1/rho*Taui2), ... )
  for (iDim = 0; iDim < nDim; iDim++) {
    GPsiEZetaTau[iDim] = 0.0;
    for (jDim = 0; jDim < nDim; jDim++) {
      GPsiEZetaTau[iDim] += GPsi[nSpecies+nDim][jDim] * (zeta[jDim][iDim] +
                                                         1/rho*tau[jDim][iDim]);
    }
  }
  // GPhi:Eta
  GPhiEta = 0.0;
  for (iDim = 0; iDim < nDim; iDim++)
    for (jDim = 0; jDim < nDim; jDim++)
      GPhiEta += GPsi[nSpecies+iDim][jDim]*eta[jDim][iDim];
  // GPsiE_i*(u_j*pi_ij)
  GPsiEvelPi = 0.0;
  for (iDim = 0; iDim < nDim; iDim++)
    for (jDim = 0; jDim < nDim; jDim++)
      GPsiEvelPi += GPsi[nSpecies+nDim][iDim]*vel[jDim]*pi[iDim][jDim];
  
  /*--- Contribution to viscous residual from Av2 ---*/
  // Mass conservation
  for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
    val_residual[iSpecies] = (-GPhiEta + GPsiEvelPi)*mu2*Volume;
  }
  // X-momentum
  val_residual[nSpecies]   = ((GPhiGInvRho[0] + 1.0/3.0*GPsi[nSpecies][0]*GInvRho[0]) +
                              (GPsi[nSpecies+1][0]*GInvRho[1] - 2.0/3.0*GPsi[nSpecies+1][1]*GInvRho[0]) +
                              (GPsi[nSpecies+2][0]*GInvRho[2] - 2.0/3.0*GPsi[nSpecies+2][2]*GInvRho[0]) +
                              (GPsi[nSpecies+3][0]*velGInvRho + GPsiEZetaTau[0]))*mu2*Volume;
  // Y-momentum
  val_residual[nSpecies+1] = ((GPsi[nSpecies][1]*GInvRho[0]   - 2.0/3.0*GPsi[nSpecies][0]*GInvRho[1]) +
                              (GPhiGInvRho[1] + 1.0/3.0*GPsi[nSpecies+1][1]*GInvRho[1]) +
                              (GPsi[nSpecies+2][1]*GInvRho[2] - 2.0/3.0*GPsi[nSpecies+2][2]*GInvRho[1]) +
                              (GPsi[nSpecies+3][1]*velGInvRho + GPsiEZetaTau[1]))*mu2*Volume;
  // Z-momentum
  val_residual[nSpecies+2] = ((GPsi[nSpecies][2]*GInvRho[0]   - 2.0/3.0*GPsi[nSpecies][0]*GInvRho[2]) +
                              (GPsi[nSpecies+1][2]*GInvRho[1] - 2.0/3.0*GPsi[nSpecies+1][1]*GInvRho[2]) +
                              (GPhiGInvRho[2] + 1.0/3.0*GPsi[nSpecies+2][2]*GInvRho[2]) +
                              (GPsi[nSpecies+3][2]*velGInvRho + GPsiEZetaTau[2]))*mu2*Volume;
  
  
  /*--- Contribution to viscous residual from Av3 ---*/
  for (iDim = 0; iDim < nDim; iDim++) {
    // Mass conservation
    for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
      Cvtrs = (3.0/2.0 + xi[iSpecies]/2.0)*Ru/Ms[iSpecies];
      
      val_residual[iSpecies] += GPsi[nSpecies+3][iDim] * (1/rhoCvtr * (vel[0]*PrimVar_Grad_i[VEL_INDEX][iDim]   +
                                                                       vel[1]*PrimVar_Grad_i[VEL_INDEX+1][iDim] +
                                                                       vel[2]*PrimVar_Grad_i[VEL_INDEX+2][iDim] -
                                                                       Cvtrs*PrimVar_Grad_i[T_INDEX][iDim])
                                                          -dTdU_i[iSpecies]/rhoCvtr *
                                                          PrimVar_Grad_i[RHOCVTR_INDEX][iDim])* mu3 * Volume;
    }
    // X-momentum
    val_residual[nSpecies]   += GPsi[nSpecies+3][iDim] * (-1/rhoCvtr*PrimVar_Grad_i[VEL_INDEX][iDim]
                                                          -dTdU_i[nSpecies]/rhoCvtr *
                                                          PrimVar_Grad_i[RHOCVTR_INDEX][iDim]) * mu3 * Volume;
    // Y-momentum
    val_residual[nSpecies+1] += GPsi[nSpecies+3][iDim] * (-1/rhoCvtr*PrimVar_Grad_i[VEL_INDEX+1][iDim]
                                                          -dTdU_i[nSpecies+1]/rhoCvtr *
                                                          PrimVar_Grad_i[RHOCVTR_INDEX][iDim]) * mu3 * Volume;
    // Z-momentum
    val_residual[nSpecies+2] += GPsi[nSpecies+3][iDim] * (-1/rhoCvtr*PrimVar_Grad_i[VEL_INDEX+2][iDim]
                                                          -dTdU_i[nSpecies+2]/rhoCvtr *
                                                          PrimVar_Grad_i[RHOCVTR_INDEX][iDim]) * mu3 * Volume;
    // Energy
    val_residual[nSpecies+3] += GPsi[nSpecies+3][iDim] * (-dTdU_i[nSpecies+3]/rhoCvtr *
                                                          PrimVar_Grad_i[RHOCVTR_INDEX][iDim]) * mu3 * Volume;
    // Vib.-el. energy
    val_residual[nSpecies+4] += GPsi[nSpecies+3][iDim] * (-dTdU_i[nSpecies+4]/rhoCvtr *
                                                          PrimVar_Grad_i[RHOCVTR_INDEX][iDim]) * mu3 * Volume;
  }
  
  /*--- Contribution to viscous residual from Av4 ---*/
  for (iDim = 0; iDim < nDim; iDim++) {
    for (iSpecies = 0; iSpecies < nSpecies; iSpecies++) {
      Cvves = var->CalcCvve(V_i[TVE_INDEX], config, iSpecies);
      val_residual[iSpecies] += ((GPsi[nSpecies+nDim][iDim]+GPsi[nSpecies+nDim+1][iDim]) *
                                 (-Cvves/rhoCvve*PrimVar_Grad_i[TVE_INDEX][iDim]
                                  -dTvedU_i[iSpecies]/rhoCvve*PrimVar_Grad_i[RHOCVVE_INDEX][iDim])) *
                                mu4 * Volume;
    }
    val_residual[nSpecies+nDim+1] += ((GPsi[nSpecies+nDim][iDim]+GPsi[nSpecies+nDim+1][iDim]) *
                                      (-dTvedU_i[nSpecies+nDim+1]/rhoCvve * PrimVar_Grad_i[RHOCVVE_INDEX][iDim])) *
                                     mu4 * Volume;
    
  }
}

//CSourceConservative_AdjFlow::CSourceConservative_AdjFlow(unsigned short val_nDim, unsigned short val_nVar, CConfig *config) : CNumerics(val_nDim, val_nVar, config) {
//  
//	Gamma = config->GetGamma();
//	Gamma_Minus_One = Gamma - 1.0;
//  
//	Velocity = new double [nDim];
//	Residual_i = new double [nVar];
//	Residual_j = new double [nVar];
//	Mean_Residual = new double [nVar];
//  
//	Mean_PrimVar_Grad = new double* [nVar];
//	for (unsigned short iVar = 0; iVar < nVar; iVar++)
//		Mean_PrimVar_Grad[iVar] = new double [nDim];
//}
//
//CSourceConservative_AdjFlow::~CSourceConservative_AdjFlow(void) {
//	delete [] Mean_Residual;
//	delete [] Residual_j;
//	delete [] Residual_i;
//	delete [] Velocity;
//  
//	for (unsigned short iVar = 0; iVar < nVar; iVar++)
//		delete [] Mean_PrimVar_Grad[iVar];
//	delete [] Mean_PrimVar_Grad;
//}

void CSource_AdjTNE2::ComputeSourceConservative (double *val_residual,
                                                 CConfig *config) {
//	unsigned short iDim, jDim, iVar;
//	double rho, nu, Ji, fv1, fv2, Omega, Shat, dist_sq, Ji_2, Ji_3, one_o_oneplusJifv1;
//	double r, g, g_6, glim, dfw_g, dg_r, dr_nuhat, dr_Shat, Ms_coeff, invOmega;
//  
//	/*--- CLOUSURE CONSTANTS ---*/
//	double cv1_3 = 7.1*7.1*7.1;
//	double k2 = 0.41*0.41;
//	double cb1 = 0.1355;
//	double cw2 = 0.3;
//	double cw3_6 = pow(2.0,6.0);
//	double sigma = 2./3.;
//	double cb2 = 0.622;
//	double cw1 = cb1/k2+(1+cb2)/sigma;
//  
//	for (iVar = 0; iVar < nVar; iVar++) {
//		Residual_i[iVar] = 0.0;
//		Residual_j[iVar] = 0.0;
//	}
//  
//	/*--- iPoint ---*/
//  
//	/*--- Density and velocities ---*/
//	rho = U_i[0];
//	for (iDim = 0; iDim < nDim; iDim++)
//		Velocity[iDim] = U_i[iDim+1]/rho;
//  
//	/*--- Vorticity ---*/
//	Omega = (PrimVar_Grad_i[1][1]-PrimVar_Grad_i[2][0])*(PrimVar_Grad_i[1][1]-PrimVar_Grad_i[2][0]);
//	if (nDim == 3) Omega += (PrimVar_Grad_i[1][2]-PrimVar_Grad_i[3][0])*(PrimVar_Grad_i[1][2]-PrimVar_Grad_i[3][0]) +
//    (PrimVar_Grad_i[2][2]-PrimVar_Grad_i[3][1])*(PrimVar_Grad_i[2][2]-PrimVar_Grad_i[3][1]);
//	Omega = sqrt(Omega);
//	invOmega = 1.0/(Omega + TURB_EPS);
//	//	invOmega = min(1.0/Omega, max_invOmega);
//  
//	/*--- Compute Ms_coeff -> coming from partial derivatives ---*/
//	Ms_coeff = 0.0;
//	if (dist_i > 0) {
//		dist_sq = dist_i*dist_i;
//		nu = Laminar_Viscosity_i/rho;
//		Ji = TurbVar_i[0]/nu;
//		Ji_2 = Ji*Ji;
//		Ji_3 = Ji_2*Ji;
//		fv1 = Ji_3/(Ji_3+cv1_3);
//		one_o_oneplusJifv1 = 1.0/(1.0+Ji*fv1);
//		fv2 = 1.0 - Ji*one_o_oneplusJifv1;
//		Shat = max(Omega + TurbVar_i[0]*fv2/(k2*dist_sq),TURB_EPS);
//    
//		r = min(TurbVar_i[0]/(Shat*k2*dist_sq),10.);
//		g = r + cw2*(pow(r,6.)-r);
//		g_6 = pow(g,6.);
//		glim = pow((1+cw3_6)/(g_6+cw3_6),1./6.);
//    
//		dfw_g  = glim*cw3_6/(g_6+cw3_6);
//		dg_r = 1.0 + cw2*(6.0*pow(r,5.0)-1.0);
//		dr_nuhat = 1.0/(Shat*k2*dist_sq);
//		dr_Shat = -dr_nuhat*TurbVar_i[0]/Shat;
//    
//		Ms_coeff = (cb1*TurbVar_i[0]-cw1*TurbVar_i[0]*TurbVar_i[0]/dist_sq*dfw_g*dg_r*dr_Shat);
//	}
//	Ms_coeff *= TurbPsi_i[0]*invOmega/rho;
//  
//	/*--- Compute residual of iPoint ---*/
//	for (iDim = 0; iDim < nDim; iDim++) {
//		for (jDim = 0; jDim < nDim; jDim++) {
//			Residual_i[0] -= Ms_coeff*(Velocity[jDim]*PrimVar_Grad_i[jDim+1][iDim]*Normal[iDim] -
//                                 Velocity[jDim]*PrimVar_Grad_i[iDim+1][jDim]*Normal[iDim]);
//			Residual_i[iDim+1] += Ms_coeff*(PrimVar_Grad_i[iDim+1][jDim]*Normal[jDim] -
//                                      PrimVar_Grad_i[jDim+1][iDim]*Normal[jDim]);
//		}
//	}
//  
//	/*--- jPoint ---*/
//  
//	/*--- Density and velocities ---*/
//	rho = U_j[0];
//	for (iDim = 0; iDim < nDim; iDim++)
//		Velocity[iDim] = U_j[iDim+1]/rho;
//  
//	/*--- Vorticity ---*/
//	Omega = (PrimVar_Grad_j[1][1]-PrimVar_Grad_j[2][0])*(PrimVar_Grad_j[1][1]-PrimVar_Grad_j[2][0]);
//	if (nDim == 3) Omega += (PrimVar_Grad_j[1][2]-PrimVar_Grad_j[3][0])*(PrimVar_Grad_j[1][2]-PrimVar_Grad_j[3][0]) +
//    (PrimVar_Grad_j[2][2]-PrimVar_Grad_j[3][1])*(PrimVar_Grad_j[2][2]-PrimVar_Grad_j[3][1]);
//	Omega = sqrt(Omega);
//	invOmega = 1.0/(Omega + TURB_EPS);
//	//	invOmega = min(1.0/Omega, max_invOmega);
//  
//	/*--- Compute Ms_coeff -> coming from partial derivatives ---*/
//	Ms_coeff = 0.0;
//	if (dist_j > 0) {
//		dist_sq = dist_j*dist_j;
//		nu = Laminar_Viscosity_j/rho;
//		Ji = TurbVar_j[0]/nu;
//		Ji_2 = Ji*Ji;
//		Ji_3 = Ji_2*Ji;
//		fv1 = Ji_3/(Ji_3+cv1_3);
//		one_o_oneplusJifv1 = 1.0/(1.0+Ji*fv1);
//		fv2 = 1.0 - Ji*one_o_oneplusJifv1;
//		Shat = max(Omega + TurbVar_j[0]*fv2/(k2*dist_sq),TURB_EPS);
//    
//		r = min(TurbVar_j[0]/(Shat*k2*dist_sq),10.);
//		g = r + cw2*(pow(r,6.)-r);
//		g_6 = pow(g,6.);
//		glim = pow((1+cw3_6)/(g_6+cw3_6),1./6.);
//    
//		dfw_g  = glim*cw3_6/(g_6+cw3_6);
//		dg_r = 1.0 + cw2*(6.0*pow(r,5.0)-1.0);
//		dr_nuhat = 1.0/(Shat*k2*dist_sq);
//		dr_Shat = -dr_nuhat*TurbVar_j[0]/Shat;
//    
//		Ms_coeff = (cb1*TurbVar_j[0]-cw1*TurbVar_j[0]*TurbVar_j[0]/dist_sq*dfw_g*dg_r*dr_Shat);
//	}
//	Ms_coeff *= TurbPsi_j[0]*invOmega/rho;
//  
//	/*--- Compute residual of jPoint ---*/
//	for (iDim = 0; iDim < nDim; iDim++) {
//		for (jDim = 0; jDim < nDim; jDim++) {
//			Residual_j[0] -= Ms_coeff*(Velocity[jDim]*PrimVar_Grad_j[jDim+1][iDim]*Normal[iDim] -
//                                 Velocity[jDim]*PrimVar_Grad_j[iDim+1][jDim]*Normal[iDim]);
//			Residual_j[iDim+1] += Ms_coeff*(PrimVar_Grad_j[iDim+1][jDim]*Normal[jDim] -
//                                      PrimVar_Grad_j[jDim+1][iDim]*Normal[jDim]);
//		}
//	}
//  
//	/*--- MEAN RESIDUAL ---*/
//	for (iVar = 0; iVar < nVar; iVar++)
//		val_residual[iVar] = 0.5*(Residual_i[iVar] + Residual_j[iVar]);
  
}
