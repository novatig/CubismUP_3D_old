//
//  Cubism3D
//  Copyright (c) 2018 CSE-Lab, ETH Zurich, Switzerland.
//  Distributed under the terms of the MIT license.
//
//  Created by Hugues de Laroussilhe.
//

#include "SpectralManipFFTW.h"
#include "../poisson/PoissonSolver_common.h"

#ifndef CUP_SINGLE_PRECISION
#define MPIREAL MPI_DOUBLE
#else
#define MPIREAL MPI_FLOAT
#endif /* CUP_SINGLE_PRECISION */

CubismUP_3D_NAMESPACE_BEGIN
using namespace cubism;

static inline Real pow2_cplx(const fft_c cplx_val) {
  return pow2(cplx_val[0]) + pow2(cplx_val[1]);
}

void SpectralManipPeriodic::_compute_largeModesForcing()
{
  fft_c *const cplxData_u = (fft_c *) data_u;
  fft_c *const cplxData_v = (fft_c *) data_v;
  fft_c *const cplxData_w = (fft_c *) data_w;
  const long nKx = static_cast<long>(gsize[0]);
  const long nKy = static_cast<long>(gsize[1]);
  const long nKz = static_cast<long>(gsize[2]);
  const Real waveFactorX = 2.0 * M_PI / sim.extent[0];
  const Real waveFactorY = 2.0 * M_PI / sim.extent[1];
  const Real waveFactorZ = 2.0 * M_PI / sim.extent[2];
  const long sizeX = gsize[0], sizeZ_hat = nz_hat;
  const long loc_n1 = local_n1, shifty = local_1_start;

  Real eps = 0, tke = 0, tkeFiltered = 0;
  #pragma omp parallel for reduction(+: eps, tke, tkeFiltered) schedule(static)
  for(long j = 0; j<loc_n1; ++j)
  for(long i = 0; i<sizeX;  ++i)
  for(long k = 0; k<sizeZ_hat; ++k)
  {
    const long linidx = (j*sizeX +i)*sizeZ_hat + k;
    const long ii = (i <= nKx/2) ? i : -(nKx-i);
    const long l = shifty + j; //memory index plus shift due to decomp
    const long jj = (l <= nKy/2) ? l : -(nKy-l);
    const long kk = (k <= nKz/2) ? k : -(nKz-k);

    const Real kx = ii*waveFactorX, ky = jj*waveFactorY, kz = kk*waveFactorZ;
    const Real k2 = kx*kx + ky*ky + kz*kz, k_norm = std::sqrt(k2);

    const Real mult = (k==0) or (k==nKz/2) ? 1 : 2;
    const Real E = mult/2 * ( pow2_cplx(cplxData_u[linidx])
      + pow2_cplx(cplxData_v[linidx]) + pow2_cplx(cplxData_w[linidx]) );
    tke += E; // Total kinetic energy
    eps += k2 * E; // Dissipation rate
    if (k_norm > 0 && k_norm <= 2) {
      tkeFiltered += E;
    } else {
      cplxData_u[linidx][0] = 0;
      cplxData_u[linidx][1] = 0;
      cplxData_v[linidx][0] = 0;
      cplxData_v[linidx][1] = 0;
      cplxData_w[linidx][0] = 0;
      cplxData_w[linidx][1] = 0;
    }
  }

  MPI_Allreduce(MPI_IN_PLACE, &tkeFiltered, 1, MPIREAL, MPI_SUM, m_comm);
  MPI_Allreduce(MPI_IN_PLACE, &tke, 1, MPIREAL, MPI_SUM, m_comm);
  MPI_Allreduce(MPI_IN_PLACE, &eps, 1, MPIREAL, MPI_SUM, m_comm);

  stats.tke_filtered *=  1 / pow2(normalizeFFT);
  stats.tke *= 1 / pow2(normalizeFFT);
  stats.eps *= 2*(sim.nu) / pow2(normalizeFFT);
}

