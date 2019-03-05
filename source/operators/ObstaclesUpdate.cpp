//
//  Cubism3D
//  Copyright (c) 2018 CSE-Lab, ETH Zurich, Switzerland.
//  Distributed under the terms of the MIT license.
//
//  Created by Guido Novati (novatig@ethz.ch).
//

#include "operators/ObstaclesUpdate.h"
#include "obstacles/ObstacleVector.h"
#include "utils/MatArrayMath.h"

CubismUP_3D_NAMESPACE_BEGIN
using namespace cubism;

using CHIMAT = Real[CUP_BLOCK_SIZE][CUP_BLOCK_SIZE][CUP_BLOCK_SIZE];
using UDEFMAT = Real[CUP_BLOCK_SIZE][CUP_BLOCK_SIZE][CUP_BLOCK_SIZE][3];
static constexpr Real EPS = std::numeric_limits<Real>::epsilon();
static constexpr Real DBLEPS = std::numeric_limits<double>::epsilon();

struct KernelIntegrateFluidMomenta : public ObstacleVisitor
{
  const double lambda, dt;
  ObstacleVector * const obstacle_vector;
  const cubism::BlockInfo * info_ptr = nullptr;
  inline double dvol(const cubism::BlockInfo&info, const int x, const int y, const int z) const {
    double h[3]; info.spacing(h, x, y, z);
    return h[0] * h[1] * h[2];
  }

  KernelIntegrateFluidMomenta(double _dt, double _lambda, ObstacleVector* ov)
    : lambda(_lambda), dt(_dt), obstacle_vector(ov) {}

  void operator()(const cubism::BlockInfo& info)
  {
    // first store the lab and info, then do visitor
    assert(info_ptr == nullptr);
    info_ptr = & info;
    ObstacleVisitor* const base = static_cast<ObstacleVisitor*> (this);
    assert( base not_eq nullptr );
    obstacle_vector->Accept( base );
    info_ptr = nullptr;
  }

  void visit(Obstacle* const op)
  {
    const BlockInfo& info = * info_ptr;
    assert(info_ptr not_eq nullptr);
    const std::vector<ObstacleBlock*>& obstblocks = op->getObstacleBlocks();
    ObstacleBlock*const o = obstblocks[info.blockID];
    if (o == nullptr) return;

    const std::array<double,3> CM = op->getCenterOfMass();
    const std::array<double,3> omega = op->getAngularVelocity();
    const std::array<double,3> UT = op->getTranslationVelocity();
    const FluidBlock &b = *(FluidBlock *)info.ptrBlock;
    CHIMAT & __restrict__ CHI = o->chi;
    UDEFMAT & __restrict__ UDEF = o->udef;
    double &VV = o->V;
    double &FX = o->FX, &FY = o->FY, &FZ = o->FZ;
    double &TX = o->TX, &TY = o->TY, &TZ = o->TZ;
    double &J0 = o->J0, &J1 = o->J1, &J2 = o->J2;
    double &J3 = o->J3, &J4 = o->J4, &J5 = o->J5;
    double &GX = o->GX, &G0 = o->G0, &G1 = o->G1, &G2 = o->G2;
    double &G3 = o->G3, &G4 = o->G4, &G5 = o->G5;

    VV = 0; FX = 0; FY = 0; FZ = 0; TX = 0; TY = 0; TZ = 0;
    J0 = 0; J1 = 0; J2 = 0; J3 = 0; J4 = 0; J5 = 0;
    GX = 0; G0 = 0; G1 = 0; G2 = 0; G3 = 0; G4 = 0; G5 = 0;

    for(int iz=0; iz<FluidBlock::sizeZ; iz++)
    for(int iy=0; iy<FluidBlock::sizeY; iy++)
    for(int ix=0; ix<FluidBlock::sizeX; ix++) {
      if (CHI[iz][iy][ix] <= 0) continue;
      double p[3]; info.pos(p, ix, iy, iz);
      const double dv = dvol(info, ix, iy, iz), X = CHI[iz][iy][ix];
      p[0] -= CM[0]; p[1] -= CM[1]; p[2] -= CM[2];
      const double UDIFF[3] = { // uf - us
       b(ix,iy,iz).u -UT[0] -(omega[1]*p[2]-omega[2]*p[1]) -UDEF[iz][iy][ix][0],
       b(ix,iy,iz).v -UT[1] -(omega[2]*p[0]-omega[0]*p[2]) -UDEF[iz][iy][ix][1],
       b(ix,iy,iz).w -UT[2] -(omega[0]*p[1]-omega[1]*p[0]) -UDEF[iz][iy][ix][2]
      };
      const double penal = dv * X*lambda/(1 + X*lambda*dt), inert = dt*penal;
      VV += X * dv;
      J0 += X * dv * ( p[1]*p[1] + p[2]*p[2] );
      J1 += X * dv * ( p[0]*p[0] + p[2]*p[2] );
      J2 += X * dv * ( p[0]*p[0] + p[1]*p[1] );
      J3 -= X * dv * p[0]*p[1];
      J4 -= X * dv * p[0]*p[2];
      J5 -= X * dv * p[1]*p[2];

      FX += penal * UDIFF[0];
      FY += penal * UDIFF[1];
      FZ += penal * UDIFF[2];
      TX += penal * ( p[1] * UDIFF[2] - p[2] * UDIFF[1] );
      TY += penal * ( p[2] * UDIFF[0] - p[0] * UDIFF[2] );
      TZ += penal * ( p[0] * UDIFF[1] - p[1] * UDIFF[0] );

      GX += inert;
      G0 += inert * ( p[1]*p[1] + p[2]*p[2] );
      G1 += inert * ( p[0]*p[0] + p[2]*p[2] );
      G2 += inert * ( p[0]*p[0] + p[1]*p[1] );
      G3 -= inert *   p[0]*p[1];
      G4 -= inert *   p[0]*p[2];
      G5 -= inert *   p[1]*p[2];
    }
  }
};

