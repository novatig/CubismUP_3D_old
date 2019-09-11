//
//  Cubism3D
//  Copyright (c) 2018 CSE-Lab, ETH Zurich, Switzerland.
//  Distributed under the terms of the MIT license.
//
//  Created by Hugues de Laroussilhe.
//

#include "SpectralIcGenerator.h"
#include "SpectralManip.h"

#include <random>
#include <iomanip>
#include <sstream>

CubismUP_3D_NAMESPACE_BEGIN
using namespace cubism;

SpectralIcGenerator::SpectralIcGenerator(SimulationData &s) : sim(s) {}

void SpectralIcGenerator::_generateTarget(std::vector<Real>& K,
                                          std::vector<Real>& E,
                                          SpectralManip& SM)
{
  if (sim.spectralIC=="cbc") {
    // Set-up Energy Spectrum from COMTE-BELLOT-CORRSIN experiment :
    // Wave numbers are given from experiment units [cm-1]
    //     => Need to convert to [m^-1] and non-dimensionalize with L_ref
    // Energy is given from experiment units [cm3/s2]
    //     => Need to convert to [m^3/s^2] and non-dimensionalize with L_ref and U0
    K = std::vector<Real> (19, 0.0);
    E = std::vector<Real> (19, 0.0);
    K[0]  =  0.20;  E[0]  = 129.0;
    K[1]  =  0.25;  E[1]  = 230.0;
    K[2]  =  0.30;  E[2]  = 322.0;
    K[3]  =  0.40;  E[3]  = 435.0;
    K[4]  =  0.50;  E[4]  = 457.0;
    K[5]  =  0.70;  E[5]  = 380.0;
    K[6]  =  1.00;  E[6]  = 270.0;
    K[7]  =  1.50;  E[7]  = 168.0;
    K[8]  =  2.00;  E[8]  = 120.0;
    K[9]  =  2.50;  E[9]  =  89.0;
    K[10] =  3.00;  E[10] =  70.3;
    K[11] =  4.00;  E[11] =  47.0;
    K[12] =  6.00;  E[12] =  24.7;
    K[13] =  8.00;  E[13] =  12.6;
    K[14] = 10.00;  E[14] =  7.42;
    K[15] = 12.50;  E[15] =  3.96;
    K[16] = 15.00;  E[16] =  2.33;
    K[17] = 17.50;  E[17] =  1.34;
    K[18] = 20.00;  E[18] =   0.8;
    const Real M = 5.08;

    const Real u_ref = std::sqrt(3.0/2)*22.2;
    const Real L_ref = 10.8*M;

    for (std::size_t idx = 0; idx < K.size(); idx++) {
      K[idx] *= L_ref * 0.01 * 2 * M_PI;
      E[idx] /= u_ref * u_ref * L_ref;
    }
  }

  else if (sim.spectralIC=="art") {
    // Initial spectrum from :
    // P. Sullivan, Neal & Mahalingam, Shankar & Kerr, Robert. (1994).
    // Deterministic forcing of homogeneous, isotropic turbulence.
    // Physics of Fluids. 6. 10.1063/1.868274.
    const Real k0   = sim.k0, tke0 = sim.tke0;
    const Real maxGridN = SM.maxGridN, maxGridL = SM.maxGridL;
    const int nBins = std::ceil(std::sqrt(3.0) * maxGridN / 2.0) + 1;
    const Real binSize = M_PI*std::sqrt(3.0) * maxGridN / (nBins * maxGridL);

    K = std::vector<Real> (nBins, 0.0);
    E = std::vector<Real> (nBins, 0.0);
    const Real coef = 32 * tke0 / (3*k0) * std::sqrt(2.0/M_PI);

    for (int i=0; i<nBins; i++) {
      K[i] = (i+0.5) * binSize;
      E[i] = coef * std::pow(K[i]/k0,4) * std::exp(-2*std::pow(K[i]/k0,2));
    }
  }

  else if (sim.spectralIC=="fromFile") {
    std::ifstream inFile;
    std::string fileName = sim.spectralICFile;
    inFile.open(fileName);
    if (!inFile){
      std::cout<<"SpectralICGenerator: cannot open file :"<<fileName<<std::endl;
      abort();
    }
    for(std::string line; std::getline(inFile, line); )
    {
      std::istringstream in(line);
      Real k_r, E_r;
      in >> k_r >> E_r;
      K.push_back(k_r);
      E.push_back(E_r);
    }
    EnergySpectrum target(K,E);

    // Set target tke
    Real k_eval = 0.0, tke0 = 0.0;
    for (int i = 0; i<SM.maxGridN; i++) {
      k_eval = (i+1) * 2*M_PI / SM.maxGridL;
      tke0  += target.interpE(k_eval);
    }

    // if user asked spectral forcing, but no value specified, satisfy spectrum
    if (sim.spectralForcing       &&
        sim.turbKinEn_target <= 0 &&
        sim.enInjectionRate  <= 0) sim.turbKinEn_target = tke0;
  }

  else if (sim.spectralIC=="fromFit") {
    const Real eps = sim.enInjectionRate;
    const Real maxGridN = SM.maxGridN, maxGridL = SM.maxGridL;
    const int nBins = std::ceil(std::sqrt(3.0) * maxGridN / 2.0) + 1;
    const Real binSize = M_PI*std::sqrt(3.0) * maxGridN / (nBins * maxGridL);

    K = std::vector<Real> (nBins, 0.0);
    E = std::vector<Real> (nBins, 0.0);

    const Real LintegralFit = 1.35793122 * std::pow(sim.nu, 1/6.0);
    const Real Lkolmogorov = std::pow(sim.nu, 0.75) * std::pow(eps, 0.25);
    const Real C  = 3.62590946e+00;
    const Real CI = 1.58981122e-04;
    const Real CE = 1.78026894e-01;
    const Real BE = 5.24030627e+00; // 5.2 from theory
    const Real P0 = 6.69070846e+03; // should be 2, but we force large scales

    for (int i=0; i<nBins; i++) {
      K[i] = (i+0.5) * binSize;
      const Real KI = K[i] * LintegralFit, KE4 = std::pow(K[i] * Lkolmogorov,4);
      const Real FL = std::pow(KI / std::sqrt(KI*KI + CI), 5/3.0 + P0 );
      const Real FE = std::exp(-BE*(std::pow(KE4 + std::pow(CE,4), 0.25 ) -CE));
      E[i] = C * std::pow(eps, 2/3.0) * std::pow(K[i], -5/3.0) * FL * FE;
    }
  }
}