void SpectralManipPeriodic::_compute_analysis()
{
  fft_c *const cplxData_u  = (fft_c *) data_u;
  fft_c *const cplxData_v  = (fft_c *) data_v;
  fft_c *const cplxData_w  = (fft_c *) data_w;
  //fft_c *const cplxData_cs2  = (fft_c *) data_cs2;
  const long nKx = static_cast<long>(gsize[0]);
  const long nKy = static_cast<long>(gsize[1]);
  const long nKz = static_cast<long>(gsize[2]);
  const Real waveFactorX = 2.0 * M_PI / sim.extent[0];
  const Real waveFactorY = 2.0 * M_PI / sim.extent[1];
  const Real waveFactorZ = 2.0 * M_PI / sim.extent[2];
  const long loc_n1 = local_n1, shifty = local_1_start;
  const long sizeX = gsize[0], sizeZ_hat = nz_hat;

  Real tke = 0, eps = 0, tauIntegral = 0;
  Real * const E_msr = stats.E_msr;
  const size_t nBins = stats.N;
  const Real nyquist = stats.nyquist;
  memset(E_msr, 0, nBins * sizeof(Real));

  // Let's only measure spectrum up to Nyquist.
  #pragma omp parallel for reduction(+ : E_msr[:nBins], tke, eps, tauIntegral)  schedule(static)
  for(long j = 0; j<loc_n1; ++j)
  for(long i = 0; i<sizeX;  ++i)
  for(long k = 0; k<sizeZ_hat; ++k)
  {
    const long linidx = (j*sizeX +i)*sizeZ_hat + k;
    const long ii = (i <= nKx/2) ? i : -(nKx-i);
    const long l = shifty + j; //memory index plus shift due to decomp
    const long jj = (l <= nKy/2) ? l : -(nKy-l);
    const long kk = (k <= nKz/2) ? k : -(nKz-k);

    const Real kx = ii*waveFactorX, ky = jj*waveFactorY, kz = kk*waveFactorZ;
    const Real mult = (k==0) or (k==nKz/2) ? 1 : 2;
    const Real k2 = kx*kx + ky*ky + kz*kz, k_norm = std::sqrt(k2);
    const Real ks = std::sqrt(ii*ii + jj*jj + kk*kk);
    const Real E = mult/2 * ( pow2_cplx(cplxData_u[linidx])
      + pow2_cplx(cplxData_v[linidx]) + pow2_cplx(cplxData_w[linidx]) );

    // Total kinetic energy
    tke += E;
    // Dissipation rate
    eps += k2 * E;
    // Large eddy turnover time
    tauIntegral += (k_norm > 0) ? E / k_norm : 0.;
    if (ks <= nyquist) {
      int binID = std::floor(ks * (nyquist-1)/nyquist);
      E_msr[binID] += E;
      //if (bComputeCs2Spectrum){
      //  const Real cs2 = std::sqrt(pow2_cplx(cplxData_cs2[linidx]));
      //  cs2_msr[binID] += mult*cs2;
      //}
    }
  }

  MPI_Allreduce(MPI_IN_PLACE, E_msr, nBins, MPIREAL, MPI_SUM, m_comm);
  MPI_Allreduce(MPI_IN_PLACE, &tke, 1, MPIREAL, MPI_SUM, m_comm);
  MPI_Allreduce(MPI_IN_PLACE, &eps, 1, MPIREAL, MPI_SUM, m_comm);
  MPI_Allreduce(MPI_IN_PLACE, &tauIntegral, 1, MPIREAL, MPI_SUM, m_comm);

  //if (bComputeCs2Spectrum){
  //  assert(false);
    //#pragma omp parallel reduction(+ : cs2_msr[:nBin])
    //MPI_Allreduce(MPI_IN_PLACE, cs2_msr, nBin, MPIREAL, MPI_SUM, sM->m_comm);
  //}

  for (size_t binID = 0; binID < nBins; binID++) {
    E_msr[binID] *= 1 / pow2(normalizeFFT);
    //if (bComputeCs2Spectrum) cs2_msr[binID] /= normalizeFFT;
  }

  stats.tke *= 1 / pow2(normalizeFFT);
  stats.eps *= 2 * (sim.nu) / pow2(normalizeFFT);

  stats.uprime = std::sqrt(2 * stats.tke / 3);
  stats.lambda = std::sqrt(15 * sim.nu / stats.eps) * stats.uprime;
  stats.Re_lambda = stats.uprime * stats.lambda / sim.nu;
  stats.tau_integral *= M_PI / (2*pow3(stats.uprime)) / pow2(normalizeFFT);
}