struct KernelFinalizeObstacleVel : public ObstacleVisitor
{
  const double dt, lambda;
  FluidGridMPI * const grid;

  std::array<   int, 3> nSum = {{0, 0, 0}};
  std::array<double, 3> uSum = {{0, 0, 0}};

  KernelFinalizeObstacleVel(double _dt, double _lambda, FluidGridMPI*g) :
    dt(_dt), lambda(_lambda), grid(g) { }

  void visit(Obstacle* const obst)
  {
    double M[20] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    const auto& oBlock = obst->getObstacleBlocks();
    #pragma omp parallel for schedule(static,1) reduction(+ : M[:20])
    for (size_t i=0; i<oBlock.size(); i++) {
      if(oBlock[i] == nullptr) continue;
      M[ 0] += oBlock[i]->V ;
      M[ 1] += oBlock[i]->FX; M[ 2] += oBlock[i]->FY; M[ 3] += oBlock[i]->FZ;
      M[ 4] += oBlock[i]->TX; M[ 5] += oBlock[i]->TY; M[ 6] += oBlock[i]->TZ;
      M[ 7] += oBlock[i]->J0; M[ 8] += oBlock[i]->J1; M[ 9] += oBlock[i]->J2;
      M[10] += oBlock[i]->J3; M[11] += oBlock[i]->J4; M[12] += oBlock[i]->J5;
      M[13] += oBlock[i]->GX;
      M[14] += oBlock[i]->G0; M[15] += oBlock[i]->G1; M[16] += oBlock[i]->G2;
      M[17] += oBlock[i]->G3; M[18] += oBlock[i]->G4; M[19] += oBlock[i]->G5;
    }
    const auto comm = grid->getCartComm();
    MPI_Allreduce(MPI_IN_PLACE, M, 20, MPI_DOUBLE, MPI_SUM, comm);
    assert(M[0] > DBLEPS);

    const double mass = M[0], penalFac = M[13];
    const SymM J = {{ M[ 7], M[ 8], M[ 9], M[10], M[11], M[12] }};
    const SymM G = {{ M[14], M[15], M[16], M[17], M[18], M[19] }};
    //solve avel = invJ \dot angMomentum
    const SymM invJ = invertSym(J);
    const GenM GinvJ = multSyms(G, invJ);
    const int bRotate[3] = { obst->bBlockRotation[0] ? 0 : 1,
                             obst->bBlockRotation[1] ? 0 : 1,
                             obst->bBlockRotation[2] ? 0 : 1 };
    const GenM EyeGinvJ = {{ //if locked obst, skip correction due to implicit
      1 +GinvJ[0]*bRotate[0],    GinvJ[1]*bRotate[1],    GinvJ[2]*bRotate[2],
         GinvJ[3]*bRotate[0], 1 +GinvJ[4]*bRotate[1],    GinvJ[5]*bRotate[2],
         GinvJ[6]*bRotate[0],    GinvJ[7]*bRotate[1], 1 +GinvJ[8]*bRotate[2] }};
    const GenM invPenMom = invertGen(EyeGinvJ);
    const GenV implAngMom = {{ M[4], M[5], M[6] }};
    // implicit computation of torque for freely-moving obstacle:
    const GenV implTorque = multGenVec(invPenMom, implAngMom);
    const GenV implAngAcc = multSymVec(invJ, implTorque);
    assert(std::fabs(obst->mass - M[ 0]) < 10*DBLEPS);
    assert(std::fabs(obst->J[0] - M[ 7]) < 10*DBLEPS);
    assert(std::fabs(obst->J[1] - M[ 8]) < 10*DBLEPS);
    assert(std::fabs(obst->J[2] - M[ 9]) < 10*DBLEPS);
    assert(std::fabs(obst->J[3] - M[10]) < 10*DBLEPS);
    assert(std::fabs(obst->J[4] - M[11]) < 10*DBLEPS);
    assert(std::fabs(obst->J[5] - M[12]) < 10*DBLEPS);

    obst->force[0] = M[1] * (obst->bForcedInSimFrame[0]? 1 : 1/(1+penalFac) );
    obst->force[1] = M[2] * (obst->bForcedInSimFrame[1]? 1 : 1/(1+penalFac) );
    obst->force[2] = M[3] * (obst->bForcedInSimFrame[2]? 1 : 1/(1+penalFac) );
    obst->torque[0] = implTorque[0];
    obst->torque[1] = implTorque[1];
    obst->torque[2] = implTorque[2];
    obst->transVel_computed[0] = obst->transVel[0] + dt * obst->force[0] / mass;
    obst->transVel_computed[1] = obst->transVel[1] + dt * obst->force[1] / mass;
    obst->transVel_computed[2] = obst->transVel[2] + dt * obst->force[2] / mass;
    obst->angVel_computed[0] = obst->angVel[0] + dt * implAngAcc[0];
    obst->angVel_computed[1] = obst->angVel[1] + dt * implAngAcc[1];
    obst->angVel_computed[2] = obst->angVel[2] + dt * implAngAcc[2];

    obst->computeVelocities();

    const auto &bFixFrameOfRef = obst->bFixFrameOfRef;
    if (bFixFrameOfRef[0]) { nSum[0]++; uSum[0] -= obst->transVel[0]; }
    if (bFixFrameOfRef[1]) { nSum[1]++; uSum[1] -= obst->transVel[1]; }
    if (bFixFrameOfRef[2]) { nSum[2]++; uSum[2] -= obst->transVel[2]; }
  }
};