void SpectralIcGenerator::_fftw2cub(const SpectralManip& SM) const
{
  const size_t NlocBlocks = SM.local_infos.size();
  #pragma omp parallel for schedule(static)
  for(size_t i=0; i<NlocBlocks; ++i) {
    BlockType& b = *(BlockType*) SM.local_infos[i].ptrBlock;
    const size_t offset = SM._offset( SM.local_infos[i] );
    for(int iz=0; iz<BlockType::sizeZ; iz++)
    for(int iy=0; iy<BlockType::sizeY; iy++)
    for(int ix=0; ix<BlockType::sizeX; ix++) {
      const size_t src_index = SM._dest(offset, iz, iy, ix);
      b(ix,iy,iz).u = SM.data_u[src_index];
      b(ix,iy,iz).v = SM.data_v[src_index];
      b(ix,iy,iz).w = SM.data_w[src_index];
    }
  }
}

void SpectralIcGenerator::run()
{
  SpectralManip* SM = initFFTWSpectralAnalysisSolver(sim);
  SM->prepareBwd();
  std::vector<Real> K, E;
  _generateTarget(K, E, * SM);
  SM->_compute_IC(K, E);
  SM->_compute_analysis();
  // if user asked spectral forcing, but no value specified, satisfy spectrum
  if (sim.spectralForcing       &&
      sim.turbKinEn_target <= 0 &&
      sim.enInjectionRate  <= 0 )
      sim.turbKinEn_target = SM->stats.tke;

  SM->runBwd();
  _fftw2cub(* SM);
  delete SM;
}

CubismUP_3D_NAMESPACE_END