void SpectralManipPeriodic::_compute_IC(const std::vector<Real> &K,
                                        const std::vector<Real> &E)
{
  std::random_device seed;
  std::mt19937 gen(seed());
  std::uniform_real_distribution<Real> randUniform(0,1);

  const EnergySpectrum target(K, E);

  fft_c *const cplxData_u = (fft_c *) data_u;
  fft_c *const cplxData_v = (fft_c *) data_v;
  fft_c *const cplxData_w = (fft_c *) data_w;
  const long nKx = static_cast<long>(gsize[0]);
  const long nKy = static_cast<long>(gsize[1]);
  const long nKz = static_cast<long>(gsize[2]);
  const Real waveFactorX = 2.0 * M_PI / sim.extent[0];
  const Real waveFactorY = 2.0 * M_PI / sim.extent[1];
  const Real waveFactorZ = 2.0 * M_PI / sim.extent[2];
  const long sizeX = gsize[0], sizeZ_hat = nz_hat;
  const long loc_n1 = local_n1, shifty = local_1_start;

  #pragma omp parallel for schedule(static)
  for(long j = 0; j<loc_n1;  ++j)
  for(long i = 0; i<sizeX;    ++i)
  for(long k = 0; k<sizeZ_hat; ++k)
  {
    const long linidx = (j*sizeX +i) * sizeZ_hat + k;
    const long ii = (i <= nKx/2) ? i : -(nKx-i);
    const long l = shifty + j; //memory index plus shift due to decomp
    const long jj = (l <= nKy/2) ? l : -(nKy-l);
    const long kk = (k <= nKz/2) ? k : -(nKz-k);

    const Real kx = ii*waveFactorX, ky = jj*waveFactorY, kz = kk*waveFactorZ;
    const Real k2 = kx*kx + ky*ky + kz*kz;
    const Real k_xy = std::sqrt(kx*kx + ky*ky);
    const Real k_norm = std::sqrt(k2);

    const Real E_k = target.interpE(k_norm);
    const Real amp = (k2==0)? 0
                    : std::sqrt(E_k/(2*M_PI*std::pow(k_norm/waveFactorX,2)));

    const Real theta1 = randUniform(gen)*2*M_PI;
    const Real theta2 = randUniform(gen)*2*M_PI;
    const Real phi    = randUniform(gen)*2*M_PI;

    const Real noise_a[2] = {amp * std::cos(theta1) * std::cos(phi),
                             amp * std::sin(theta1) * std::cos(phi)};
    const Real noise_b[2] = {amp * std::cos(theta2) * std::sin(phi),
                             amp * std::sin(theta2) * std::sin(phi)};

    const Real fac = k_norm*k_xy, invFac = fac==0? 0 : 1/fac;

    cplxData_u[linidx][0] = k_norm==0? 0
                  : invFac * (noise_a[0] * k_norm * ky + noise_b[0] * kx * kz );
    cplxData_u[linidx][1] = k_norm==0? 0
                  : invFac * (noise_a[0] * k_norm * ky + noise_b[1] * kx * kz );

    cplxData_v[linidx][0] = k_norm==0? 0
                  : invFac * (noise_b[0] * ky * kz - noise_a[0] * k_norm * kx );
    cplxData_v[linidx][1] = k_norm==0? 0
                  : invFac * (noise_b[1] * ky * kz - noise_a[1] * k_norm * kx );

    cplxData_w[linidx][0] = k_norm==0? 0 : -noise_b[0] * k_xy / k_norm;
    cplxData_w[linidx][1] = k_norm==0? 0 : -noise_b[1] * k_xy / k_norm;
  }
}

SpectralManipPeriodic::SpectralManipPeriodic(SimulationData&s): SpectralManip(s)
{
  const int retval = _FFTW_(init_threads)();
  if(retval==0) {
    fprintf(stderr, "SpectralManip: ERROR: Call to fftw_init_threads() returned zero.\n");
    exit(1);
  }
  _FFTW_(mpi_init)();
  const int desired_threads = omp_get_max_threads();
  _FFTW_(plan_with_nthreads)(desired_threads);

  alloc_local = _FFTW_(mpi_local_size_3d_transposed) (
    gsize[0], gsize[1], gsize[2]/2+1, m_comm,
    &local_n0, &local_0_start, &local_n1, &local_1_start);

  data_size = (size_t) myN[0] * (size_t) myN[1] * (size_t) 2*nz_hat;
  stridez = 1; // fast
  stridey = 2*(nz_hat);
  stridex = myN[1] * 2*(nz_hat); // slow

  data_u = _FFTW_(alloc_real)(2*alloc_local);
  data_v = _FFTW_(alloc_real)(2*alloc_local);
  data_w = _FFTW_(alloc_real)(2*alloc_local);
  // data_cs2 = _FFTW_(alloc_real)(2*alloc_local);
}