void UpdateObstacles::operator()(const double dt)
{
  sim.startProfiler("Obst Int Vel");
  { // integrate momenta by looping over grid
    #pragma omp parallel
    { // each thread needs to call its own non-const operator() function
      auto K = KernelIntegrateFluidMomenta(dt, sim.lambda, sim.obstacle_vector);
      #pragma omp for schedule(dynamic, 1)
      for (size_t i = 0; i < vInfo.size(); ++i) K(vInfo[i]);
    }
  }
  sim.stopProfiler();

  sim.startProfiler("Obst Upd Vel");
  auto* K = new KernelFinalizeObstacleVel(dt, sim.lambda, sim.grid);
  ObstacleVisitor* kernel = static_cast<ObstacleVisitor*>(K);
  assert(kernel not_eq nullptr);
  sim.obstacle_vector->Accept(kernel); // accept you son of a french cow
  if( K->nSum[0] > 0 ) sim.uinf[0] = K->uSum[0] / K->nSum[0];
  if( K->nSum[1] > 0 ) sim.uinf[1] = K->uSum[1] / K->nSum[1];
  if( K->nSum[2] > 0 ) sim.uinf[2] = K->uSum[2] / K->nSum[2];
  //if(rank == 0) if(nSum[0] || nSum[1] || nSum[2])
  //  printf("New Uinf %g %g %g (from %d %d %d)\n",
  //  uInf[0],uInf[1],uInf[2],nSum[0],nSum[1],nSum[2]);
  delete K;

  sim.obstacle_vector->update();
  sim.stopProfiler();

  check("UpdateObstacles");
}

CubismUP_3D_NAMESPACE_END