void SpectralManipPeriodic::prepareFwd()
{
  if (bAllocFwd) return;

  fwd_u = (void*) _FFTW_(mpi_plan_dft_r2c_3d)(gsize[0], gsize[1], gsize[2],
    data_u, (fft_c*)data_u, m_comm, FFTW_MPI_TRANSPOSED_OUT  | FFTW_MEASURE);
  fwd_v = (void*) _FFTW_(mpi_plan_dft_r2c_3d)(gsize[0], gsize[1], gsize[2],
    data_v, (fft_c*)data_v, m_comm, FFTW_MPI_TRANSPOSED_OUT  | FFTW_MEASURE);
  fwd_w = (void*) _FFTW_(mpi_plan_dft_r2c_3d)(gsize[0], gsize[1], gsize[2],
    data_w, (fft_c*)data_w, m_comm, FFTW_MPI_TRANSPOSED_OUT  | FFTW_MEASURE);

  //fwd_cs2 = (void*) _FFTW_(mpi_plan_dft_r2c_3d)(gsize[0], gsize[1], gsize[2],
  //data_cs2, (fft_c*)data_cs2, m_comm, FFTW_MPI_TRANSPOSED_OUT | FFTW_MEASURE);
  bAllocFwd = true;
}

void SpectralManipPeriodic::prepareBwd()
{
  if (bAllocBwd) return;

  bwd_u = (void*) _FFTW_(mpi_plan_dft_c2r_3d)(gsize[0], gsize[1], gsize[2],
    (fft_c*)data_u, data_u, m_comm, FFTW_MPI_TRANSPOSED_IN  | FFTW_MEASURE);
  bwd_v = (void*) _FFTW_(mpi_plan_dft_c2r_3d)(gsize[0], gsize[1], gsize[2],
    (fft_c*)data_v, data_v, m_comm, FFTW_MPI_TRANSPOSED_IN  | FFTW_MEASURE);
  bwd_w = (void*) _FFTW_(mpi_plan_dft_c2r_3d)(gsize[0], gsize[1], gsize[2],
    (fft_c*)data_w, data_w, m_comm, FFTW_MPI_TRANSPOSED_IN  | FFTW_MEASURE);

  bAllocBwd = true;
}

void SpectralManipPeriodic::runFwd() const
{
  assert(bAllocFwd);
  // we can use one plan for multiple data:
  //_FFTW_(execute_dft_r2c)( (fft_plan) fwd_u, data_u, (fft_c*)data_u );
  //_FFTW_(execute_dft_r2c)( (fft_plan) fwd_u, data_v, (fft_c*)data_v );
  //_FFTW_(execute_dft_r2c)( (fft_plan) fwd_u, data_w, (fft_c*)data_w );
  _FFTW_(execute)((fft_plan) fwd_u);
  _FFTW_(execute)((fft_plan) fwd_v);
  _FFTW_(execute)((fft_plan) fwd_w);

  // _FFTW_(execute)((fft_plan) fwd_cs2);
}

void SpectralManipPeriodic::runBwd() const
{
  assert(bAllocBwd);
  // we can use one plan for multiple data:
  //_FFTW_(execute_dft_c2r)( (fft_plan) bwd_u, (fft_c*)data_u, data_u );
  //_FFTW_(execute_dft_c2r)( (fft_plan) bwd_u, (fft_c*)data_v, data_v );
  //_FFTW_(execute_dft_c2r)( (fft_plan) bwd_u, (fft_c*)data_w, data_w );
  _FFTW_(execute)((fft_plan) bwd_u);
  _FFTW_(execute)((fft_plan) bwd_v);
  _FFTW_(execute)((fft_plan) bwd_w);
}

SpectralManipPeriodic::~SpectralManipPeriodic()
{
  _FFTW_(free)(data_u);
  _FFTW_(free)(data_v);
  _FFTW_(free)(data_w);
  // _FFTW_(free)(data_cs2);
  if (bAllocFwd) {
    _FFTW_(destroy_plan)((fft_plan) fwd_u);
    _FFTW_(destroy_plan)((fft_plan) fwd_v);
    _FFTW_(destroy_plan)((fft_plan) fwd_w);
    // _FFTW_(destroy_plan)((fft_plan) fwd_cs2);
  }
  if (bAllocBwd) {
    _FFTW_(destroy_plan)((fft_plan) bwd_u);
    _FFTW_(destroy_plan)((fft_plan) bwd_v);
    _FFTW_(destroy_plan)((fft_plan) bwd_w);
  }
  _FFTW_(mpi_cleanup)();
}

CubismUP_3D_NAMESPACE_END
#undef MPIREAL